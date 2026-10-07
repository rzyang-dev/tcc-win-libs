/* tcwinreadline 行编辑器验证
 *
 * 这是 readline 能力的核心验收测试。
 *
 * 为什么能自动化：编辑器分两层，第一层（tcwin_rl_feed）是**纯状态机**，
 * 输入是按键枚举、输出是对缓冲区的修改，不做任何 I/O。
 * 所以可以完全确定性地喂按键序列、断言缓冲区与光标位置，
 * 不需要交互式终端。
 *
 * 编译：scripts\tcc.cmd examples\probes\probe_readline.c -With readline -Run
 */
#include "tccwin.h"
#include <stdio.h>
#include <string.h>

#include "tcwinreadline.h"

static int pass = 0, fail = 0;
#define CK(d, c) do { if (c) { printf("  [OK]   %s\n", d); pass++; } \
                      else   { printf("  [FAIL] %s\n", d); fail++; } } while (0)

static tcwin_rl_t rl;

/* 喂一个完整 UTF-8 序列：首字节进 ch，其余进 u8_tail。
 *
 * 这里刻意用**函数**而不是宏。曾经的宏版本在 TCC 上出过问题：
 * 用两字符的 UTF-8 字面量调用时，宏体里的三元赋值
 * `_e.u8_tail_len = (_n > 1) ? (_n - 1) : 0;` 没被正确执行，
 * 结果是垃圾值 34710（实测）。单字符调用则正常 —— 说明是 TCC 的
 * 宏解析缺陷，不是逻辑错。见 PITFALLS P64。 */
static void utf8in(const char *s) {
    tcwin_rl_event e;
    int n = (int)strlen(s), k, tail;

    e.key = TCWIN_RL_KEY_CHAR;
    e.ch  = s[0];
    e.u8_tail_len = 0;
    if (n <= 1) {
        tcwin_rl_feed(&rl, e);
        return;
    }

    /* ⚠️ u8_tail 只有 3 字节（UTF-8 序列最长 4 字节 = ch + 3）。
     * 这里**必须钳位**，否则会把 s 里多余的字节写进紧邻的 u8_tail_len
     * 字段，把它覆盖成垃圾值。
     *
     * 实测踩到：不钳位时喂 6 字节的 "中文"，u8_tail_len 变成 34710,
     * 插入长度只有 3 而不是 6。单字符（3 字节）调用不会溢出，
     * 所以现象是"有的用例过、有的不过"，很容易误判成编辑器有 bug。
     * 见 PITFALLS P64。 */
    tail = n - 1;
    if (tail > (int)sizeof e.u8_tail) tail = (int)sizeof e.u8_tail;
    e.u8_tail_len = tail;
    for (k = 0; k < tail; k++) e.u8_tail[k] = s[1 + k];

    tcwin_rl_feed(&rl, e);
}

/* 便捷：发一个字符键 */
static int ch(char c) {
    tcwin_rl_event e;
    e.key = TCWIN_RL_KEY_CHAR;
    e.ch = c;
    return tcwin_rl_feed(&rl, e);
}
static int key(int k) {
    tcwin_rl_event e;
    e.key = k;
    e.ch = 0;
    return tcwin_rl_feed(&rl, e);
}
/* 输入一个字符串 */
static void text(const char *s) { while (*s) ch(*s++); }

/* 断言当前行内容与光标 */
static void expect_line(const char *want, int want_cursor, const char *desc) {
    const char *got = tcwin_rl_line(&rl);
    int gc = tcwin_rl_cursor(&rl);
    if (strcmp(got, want) == 0 && gc == want_cursor) {
        printf("  [OK]   %-42s \"%s\" 光标=%d\n", desc, got, gc);
        pass++;
    } else {
        printf("  [FAIL] %-42s 得到 \"%s\" 光标=%d，期望 \"%s\" 光标=%d\n",
               desc, got, gc, want, want_cursor);
        fail++;
    }
}

