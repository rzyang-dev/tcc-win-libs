/* tcwinreadline.c - 终端行编辑器实现
 *
 * 分两层：
 *   第一层 tcwin_rl_feed()  —— 纯状态机，不碰任何 I/O
 *   第二层 tcwin_rl_readline() —— Win32 控制台驱动
 *
 * 详见 include/tcwinreadline.h 的说明。
 */
#include "tccwin.h"
#include <stdio.h>
#include <string.h>

#include "tcwinreadline.h"
#include "tcwin_rl_utf8.h"   /* UTF-8 / 显示宽度纯逻辑 */

/* ============================================================
 * 第一层：行编辑状态机
 * ============================================================ */

static void tcwin_rl__copy(char *dst, int dstsz, const char *src, int srclen) {
    int n = srclen;
    if (n > dstsz - 1) n = dstsz - 1;
    if (n > 0) memcpy(dst, src, (size_t)n);
    dst[n] = 0;
}

void tcwin_rl_init(tcwin_rl_t *rl, const char *prompt) {
    if (!rl) return;
    memset(rl, 0, sizeof *rl);
    if (prompt) tcwin_rl__copy(rl->prompt, (int)sizeof rl->prompt, prompt, (int)strlen(prompt));
    rl->browse = -1;
    rl->cursor = 0;
    rl->len = 0;
    rl->line[0] = 0;
}

void tcwin_rl_clear_line(tcwin_rl_t *rl) {
    if (!rl) return;
    rl->len = 0;
    rl->cursor = 0;
    rl->line[0] = 0;
    /* 退出历史浏览状态，但不丢历史本身 */
    if (rl->browse >= 0) rl->browse = -1;
}

const char *tcwin_rl_line(const tcwin_rl_t *rl)   { return rl ? rl->line : ""; }
int tcwin_rl_length(const tcwin_rl_t *rl)         { return rl ? rl->len : 0; }
int tcwin_rl_cursor(const tcwin_rl_t *rl)         { return rl ? rl->cursor : 0; }
int tcwin_rl_history_count(const tcwin_rl_t *rl)  { return rl ? rl->hist_len : 0; }

void tcwin_rl_history_add(tcwin_rl_t *rl, const char *line) {
    int len;
    int prev;
    if (!rl || !line) return;
    len = (int)strlen(line);
    if (len == 0) return;                       /* 忽略空行 */
    if (len > TCWIN_RL_LINE_MAX - 1) len = TCWIN_RL_LINE_MAX - 1;

    /* 与最新一条相同就不重复记录（连续重复输入很常见） */
    if (rl->hist_len > 0) {
        prev = rl->hist_head - 1;
        if (prev < 0) prev = TCWIN_RL_HISTORY_MAX - 1;
        if (strncmp(rl->hist[prev], line, (size_t)len) == 0 &&
            rl->hist[prev][len] == 0) {
            return;
        }
    }

    tcwin_rl__copy(rl->hist[rl->hist_head], TCWIN_RL_LINE_MAX, line, len);
    rl->hist_head = (rl->hist_head + 1) % TCWIN_RL_HISTORY_MAX;
    if (rl->hist_len < TCWIN_RL_HISTORY_MAX) rl->hist_len++;
}

const char *tcwin_rl_history_get(const tcwin_rl_t *rl, int i) {
    int idx;
    if (!rl || i < 0 || i >= rl->hist_len) return NULL;
    /* hist[0] 是最新的：从 hist_head-1 往回数 */
    idx = rl->hist_head - 1 - i;
    while (idx < 0) idx += TCWIN_RL_HISTORY_MAX;
    return rl->hist[idx];
}

/* 在光标处插入一个字节（ASCII 路径，也是 UTF-8 序列的底层写入） */
static int tcwin_rl__insert(tcwin_rl_t *rl, char c) {
    if (rl->len >= TCWIN_RL_LINE_MAX - 1) return TCWIN_RL_CONTINUE;   /* 满，丢弃 */
    if (rl->cursor < rl->len) {
        memmove(rl->line + rl->cursor + 1, rl->line + rl->cursor,
                (size_t)(rl->len - rl->cursor));
    }
    rl->line[rl->cursor] = c;
    rl->cursor++;
    rl->len++;
    rl->line[rl->len] = 0;
    return TCWIN_RL_CONTINUE;
}