int main(void) {
    /* 让控制台按 UTF-8 解释输出，否则中文在 GBK(936) 控制台上乱码。
     * 见 docs/PITFALLS.md P59。 */
    SetConsoleOutputCP(CP_UTF8);    printf("=== tcwinreadline 行编辑器验证 ===\n\n");

    /* ---------- 基本插入 ---------- */
    printf("-- 基本输入 --\n");
    tcwin_rl_init(&rl, "> ");
    expect_line("", 0, "初始为空");
    text("hello");
    expect_line("hello", 5, "输入 hello");
    ch(' ');
    text("world");
    expect_line("hello world", 11, "输入 hello world");
    printf("\n");

    /* ---------- 光标移动 ---------- */
    printf("-- 光标移动 --\n");
    tcwin_rl_clear_line(&rl);
    text("abcdef");
    expect_line("abcdef", 6, "输入 abcdef");
    key(TCWIN_RL_KEY_LEFT);
    expect_line("abcdef", 5, "← 一次");
    key(TCWIN_RL_KEY_LEFT);
    key(TCWIN_RL_KEY_LEFT);
    expect_line("abcdef", 3, "← 三次");
    key(TCWIN_RL_KEY_RIGHT);
    expect_line("abcdef", 4, "→ 一次");
    key(TCWIN_RL_KEY_HOME);
    expect_line("abcdef", 0, "Home");
    key(TCWIN_RL_KEY_END);
    expect_line("abcdef", 6, "End");

    /* 边界：光标不能越界 */
    key(TCWIN_RL_KEY_LEFT); key(TCWIN_RL_KEY_LEFT); key(TCWIN_RL_KEY_LEFT);
    key(TCWIN_RL_KEY_LEFT); key(TCWIN_RL_KEY_LEFT); key(TCWIN_RL_KEY_LEFT);
    key(TCWIN_RL_KEY_LEFT); key(TCWIN_RL_KEY_LEFT);
    expect_line("abcdef", 0, "← 超出左边界后停在 0");
    for (int i = 0; i < 10; i++) key(TCWIN_RL_KEY_RIGHT);
    expect_line("abcdef", 6, "→ 超出右边界后停在 len");
    printf("\n");

    /* ---------- emacs 风格组合键 ---------- */
    printf("-- emacs 风格 Ctrl 组合 --\n");
    tcwin_rl_clear_line(&rl);
    text("abcdef");
    key(TCWIN_RL_KEY_CTRL_A);
    expect_line("abcdef", 0, "Ctrl-A 行首");
    key(TCWIN_RL_KEY_CTRL_E);
    expect_line("abcdef", 6, "Ctrl-E 行尾");
    key(TCWIN_RL_KEY_CTRL_B);
    expect_line("abcdef", 5, "Ctrl-B 左移");
    key(TCWIN_RL_KEY_CTRL_F);
    expect_line("abcdef", 6, "Ctrl-F 右移");
    printf("\n");

    /* ---------- 编辑：插入到中间 ---------- */
    printf("-- 中间插入 --\n");
    tcwin_rl_clear_line(&rl);
    text("ac");
    key(TCWIN_RL_KEY_LEFT);
    ch('b');
    expect_line("abc", 2, "在 a 与 c 之间插入 b");
    key(TCWIN_RL_KEY_END);
    ch('d');
    expect_line("abcd", 4, "行尾追加 d");
    key(TCWIN_RL_KEY_HOME);
    ch('X');
    expect_line("Xabcd", 1, "行首插入 X");
    printf("\n");

    /* ---------- 编辑：删除 ---------- */
    printf("-- 删除 --\n");
    tcwin_rl_clear_line(&rl);
    text("abcd");
    key(TCWIN_RL_KEY_BACKSPACE);
    expect_line("abc", 3, "Backspace 删除行尾");
    key(TCWIN_RL_KEY_HOME);
    key(TCWIN_RL_KEY_DELETE);
    expect_line("bc", 0, "行首 Delete 删除首字符");
    key(TCWIN_RL_KEY_END);
    key(TCWIN_RL_KEY_LEFT);
    key(TCWIN_RL_KEY_BACKSPACE);
    expect_line("c", 0, "中间 Backspace");

    /* 边界 */
    key(TCWIN_RL_KEY_BACKSPACE);
    expect_line("c", 0, "光标在行首时 Backspace 无效果");
    key(TCWIN_RL_KEY_END);
    key(TCWIN_RL_KEY_DELETE);
    expect_line("c", 1, "光标在行尾时 Delete 无效果");
    printf("\n");

    /* ---------- Ctrl-U / Ctrl-K / Ctrl-W ---------- */
    printf("-- Ctrl-U / Ctrl-K / Ctrl-W --\n");
    tcwin_rl_clear_line(&rl);
    text("hello world foo");
    key(TCWIN_RL_KEY_CTRL_W);
    expect_line("hello world ", 12, "Ctrl-W 删掉 foo");
    key(TCWIN_RL_KEY_CTRL_W);
    expect_line("hello ", 6, "Ctrl-W 再删一词（连同空格）");
    key(TCWIN_RL_KEY_CTRL_U);
    expect_line("", 0, "Ctrl-U 删掉左侧全部");

    tcwin_rl_clear_line(&rl);
    text("abcdef");
    key(TCWIN_RL_KEY_HOME);
    key(TCWIN_RL_KEY_RIGHT); key(TCWIN_RL_KEY_RIGHT);
    key(TCWIN_RL_KEY_CTRL_K);
    expect_line("ab", 2, "Ctrl-K 删掉右侧全部");
    key(TCWIN_RL_KEY_CTRL_U);
    expect_line("", 0, "Ctrl-K 后 Ctrl-U 清空");
    printf("\n");

    /* ---------- Ctrl-D ---------- */
    printf("-- Ctrl-D --\n");
    tcwin_rl_clear_line(&rl);
    CK("空行时 Ctrl-D 返回 EOF", key(TCWIN_RL_KEY_CTRL_D) == TCWIN_RL_EOF);
    tcwin_rl_clear_line(&rl);
    text("ab");
    key(TCWIN_RL_KEY_HOME);
    CK("非空行时 Ctrl-D 不返回 EOF", key(TCWIN_RL_KEY_CTRL_D) != TCWIN_RL_EOF);
    expect_line("b", 0, "非空行 Ctrl-D 删除光标处字符");
    printf("\n");

    /* ---------- Enter / Ctrl-C / Ctrl-L ---------- */
    printf("-- Enter / Ctrl-C / Ctrl-L --\n");
    tcwin_rl_clear_line(&rl);
    text("submit me");
    CK("Enter 返回 SUBMIT", key(TCWIN_RL_KEY_ENTER) == TCWIN_RL_SUBMIT);
    CK("SUBMIT 后行内容仍在（调用方负责取用）",
       strcmp(tcwin_rl_line(&rl), "submit me") == 0);
    CK("Ctrl-C 返回 ABORT", key(TCWIN_RL_KEY_CTRL_C) == TCWIN_RL_ABORT);
    CK("Ctrl-L 返回 REDRAW", key(TCWIN_RL_KEY_CTRL_L) == TCWIN_RL_REDRAW);
    CK("Ctrl-L 不改变行内容", strcmp(tcwin_rl_line(&rl), "submit me") == 0);
    printf("\n");

    /* ---------- 历史 ---------- */
    printf("-- 历史 --\n");
    tcwin_rl_init(&rl, "> ");
    tcwin_rl_history_add(&rl, "first");
    tcwin_rl_history_add(&rl, "second");
    tcwin_rl_history_add(&rl, "third");
    CK("历史条数 = 3", tcwin_rl_history_count(&rl) == 3);
    CK("history_get(0) 是最新的", strcmp(tcwin_rl_history_get(&rl, 0), "third") == 0);
    CK("history_get(1)", strcmp(tcwin_rl_history_get(&rl, 1), "second") == 0);
    CK("history_get(2) 是最老的", strcmp(tcwin_rl_history_get(&rl, 2), "first") == 0);
    CK("history_get(3) 越界返回 NULL", tcwin_rl_history_get(&rl, 3) == NULL);
    CK("history_get(-1) 越界返回 NULL", tcwin_rl_history_get(&rl, -1) == NULL);

    /* 空行不入历史 */
    tcwin_rl_history_add(&rl, "");
    CK("空串不入历史", tcwin_rl_history_count(&rl) == 3);

    /* 相邻重复不入历史 */
    tcwin_rl_history_add(&rl, "third");
    CK("相邻重复不入历史", tcwin_rl_history_count(&rl) == 3);
    tcwin_rl_history_add(&rl, "fourth");
    CK("不同内容入历史", tcwin_rl_history_count(&rl) == 4);
    printf("\n");

    /* ---------- 历史浏览 ---------- */
    printf("-- 历史浏览（↑/↓） --\n");
    tcwin_rl_init(&rl, "> ");
    tcwin_rl_history_add(&rl, "cmd1");
    tcwin_rl_history_add(&rl, "cmd2");
    tcwin_rl_history_add(&rl, "cmd3");

    /* 先在编辑区打点字，验证浏览退出后能恢复 */
    text("draft");
    expect_line("draft", 5, "浏览前先输入 draft");
    key(TCWIN_RL_KEY_UP);
    expect_line("cmd3", 4, "↑ 取最新一条 cmd3");
    key(TCWIN_RL_KEY_UP);
    expect_line("cmd2", 4, "↑ 再取 cmd2");
    key(TCWIN_RL_KEY_UP);
    expect_line("cmd1", 4, "↑ 取最老的 cmd1");
    key(TCWIN_RL_KEY_UP);
    expect_line("cmd1", 4, "已在最老一条，↑ 不再变化");
    key(TCWIN_RL_KEY_DOWN);
    expect_line("cmd2", 4, "↓ 回到 cmd2");
    key(TCWIN_RL_KEY_DOWN);
    expect_line("cmd3", 4, "↓ 回到 cmd3");
    key(TCWIN_RL_KEY_DOWN);
    expect_line("draft", 5, "↓ 越过最新一条后恢复原来的 draft");
    key(TCWIN_RL_KEY_DOWN);
    expect_line("draft", 5, "不在浏览中时 ↓ 无效果");
    printf("\n");

    /* ---------- 历史浏览中编辑 ---------- */
    printf("-- 历史浏览中编辑后提交 --\n");
    tcwin_rl_init(&rl, "> ");
    tcwin_rl_history_add(&rl, "git status");
    key(TCWIN_RL_KEY_UP);
    expect_line("git status", 10, "↑ 取到 git status");
    ch(' ');
    text("-s");
    expect_line("git status -s", 13, "在历史条目上继续编辑");
    printf("\n");

    /* ---------- 空历史时浏览 ---------- */
    printf("-- 边界：空历史 --\n");
    tcwin_rl_init(&rl, "> ");
    text("abc");
    key(TCWIN_RL_KEY_UP);
    expect_line("abc", 3, "空历史时 ↑ 不影响当前编辑");
    key(TCWIN_RL_KEY_DOWN);
    expect_line("abc", 3, "空历史时 ↓ 不影响当前编辑");
    printf("\n");

    /* ---------- 缓冲上限 ---------- */
    printf("-- 边界：缓冲上限 --\n");
    tcwin_rl_init(&rl, "> ");
    for (int i = 0; i < TCWIN_RL_LINE_MAX + 200; i++) ch('x');
    printf("       输入 %d 个字符后长度 = %d（上限 %d）\n",
           TCWIN_RL_LINE_MAX + 200, tcwin_rl_length(&rl), TCWIN_RL_LINE_MAX - 1);
    CK("超出上限后不越界（长度恰为上限-1）",
       tcwin_rl_length(&rl) == TCWIN_RL_LINE_MAX - 1);
    CK("光标不越过长度", tcwin_rl_cursor(&rl) <= tcwin_rl_length(&rl));
    CK("缓冲区以 NUL 结尾",
       tcwin_rl_line(&rl)[tcwin_rl_length(&rl)] == 0);

    /* 满了之后仍能编辑 */
    key(TCWIN_RL_KEY_BACKSPACE);
    CK("满缓冲仍可退格", tcwin_rl_length(&rl) == TCWIN_RL_LINE_MAX - 2);
    printf("\n");

    /* ---------- 历史环形缓冲 ---------- */
    printf("-- 边界：历史环形缓冲 --\n");
    tcwin_rl_init(&rl, "> ");
    {
        char tmp[32];
        int total = TCWIN_RL_HISTORY_MAX + 20;
        for (int i = 0; i < total; i++) {
            snprintf(tmp, sizeof tmp, "cmd%d", i);
            tcwin_rl_history_add(&rl, tmp);
        }
        printf("       写入 %d 条，历史保留 %d 条（上限 %d）\n",
               total, tcwin_rl_history_count(&rl), TCWIN_RL_HISTORY_MAX);
        CK("历史不超过上限", tcwin_rl_history_count(&rl) == TCWIN_RL_HISTORY_MAX);
        /* 最新的应是最后写入的那条 */
        snprintf(tmp, sizeof tmp, "cmd%d", total - 1);
        CK("最新条目正确", strcmp(tcwin_rl_history_get(&rl, 0), tmp) == 0);
        /* 最老的应是 total - HISTORY_MAX */
        snprintf(tmp, sizeof tmp, "cmd%d", total - TCWIN_RL_HISTORY_MAX);
        CK("最老条目为被挤出边界后的第一条",
           strcmp(tcwin_rl_history_get(&rl, TCWIN_RL_HISTORY_MAX - 1), tmp) == 0);
    }
    printf("\n");

    /* ---------- clear_line ---------- */
    printf("-- clear_line --\n");
    tcwin_rl_init(&rl, "> ");
    text("junk");
    tcwin_rl_history_add(&rl, "kept");
    tcwin_rl_clear_line(&rl);
    expect_line("", 0, "clear_line 清空内容与光标");
    CK("clear_line 不丢历史", tcwin_rl_history_count(&rl) == 1);
    CK("clear_line 后历史仍可读",
       strcmp(tcwin_rl_history_get(&rl, 0), "kept") == 0);
    printf("\n");

    /* ---------- 非打印字符 ---------- */
    printf("-- 边界：控制字符不被插入 --\n");
    tcwin_rl_init(&rl, "> ");
    {
        tcwin_rl_event e;
        e.key = TCWIN_RL_KEY_CHAR; e.ch = 0x01;   /* Ctrl-A 的 ASCII 码 */
        tcwin_rl_feed(&rl, e);
        e.ch = 0x7F;                            /* DEL */
        tcwin_rl_feed(&rl, e);
        e.ch = 0x09;                            /* Tab */
        tcwin_rl_feed(&rl, e);
    }
    expect_line("", 0, "控制字符（<0x20 与 0x7F）不被插入");
    printf("\n");


    /* ============================================================
     * 第二层：控制台按键翻译
     *
     * tcwin_rl_translate_key 是控制台层唯一有分支逻辑的部分。
     * 它把 Win32 的 INPUT_RECORD 翻译成按键事件，涉及：
     *   - 虚拟键码 → 按键枚举的映射
     *   - Ctrl 组合键的判定
     *   - 按键抬起/非按键记录的过滤
     * 这些分支不测就不知道对不对。
     * ============================================================ */
    printf("-- 控制台层：按键翻译 --\n");
    {
        INPUT_RECORD rec;
        tcwin_rl_event ev;
        int r;

        /* 辅助宏：构造一个按键按下记录 */
        #define MAKEKEY(vk, ascii, ctrlstate) do {                    \
            memset(&rec, 0, sizeof rec);                               \
            rec.EventType = KEY_EVENT;                                 \
            rec.Event.KeyEvent.bKeyDown = TRUE;                        \
            rec.Event.KeyEvent.wVirtualKeyCode = (WORD)(vk);           \
            rec.Event.KeyEvent.uChar.AsciiChar = (char)(ascii);        \
            rec.Event.KeyEvent.dwControlKeyState = (DWORD)(ctrlstate); \
        } while (0)

        /* 可打印字符 */
        MAKEKEY('a', 'a', 0);
        r = tcwin_rl_translate_key(&rec, &ev);
        CK("'a' → CHAR('a')", r && ev.key == TCWIN_RL_KEY_CHAR && ev.ch == 'a');

        MAKEKEY(VK_SPACE, ' ', 0);
        r = tcwin_rl_translate_key(&rec, &ev);
        CK("空格 → CHAR(' ')", r && ev.key == TCWIN_RL_KEY_CHAR && ev.ch == ' ');

        /* 方向键（AsciiChar 为 0） */
        MAKEKEY(VK_LEFT, 0, 0);
        r = tcwin_rl_translate_key(&rec, &ev);
        CK("VK_LEFT → LEFT", r && ev.key == TCWIN_RL_KEY_LEFT);

        MAKEKEY(VK_RIGHT, 0, 0);
        r = tcwin_rl_translate_key(&rec, &ev);
        CK("VK_RIGHT → RIGHT", r && ev.key == TCWIN_RL_KEY_RIGHT);

        MAKEKEY(VK_UP, 0, 0);
        r = tcwin_rl_translate_key(&rec, &ev);
        CK("VK_UP → UP", r && ev.key == TCWIN_RL_KEY_UP);

        MAKEKEY(VK_DOWN, 0, 0);
        r = tcwin_rl_translate_key(&rec, &ev);
        CK("VK_DOWN → DOWN", r && ev.key == TCWIN_RL_KEY_DOWN);

        MAKEKEY(VK_HOME, 0, 0);
        r = tcwin_rl_translate_key(&rec, &ev);
        CK("VK_HOME → HOME", r && ev.key == TCWIN_RL_KEY_HOME);

        MAKEKEY(VK_END, 0, 0);
        r = tcwin_rl_translate_key(&rec, &ev);
        CK("VK_END → END", r && ev.key == TCWIN_RL_KEY_END);

        MAKEKEY(VK_DELETE, 0, 0);
        r = tcwin_rl_translate_key(&rec, &ev);
        CK("VK_DELETE → DELETE", r && ev.key == TCWIN_RL_KEY_DELETE);

        MAKEKEY(VK_BACK, 8, 0);
        r = tcwin_rl_translate_key(&rec, &ev);
        CK("VK_BACK → BACKSPACE（不当作 CHAR 0x08）",
           r && ev.key == TCWIN_RL_KEY_BACKSPACE);

        MAKEKEY(VK_RETURN, 13, 0);
        r = tcwin_rl_translate_key(&rec, &ev);
        CK("VK_RETURN → ENTER", r && ev.key == TCWIN_RL_KEY_ENTER);

        MAKEKEY(VK_ESCAPE, 27, 0);
        r = tcwin_rl_translate_key(&rec, &ev);
        CK("VK_ESCAPE → ESCAPE", r && ev.key == TCWIN_RL_KEY_ESCAPE);

        /* Ctrl 组合键 */
        MAKEKEY('A', 1, LEFT_CTRL_PRESSED);
        r = tcwin_rl_translate_key(&rec, &ev);
        CK("Ctrl-A → CTRL_A（优先级高于 CHAR）",
           r && ev.key == TCWIN_RL_KEY_CTRL_A);

        MAKEKEY('C', 3, LEFT_CTRL_PRESSED);
        r = tcwin_rl_translate_key(&rec, &ev);
        CK("Ctrl-C → CTRL_C", r && ev.key == TCWIN_RL_KEY_CTRL_C);

        MAKEKEY('D', 4, RIGHT_CTRL_PRESSED);
        r = tcwin_rl_translate_key(&rec, &ev);
        CK("右 Ctrl-D → CTRL_D（右 Ctrl 也认）",
           r && ev.key == TCWIN_RL_KEY_CTRL_D);

        MAKEKEY('W', 23, LEFT_CTRL_PRESSED);
        r = tcwin_rl_translate_key(&rec, &ev);
        CK("Ctrl-W → CTRL_W", r && ev.key == TCWIN_RL_KEY_CTRL_W);

        MAKEKEY('L', 12, LEFT_CTRL_PRESSED);
        r = tcwin_rl_translate_key(&rec, &ev);
        CK("Ctrl-L → CTRL_L", r && ev.key == TCWIN_RL_KEY_CTRL_L);

        /* 按键抬起不产生事件 */
        MAKEKEY('x', 'x', 0);
        rec.Event.KeyEvent.bKeyDown = FALSE;
        r = tcwin_rl_translate_key(&rec, &ev);
        CK("按键抬起记录被忽略", r == 0);

        /* 非按键记录被忽略 */
        memset(&rec, 0, sizeof rec);
        rec.EventType = WINDOW_BUFFER_SIZE_EVENT;
        r = tcwin_rl_translate_key(&rec, &ev);
        CK("窗口尺寸变化记录被忽略", r == 0);

        /* Tab 无补全功能，忽略 */
        MAKEKEY(VK_TAB, 9, 0);
        r = tcwin_rl_translate_key(&rec, &ev);
        CK("Tab 被忽略（无补全功能）", r == 0);

        /* 未知 Ctrl 组合忽略 */
        MAKEKEY('Z', 26, LEFT_CTRL_PRESSED);
        r = tcwin_rl_translate_key(&rec, &ev);
        CK("未绑定的 Ctrl 组合被忽略", r == 0);

        /* 无字符且非功能键 → 忽略（例如单独的 Shift 按下） */
        MAKEKEY(VK_SHIFT, 0, SHIFT_PRESSED);
        r = tcwin_rl_translate_key(&rec, &ev);
        CK("单独 Shift 被忽略", r == 0);

        #undef MAKEKEY
    }
    printf("\n");

    /* ---------- 端到端：翻译 + 编辑串起来 ---------- */
    printf("-- 端到端：翻译 → 编辑 --\n");
    {
        INPUT_RECORD rec;
        tcwin_rl_event ev;
        tcwin_rl_init(&rl, "$ ");
        #define SENDKEY(vk, ascii, cs) do {                            \
            memset(&rec, 0, sizeof rec);                               \
            rec.EventType = KEY_EVENT;                                 \
            rec.Event.KeyEvent.bKeyDown = TRUE;                        \
            rec.Event.KeyEvent.wVirtualKeyCode = (WORD)(vk);           \
            rec.Event.KeyEvent.uChar.AsciiChar = (char)(ascii);        \
            rec.Event.KeyEvent.dwControlKeyState = (DWORD)(cs);        \
            if (tcwin_rl_translate_key(&rec, &ev)) tcwin_rl_feed(&rl, ev); \
        } while (0)

        SENDKEY('h','h',0); SENDKEY('i','i',0);
        SENDKEY(VK_HOME,0,0);
        SENDKEY('X','X',0);
        expect_line("Xhi", 1, "翻译+编辑：输入 hi、Home、插入 X");

        tcwin_rl_history_add(&rl, "prev-cmd");
        SENDKEY(VK_UP,0,0);
        expect_line("prev-cmd", 8, "翻译+编辑：↑ 取历史");
        SENDKEY(VK_END,0,0);
        SENDKEY(VK_RETURN,13,0);
        CK("翻译+编辑：Enter 已提交（行内容保持）",
           strcmp(tcwin_rl_line(&rl), "prev-cmd") == 0);

        /* --- UTF-8 多字节输入（2025 轮新增）----------------------------
         * 原来完全按字节处理：退格只删一个字节，留下半个字符；
         * 光标按字节数定位，与屏幕列数不符。
         *
         * 这里测的是**行为**（编辑器状态），纯逻辑的逐函数断言
         * 在 probe_rl_utf8.c 里（98 项）。
         * ------------------------------------------------------------ */
        {

            tcwin_rl_init(&rl, "> ");

            /* 1. 输入 "a中b"（字节数 1+3+1=5，码点数 3） */
            ch('a');
            utf8in("\xE4\xB8\xAD");       /* 中 */
            ch('b');
            expect_line("a\xE4\xB8\xAD" "b", 5, "UTF-8：输入 a中b 后光标在字节 5");
            CK("UTF-8：长度按字节算 == 5", tcwin_rl_length(&rl) == 5);

            /* 2. 左移应跨过整个 'b' 到字节 4 */
            key(TCWIN_RL_KEY_LEFT);
            CK("UTF-8：左移一次到字节 4（在 中 之后）", tcwin_rl_cursor(&rl) == 4);

            /* 3. 再左移应跨过 3 字节的 "中" 到字节 1 —— 只移一个码点 */
            key(TCWIN_RL_KEY_LEFT);
            CK("UTF-8：再左移跨过 3 字节的中，到字节 1", tcwin_rl_cursor(&rl) == 1);

            /* 4. 右移应跨回 3 字节到字节 4 */
            key(TCWIN_RL_KEY_RIGHT);
            CK("UTF-8：右移跨过中，到字节 4", tcwin_rl_cursor(&rl) == 4);

            /* 5. 在 "中" 之前（字节 1）按 Delete：应删掉整个 3 字节序列 */
            key(TCWIN_RL_KEY_HOME);
            key(TCWIN_RL_KEY_RIGHT);        /* 移到字节 1，中 之前 */
            CK("UTF-8：移到字节 1", tcwin_rl_cursor(&rl) == 1);
            key(TCWIN_RL_KEY_DELETE);
            expect_line("ab", 1, "UTF-8：Delete 删掉整个 3 字节的中");

            /* 6. 退格：从末尾退，应删掉整个码点 */
            tcwin_rl_init(&rl, "> ");
            /* 一次按键事件只送**一个字符** —— 真实控制台就是每个字符
             * 一次 KEY_EVENT。所以这里分两次喂，不要一次塞 6 字节。 */
            utf8in("\xE4\xB8\xAD");                /* 中 */
            utf8in("\xE6\x96\x87");                /* 文 */
            CK("UTF-8：中文 6 字节", tcwin_rl_length(&rl) == 6);
            key(TCWIN_RL_KEY_BACKSPACE);
            expect_line("\xE4\xB8\xAD", 3, "UTF-8：退格删掉整个 文（3 字节）");
            key(TCWIN_RL_KEY_BACKSPACE);
            expect_line("", 0, "UTF-8：再退格删掉整个 中，行为空");

            /* 7. 退格不该多删：在 ASCII 后面退格只删 1 字节 */
            tcwin_rl_init(&rl, "> ");
            text("abc");
            key(TCWIN_RL_KEY_BACKSPACE);
            expect_line("ab", 2, "UTF-8：ASCII 退格仍删 1 字节");

            /* 8. 在中间插入多字节：光标之前的字节数不变 */
            tcwin_rl_init(&rl, "> ");
            ch('x'); ch('y');
            key(TCWIN_RL_KEY_LEFT);          /* 光标在字节 1 */
            utf8in("\xE4\xB8\xAD");        /* 在 x 与 y 之间插 中 */
            expect_line("x\xE4\xB8\xAD" "y", 4, "UTF-8：中间插入中，光标到字节 4");

            /* 9. 4 字节 emoji 序列（U+1F600，F0 9F 98 80） */
            tcwin_rl_init(&rl, "> ");
            utf8in("\xF0\x9F\x98\x80");
            CK("UTF-8：4 字节 emoji 整段插入（长度 4）", tcwin_rl_length(&rl) == 4);
            CK("UTF-8：emoji 后光标在字节 4", tcwin_rl_cursor(&rl) == 4);
            key(TCWIN_RL_KEY_BACKSPACE);
            expect_line("", 0, "UTF-8：退格一次删掉整个 4 字节 emoji");

            /* 10. 旧路径不受影响：只设 ch 不设 u8_tail，行为与以前一致 */
            tcwin_rl_init(&rl, "> ");
            {
                tcwin_rl_event _e;
                _e.key = TCWIN_RL_KEY_CHAR;
                _e.ch = 'A';
                _e.u8_tail_len = 0;      /* 关键：不设 tail */
                tcwin_rl_feed(&rl, _e);
            }
            expect_line("A", 1, "UTF-8：只设 ch 的旧路径仍工作（向后兼容）");

            /* 11. tail 数据不全时退化为只插首字节（不丢字符、不越界） */
            tcwin_rl_init(&rl, "> ");
            {
                tcwin_rl_event _e;
                _e.key = TCWIN_RL_KEY_CHAR;
                _e.ch = (char)0xE4;      /* 中的首字节，但会说需要 3 字节 */
                _e.u8_tail_len = 1;      /* 只给 1 个 tail，不够 */
                _e.u8_tail[0] = (char)0xB8;
                tcwin_rl_feed(&rl, _e);
            }
            CK("UTF-8：tail 不全时只插首字节，不越界", tcwin_rl_length(&rl) == 1);

            /* 12. 容量边界：接近满时多字节整段丢弃，不留下半个字符 */
            tcwin_rl_init(&rl, "> ");
            {
                int _k;
                /* 填到只剩 2 字节空间（TCWIN_RL_LINE_MAX-1 是可写上限） */
                for (_k = 0; _k < TCWIN_RL_LINE_MAX - 3; _k++) ch('z');
                CK("UTF-8：填满到上限-2", tcwin_rl_length(&rl) == TCWIN_RL_LINE_MAX - 3);
                utf8in("\xE4\xB8\xAD");   /* 需要 3 字节，只剩 2 -> 整段丢弃 */
                CK("UTF-8：装不下时整段丢弃（长度不变）",
                   tcwin_rl_length(&rl) == TCWIN_RL_LINE_MAX - 3);
            }

        }

        #undef SENDKEY
    }
    printf("\n");    printf("=== %d 通过, %d 失败 ===\n", pass, fail);
    return fail ? 1 : 0;
}