/* 在光标处插入一个完整的 UTF-8 序列（首字节 + 后续字节）。
 *
 * 光标落在这个序列**之后** —— 从用户角度看"输入一个中文字"是一次原子
 * 操作，不应出现光标停在半个字符中间的中间态。
 *
 * 空间不足时整段丢弃（不做部分插入，否则留下半个字符）。
 * 数据不全时退化为只插首字节，保持与旧行为一致，不至于丢字符。 */
static int tcwin_rl__insert_seq(tcwin_rl_t *rl, char first, const char *tail, int taillen) {
    int need, i;

    need = tcwin_u8_seqlen((unsigned char)first);
    if (need <= 1 || taillen < need - 1) {
        return tcwin_rl__insert(rl, first);      /* 单字节，或 tail 数据不全 */
    }
    if (rl->len + need > TCWIN_RL_LINE_MAX - 1) return TCWIN_RL_CONTINUE;  /* 装不下 */

    if (rl->cursor < rl->len) {
        memmove(rl->line + rl->cursor + need, rl->line + rl->cursor,
                (size_t)(rl->len - rl->cursor));
    }
    rl->line[rl->cursor] = first;
    for (i = 0; i < need - 1; i++) rl->line[rl->cursor + 1 + i] = tail[i];
    rl->cursor += need;
    rl->len    += need;
    rl->line[rl->len] = 0;
    return TCWIN_RL_CONTINUE;
}

/* 删除光标左侧**一个码点**（不是一字节）。
 *
 * 找不到合法序列起点时退化为删一个字节 —— 畸形输入下也要能动，不能卡住。 */
static void tcwin_rl__backspace(tcwin_rl_t *rl) {
    int prev;
    if (rl->cursor <= 0) return;
    prev = tcwin_u8_prev(rl->line, rl->cursor);
    if (prev >= rl->cursor) prev = rl->cursor - 1;   /* 兜底：至少退一字节 */
    memmove(rl->line + prev, rl->line + rl->cursor,
            (size_t)(rl->len - rl->cursor));
    rl->len -= (rl->cursor - prev);
    rl->cursor = prev;
    rl->line[rl->len] = 0;
}

/* 删除光标处**一个码点**（不是一字节）。 */
static void tcwin_rl__delete(tcwin_rl_t *rl) {
    int next;
    if (rl->cursor >= rl->len) return;
    next = tcwin_u8_next(rl->line, rl->cursor, rl->len);
    if (next <= rl->cursor) next = rl->cursor + 1;   /* 兜底 */
    if (next > rl->len) next = rl->len;
    memmove(rl->line + rl->cursor, rl->line + next,
            (size_t)(rl->len - next + 1));           /* +1 连 NUL 一起搬 */
    rl->len -= (next - rl->cursor);
}

/* 光标左移一个词 */
static void tcwin_rl__word_left(tcwin_rl_t *rl) {
    int c = rl->cursor;
    while (c > 0 && rl->line[c - 1] == ' ') c--;
    while (c > 0 && rl->line[c - 1] != ' ') c--;
    rl->cursor = c;
}

/* 把当前编辑内容挪到浏览暂存区（只在首次进入浏览时做） */
static void tcwin_rl__browse_save(tcwin_rl_t *rl) {
    if (rl->browse >= 0) return;      /* 已经在浏览中，不要覆盖 */
    tcwin_rl__copy(rl->saved, TCWIN_RL_LINE_MAX, rl->line, rl->len);
    rl->saved_len = rl->len;
    rl->saved_cursor = rl->cursor;
}

/* 从浏览暂存区恢复 */
static void tcwin_rl__browse_restore(tcwin_rl_t *rl) {
    tcwin_rl__copy(rl->line, TCWIN_RL_LINE_MAX, rl->saved, rl->saved_len);
    rl->len = rl->saved_len;
    rl->cursor = rl->saved_cursor;
    if (rl->cursor > rl->len) rl->cursor = rl->len;
}

/* 载入第 i 条历史到当前行 */
static void tcwin_rl__browse_load(tcwin_rl_t *rl, int i) {
    const char *h = tcwin_rl_history_get(rl, i);
    int n;
    if (!h) return;
    n = (int)strlen(h);
    tcwin_rl__copy(rl->line, TCWIN_RL_LINE_MAX, h, n);
    rl->len = n;
    rl->cursor = n;                 /* 光标放到行尾，符合常见习惯 */
    rl->browse = i;
}

static int tcwin_rl__history_prev(tcwin_rl_t *rl) {
    if (rl->hist_len == 0) return TCWIN_RL_CONTINUE;
    tcwin_rl__browse_save(rl);
    if (rl->browse < 0) {
        tcwin_rl__browse_load(rl, 0);                 /* 最新一条 */
    } else if (rl->browse + 1 < rl->hist_len) {
        tcwin_rl__browse_load(rl, rl->browse + 1);    /* 更早一条 */
    }
    /* 已在最老一条：不动 */
    return TCWIN_RL_CONTINUE;
}

static int tcwin_rl__history_next(tcwin_rl_t *rl) {
    if (rl->browse < 0) return TCWIN_RL_CONTINUE;     /* 不在浏览中，不动 */
    if (rl->browse > 0) {
        tcwin_rl__browse_load(rl, rl->browse - 1);    /* 较新一条 */
    } else {
        /* 回到用户进入浏览前正在编辑的内容 */
        rl->browse = -1;
        tcwin_rl__browse_restore(rl);
    }
    return TCWIN_RL_CONTINUE;
}

int tcwin_rl_feed(tcwin_rl_t *rl, tcwin_rl_event ev) {
    if (!rl) return TCWIN_RL_CONTINUE;

    switch (ev.key) {
    case TCWIN_RL_KEY_CHAR:
        /* 可打印字符。ASCII 走单字节路径；UTF-8 多字节序列由 u8_tail 带上
         * 其余字节，整段原子插入。 */
        if (ev.u8_tail_len > 0 && (unsigned char)ev.ch >= 0x80) {
            return tcwin_rl__insert_seq(rl, ev.ch, ev.u8_tail, ev.u8_tail_len);
        }
        if ((unsigned char)ev.ch >= 0x20 && ev.ch != 0x7F) {
            return tcwin_rl__insert(rl, ev.ch);
        }
        return TCWIN_RL_CONTINUE;

    case TCWIN_RL_KEY_ENTER:
        return TCWIN_RL_SUBMIT;

    case TCWIN_RL_KEY_BACKSPACE:
        tcwin_rl__backspace(rl);
        return TCWIN_RL_CONTINUE;

    case TCWIN_RL_KEY_DELETE:
        tcwin_rl__delete(rl);
        return TCWIN_RL_CONTINUE;

    case TCWIN_RL_KEY_LEFT:
        /* 左移一个**码点**，不是一字节 —— 否则光标会停在半个中文里 */
        rl->cursor = tcwin_u8_prev(rl->line, rl->cursor);
        return TCWIN_RL_CONTINUE;

    case TCWIN_RL_KEY_RIGHT:
        rl->cursor = tcwin_u8_next(rl->line, rl->cursor, rl->len);
        return TCWIN_RL_CONTINUE;

    case TCWIN_RL_KEY_HOME:
        rl->cursor = 0;
        return TCWIN_RL_CONTINUE;

    case TCWIN_RL_KEY_END:
        rl->cursor = rl->len;
        return TCWIN_RL_CONTINUE;

    case TCWIN_RL_KEY_UP:
        return tcwin_rl__history_prev(rl);

    case TCWIN_RL_KEY_DOWN:
        return tcwin_rl__history_next(rl);

    case TCWIN_RL_KEY_CTRL_A: rl->cursor = 0;     return TCWIN_RL_CONTINUE;
    case TCWIN_RL_KEY_CTRL_E: rl->cursor = rl->len; return TCWIN_RL_CONTINUE;
    case TCWIN_RL_KEY_CTRL_B:
        if (rl->cursor > 0) rl->cursor--;
        return TCWIN_RL_CONTINUE;
    case TCWIN_RL_KEY_CTRL_F:
        if (rl->cursor < rl->len) rl->cursor++;
        return TCWIN_RL_CONTINUE;

    case TCWIN_RL_KEY_CTRL_U:
        /* 删除光标左侧全部 */
        if (rl->cursor > 0) {
            memmove(rl->line, rl->line + rl->cursor, (size_t)(rl->len - rl->cursor));
            rl->len -= rl->cursor;
            rl->cursor = 0;
            rl->line[rl->len] = 0;
        }
        return TCWIN_RL_CONTINUE;

    case TCWIN_RL_KEY_CTRL_K:
        /* 删除光标右侧全部 */
        if (rl->cursor < rl->len) {
            rl->len = rl->cursor;
            rl->line[rl->len] = 0;
        }
        return TCWIN_RL_CONTINUE;

    case TCWIN_RL_KEY_CTRL_W: {
        /* 删除光标左侧一个词（连同前面的空格） */
        int newcur = rl->cursor;
        while (newcur > 0 && rl->line[newcur - 1] == ' ') newcur--;
        while (newcur > 0 && rl->line[newcur - 1] != ' ') newcur--;
        if (newcur != rl->cursor) {
            memmove(rl->line + newcur, rl->line + rl->cursor,
                    (size_t)(rl->len - rl->cursor));
            rl->len -= (rl->cursor - newcur);
            rl->cursor = newcur;
            rl->line[rl->len] = 0;
        }
        return TCWIN_RL_CONTINUE;
    }

    case TCWIN_RL_KEY_CTRL_L:
        return TCWIN_RL_REDRAW;

    case TCWIN_RL_KEY_CTRL_C:
        return TCWIN_RL_ABORT;

    case TCWIN_RL_KEY_CTRL_D:
        if (rl->len == 0) return TCWIN_RL_EOF;
        tcwin_rl__delete(rl);
        return TCWIN_RL_CONTINUE;

    case TCWIN_RL_KEY_ESCAPE:
    case TCWIN_RL_KEY_NONE:
    default:
        return TCWIN_RL_CONTINUE;
    }
}

/* ============================================================
 * 第二层：Win32 控制台驱动
 * ============================================================ */

#define TCWIN_RL_WIN32 1

/* 重绘整行：清掉当前行、重画提示符与内容、把光标放回去。
 * 用 \r 回到行首 + 逐字符覆盖，不用清屏 API（避免闪烁）。 */
static void tcwin_rl__redraw(tcwin_rl_t *rl, HANDLE hout) {
    DWORD written = 0;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    int vis_col = 0;
    int i;
    char buf[TCWIN_RL_LINE_MAX + 128];
    int n = 0;

    /* 光标当前所在列（相对于行首）。
     * 重绘时先把光标移回行首，所以需要知道提示符占了多宽。 */
    if (GetConsoleScreenBufferInfo(hout, &csbi)) {
        vis_col = csbi.dwCursorPosition.X;
    } else {
        vis_col = 0;
    }
    (void)vis_col;

    buf[n++] = '\r';
    /* 提示符 */
    for (i = 0; rl->prompt[i] && n < (int)sizeof buf - 2; i++) buf[n++] = rl->prompt[i];
    /* 行内容 */
    for (i = 0; i < rl->len && n < (int)sizeof buf - 2; i++) buf[n++] = rl->line[i];
    /* 用空格擦掉上次留下的尾巴（多写几个空格无害） */
    for (i = 0; i < 4 && n < (int)sizeof buf - 1; i++) buf[n++] = ' ';
    buf[n++] = '\r';

    WriteConsoleA(hout, buf, (DWORD)n, &written, NULL);

    /* 光标移到「提示符**显示宽度** + 光标左侧内容的**显示宽度**」处。
     *
     * 这里必须用显示宽度而不是字节数/长度：一个中文占 2 列、占用 3 字节，
     * 用字节数定位会让光标跑到字符右边去；用码点数也不对（中文 1 码点 2 列）。 */
    {
        COORD pos;
        int promptw = tcwin_u8_display_width(rl->prompt, (int)strlen(rl->prompt));
        int leftw   = tcwin_u8_display_width(rl->line, rl->cursor);
        pos.X = (SHORT)(promptw + leftw);
        pos.Y = csbi.dwCursorPosition.Y;
        SetConsoleCursorPosition(hout, pos);
    }
}

/* 把 Win32 按键记录翻译成 tcwin_rl_event。
 * 返回 0 表示这条记录不产生事件（如窗口尺寸变化、按键抬起）。
 *
 * 这是公开 API 而不是 static：它是"控制台层"唯一有分支逻辑的部分，
 * 暴露出来才能在不接触真实终端的情况下测试它（见 probe_readline.c）。 */
int tcwin_rl_translate_key(const INPUT_RECORD *rec, tcwin_rl_event *ev) {
    WORD vk;
    DWORD ctrl;

    if (rec->EventType != KEY_EVENT) return 0;
    if (!rec->Event.KeyEvent.bKeyDown) return 0;    /* 只处理按下 */

    vk   = rec->Event.KeyEvent.wVirtualKeyCode;
    ctrl = rec->Event.KeyEvent.dwControlKeyState;

    ev->key = TCWIN_RL_KEY_NONE;
    ev->ch  = rec->Event.KeyEvent.uChar.AsciiChar;
    ev->u8_tail_len = 0;
    ev->u8_tail[0] = ev->u8_tail[1] = ev->u8_tail[2] = 0;

    /* 非 ASCII 输入：Windows 控制台把字符放在 uChar.UnicodeChar（UTF-16），
     * 而 AsciiChar 只保留低字节 —— 直接用它会把"中"变成 0x2D（'-'）。
     *
     * 实测踩到：这正是 P59 记录的编码类静默错误（值看着像字符但是错的）。
     * 修法是取 UnicodeChar 转成 UTF-8，多字节部分放进 u8_tail。
     *
     * 注意代理对（U+10000 以上，如 emoji）：控制台会分两次送
     * （高位代理、低位代理各一次按键）。这里**不做跨事件拼合**，
     * 落单的代理会被 tcwin_u8_from_utf16 替换成 U+FFFD。
     * 「未验证边界」：emoji 输入在真实控制台上会显示成替换字符。 */
    if (ev->ch == 0) {
        WCHAR wc = rec->Event.KeyEvent.uChar.UnicodeChar;
        if (wc >= 0x20 && wc != 0x7F) {
            char u8[8];
            int n8 = tcwin_u8_from_utf16(&wc, 1, u8, (int)sizeof u8);
            if (n8 >= 1) {
                ev->ch = u8[0];
                if (n8 > 1 && n8 <= 4) {
                    int k;
                    for (k = 1; k < n8; k++) ev->u8_tail[k - 1] = u8[k];
                    ev->u8_tail_len = n8 - 1;
                }
            }
        }
    }

    /* Ctrl 组合键优先 */
    if (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) {
        switch (vk) {
        case 'A': ev->key = TCWIN_RL_KEY_CTRL_A; return 1;
        case 'B': ev->key = TCWIN_RL_KEY_CTRL_B; return 1;
        case 'C': ev->key = TCWIN_RL_KEY_CTRL_C; return 1;
        case 'D': ev->key = TCWIN_RL_KEY_CTRL_D; return 1;
        case 'E': ev->key = TCWIN_RL_KEY_CTRL_E; return 1;
        case 'F': ev->key = TCWIN_RL_KEY_CTRL_F; return 1;
        case 'K': ev->key = TCWIN_RL_KEY_CTRL_K; return 1;
        case 'L': ev->key = TCWIN_RL_KEY_CTRL_L; return 1;
        case 'U': ev->key = TCWIN_RL_KEY_CTRL_U; return 1;
        case 'W': ev->key = TCWIN_RL_KEY_CTRL_W; return 1;
        default: return 0;      /* 其它 Ctrl 组合忽略 */
        }
    }

    switch (vk) {
    case VK_RETURN:    ev->key = TCWIN_RL_KEY_ENTER;     return 1;
    case VK_BACK:      ev->key = TCWIN_RL_KEY_BACKSPACE; return 1;
    case VK_DELETE:    ev->key = TCWIN_RL_KEY_DELETE;    return 1;
    case VK_LEFT:      ev->key = TCWIN_RL_KEY_LEFT;      return 1;
    case VK_RIGHT:     ev->key = TCWIN_RL_KEY_RIGHT;     return 1;
    case VK_UP:        ev->key = TCWIN_RL_KEY_UP;        return 1;
    case VK_DOWN:      ev->key = TCWIN_RL_KEY_DOWN;      return 1;
    case VK_HOME:      ev->key = TCWIN_RL_KEY_HOME;      return 1;
    case VK_END:       ev->key = TCWIN_RL_KEY_END;       return 1;
    case VK_ESCAPE:    ev->key = TCWIN_RL_KEY_ESCAPE;    return 1;
    case VK_TAB:       return 0;    /* 没有补全功能，忽略 */
    default: break;
    }

    if (ev->ch != 0) {
        ev->key = TCWIN_RL_KEY_CHAR;
        return 1;
    }
    return 0;
}

int tcwin_rl_readline(tcwin_rl_t *rl, char *out, int outsz) {
    HANDLE hin, hout;
    DWORD old_in_mode = 0, old_out_mode = 0;
    int in_mode_saved = 0, out_mode_saved = 0;
    int status = TCWIN_RL_CONTINUE;

    if (!rl || !out || outsz <= 0) return -1;

    hin  = GetStdHandle(STD_INPUT_HANDLE);
    hout = GetStdHandle(STD_OUTPUT_HANDLE);

    /* 输入被重定向（管道/文件）时退化到 fgets ——
     * 此时没有控制台按键可读，行编辑无意义。这是正确行为。 */
    if (hin == INVALID_HANDLE_VALUE || hout == INVALID_HANDLE_VALUE ||
        GetConsoleMode(hin, &old_in_mode) == 0) {
        if (!fgets(out, outsz, stdin)) return -1;
        {
            int n = (int)strlen(out);
            while (n > 0 && (out[n-1] == '\n' || out[n-1] == '\r')) out[--n] = 0;
            return n;
        }
    }

    /* 关掉行缓冲与回显，保留 ENABLE_PROCESSED_INPUT
     * （这样 Ctrl-C 才作为按键事件送进来，而不是直接终止进程）。 */
    SetConsoleMode(hin, (old_in_mode & ~(DWORD)(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT))
                        | ENABLE_PROCESSED_INPUT);
    in_mode_saved = 1;
    if (GetConsoleMode(hout, &old_out_mode)) {
        SetConsoleMode(hout, old_out_mode | ENABLE_PROCESSED_OUTPUT);
        out_mode_saved = 1;
    }

    tcwin_rl_clear_line(rl);
    tcwin_rl__redraw(rl, hout);

    for (;;) {
        INPUT_RECORD rec;
        DWORD nread = 0;
        tcwin_rl_event ev;

        if (!ReadConsoleInputA(hin, &rec, 1, &nread) || nread == 0) {
            status = TCWIN_RL_EOF;
            break;
        }
        if (!tcwin_rl_translate_key(&rec, &ev)) continue;

        status = tcwin_rl_feed(rl, ev);

        if (status == TCWIN_RL_SUBMIT) {
            /* 换行，把光标移到下一行 */
            DWORD w;
            WriteConsoleA(hout, "\r\n", 2, &w, NULL);
            break;
        }
        if (status == TCWIN_RL_ABORT || status == TCWIN_RL_EOF) {
            DWORD w;
            WriteConsoleA(hout, "\r\n", 2, &w, NULL);
            break;
        }
        if (status == TCWIN_RL_REDRAW) {
            /* 清屏并把光标放回左上角 */
            DWORD w;
            CONSOLE_SCREEN_BUFFER_INFO csbi;
            DWORD cells;
            COORD home;
            WriteConsoleA(hout, "\x1b[2J\x1b[H", 7, &w, NULL);
            if (GetConsoleScreenBufferInfo(hout, &csbi)) {
                cells = (DWORD)csbi.dwSize.X * (DWORD)csbi.dwSize.Y;
                home.X = 0; home.Y = 0;
                FillConsoleOutputCharacterA(hout, ' ', cells, home, &w);
            }
            status = TCWIN_RL_CONTINUE;
        }
        tcwin_rl__redraw(rl, hout);
    }

    if (in_mode_saved)  SetConsoleMode(hin, old_in_mode);
    if (out_mode_saved) SetConsoleMode(hout, old_out_mode);

    if (status == TCWIN_RL_SUBMIT) {
        int n = rl->len;
        if (n > outsz - 1) n = outsz - 1;
        memcpy(out, rl->line, (size_t)n);
        out[n] = 0;
        return n;
    }
    out[0] = 0;
    return -1;
}

void tcwin_rl_free(tcwin_rl_t *rl) {
    (void)rl;   /* 当前实现全部是内嵌缓冲，没有堆分配 */
}
