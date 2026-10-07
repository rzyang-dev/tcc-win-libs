/* tcwin_rl_utf8.h - 行编辑器的 UTF-8 / 显示宽度纯逻辑
 *
 * ============================================================
 * 为什么单独一个头
 * ============================================================
 *
 * `tcwinreadline` 原来**完全按字节**处理输入：插入/退格/删除/光标移动
 * 都是 `line[cursor]` 的单字节操作。遇到 UTF-8 多字节字符时：
 *
 *   - 退格只删掉一个字节，留下半个字符（渲染出乱码）
 *   - 光标按字节数定位，而屏幕上中文占 2 列、emoji 占 2 列，
 *     于是光标位置与视觉位置不符
 *
 * 修法是引入"码点"这一层。这一层是**纯逻辑**（不碰控制台、不碰 I/O），
 * 所以单独放一个头，让 `examples/probes/probe_readline.c` 能直接包含它
 * 并逐个断言 —— 这与本环境其它地方的"纯逻辑核心 + 薄 I/O 驱动"分层一致。
 *
 * ============================================================
 * 纯头文件，无外部依赖
 * ============================================================
 *
 * 只用 <stddef.h>，不依赖 tccwin.h / windows.h，可以在任何 C89 编译器上编译。
 *
 * 函数名前缀 `tcwin_u8_`，都是 `static`，避免多编译单元重复定义。
 */

#ifndef TCWIN_RL_UTF8_H
#define TCWIN_RL_UTF8_H

#include <stddef.h>

/* 一个 UTF-8 序列最多 4 字节（RFC 3629；5/6 字节的旧形式不接受） */
#define TCWIN_U8_MAX 4

/* 判断字节是否为 UTF-8 续字节（10xxxxxx） */
static int tcwin_u8_is_cont(unsigned char c) {
    return (c & 0xC0) == 0x80;
}

/* 从 s 开头的序列**应该**有多长（按首字节的位模式判断）。
 *
 * 注意这是"首字节声称的长度"，不做合法性校验 —— 校验见 tcwin_u8_seqlens。
 * 非法首字节（续字节或 0xF8+）返回 1，让调用方按单字节前进，
 * 不至于死循环。 */
static int tcwin_u8_seqlen(unsigned char c) {
    if (c < 0x80)        return 1;   /* 0xxxxxxx   ASCII */
    if ((c & 0xE0) == 0xC0) return 2;   /* 110xxxxx   2 字节 */
    if ((c & 0xF0) == 0xE0) return 3;   /* 1110xxxx   3 字节 */
    if ((c & 0xF8) == 0xF0) return 4;   /* 11110xxx   4 字节 */
    return 1;                        /* 续字节 / 0xF8+：按单字节处理 */
}

/* 从 s 开头取一个码点。
 *
 * 返回值：码点值（>= 0）；若序列不完整或非法，返回 -1，
 * 并把 *adv 设为 1（按单字节前进，避免卡死）。
 * *adv 总是被设置成"应该前进多少字节"。
 *
 * 只做结构性校验（续字节个数与位模式），不做超长编码 / 代理区 / 上限校验 ——
 * 行编辑器不需要那么严格，且不严格更不容易在畸形输入上行为怪异。 */
static int tcwin_u8_decode(const char *s, int *adv) {
    unsigned char c;
    int need, i;
    int cp;

    if (adv) *adv = 1;
    if (!s || !s[0]) return -1;

    c = (unsigned char)s[0];
    need = tcwin_u8_seqlen(c);

    if (need == 1) {
        if (c < 0x80) return (int)c;
        return -1;                       /* 非法单字节 */
    }

    /* 检查后续字节是否都是续字节且非 NUL */
    for (i = 1; i < need; i++) {
        if (!s[i] || !tcwin_u8_is_cont((unsigned char)s[i])) return -1;
    }

    /* 组装码点 */
    if (need == 2) {
        cp = ((int)(c & 0x1F) << 6) | ((int)((unsigned char)s[1] & 0x3F));
        /* 拒绝超长编码（C0/C1 开头） */
        if (cp < 0x80) return -1;
    } else if (need == 3) {
        cp = ((int)(c & 0x0F) << 12)
           | ((int)((unsigned char)s[1] & 0x3F) << 6)
           | ((int)((unsigned char)s[2] & 0x3F));
        if (cp < 0x800) return -1;
        /* 拒绝 UTF-16 代理区（U+D800..U+DFFF），它们不是合法 UTF-8 */
        if (cp >= 0xD800 && cp <= 0xDFFF) return -1;
    } else {
        cp = ((int)(c & 0x07) << 18)
           | ((int)((unsigned char)s[1] & 0x3F) << 12)
           | ((int)((unsigned char)s[2] & 0x3F) << 6)
           | ((int)((unsigned char)s[3] & 0x3F));
        if (cp < 0x10000 || cp > 0x10FFFF) return -1;
    }

    if (adv) *adv = need;
    return cp;
}

/* 从 buf 的字节位置 `pos` 向前退**一个码点**，返回新的字节位置。
 *
 * 语义：用于"光标左移"与"退格"。如果 pos 落在某个多字节序列中间
 * （理论上不该发生，但畸形输入下可能），会一直退到序列首字节。
 *
 * pos <= 0 时返回 0。 */
static int tcwin_u8_prev(const char *buf, int pos) {
    int p;
    if (pos <= 0) return 0;
    p = pos - 1;
    /* 最多退 3 个字节就能到首字节（4 字节序列的第 4 字节往前数 3 个） */
    while (p > 0 && tcwin_u8_is_cont((unsigned char)buf[p])) p--;
    return p;
}

/* 从 buf 的字节位置 `pos` 向前进**一个码点**，返回新的字节位置。
 *
 * 语义：用于"光标右移"。到 NUL 就停在 NUL 处（不越界）。
 * len 是缓冲区的有效字节长度（不含 NUL）。 */
static int tcwin_u8_next(const char *buf, int pos, int len) {
    int adv;
    if (pos >= len) return len;
    tcwin_u8_decode(buf + pos, &adv);
    if (adv < 1) adv = 1;
    if (pos + adv > len) return len;
    return pos + adv;
}

/* 统计 buf 前 `bytes` 个字节里有多少个码点。
 * 畸形字节按 1 个码点计。 */
static int tcwin_u8_count(const char *buf, int bytes) {
    int i = 0, n = 0, adv;
    while (i < bytes) {
        tcwin_u8_decode(buf + i, &adv);
        if (adv < 1) adv = 1;
        i += adv;
        n++;
    }
    return n;
}

/* 码点的显示宽度（终端列数）。
 *
 * 简化规则，覆盖实际会遇到的绝大多数情况：
 *   0        -> 0   （组合记号等，本实现不处理，按 0 会算错宽度，
 *                     所以下面把已知的零宽区间单独列出后按 0 处理）
 *   1        -> 其余
 *   2        -> 东亚全角（CJK 汉字、假名、全角标点）、
 *               emoji 与符号（U+1F300 以上）、CJK 扩展 B 及以后
 *
 * **未验证边界**：不做完整的 East Asian Width 表，组合字符（变音符）、
 * 印度语系连写、零宽连接符等一律按 1 列算。行编辑器场景够用；
 * 要做终端表格对齐就不能用这个。 */
static int tcwin_u8_width(int cp) {
    if (cp < 0) return 1;                 /* 非法字节按 1 列 */
    if (cp == 0) return 0;

    /* 控制字符不占列（实际会被过滤，这里兜底） */
    if (cp < 0x20 || cp == 0x7F) return 0;

    /* 组合用记号（U+0300..U+036F 等）—— 零宽 */
    if (cp >= 0x0300 && cp <= 0x036F) return 0;
    if (cp >= 0x1AB0 && cp <= 0x1AFF) return 0;
    if (cp >= 0x1DC0 && cp <= 0x1DFF) return 0;
    if (cp >= 0x20D0 && cp <= 0x20FF) return 0;
    if (cp >= 0xFE00 && cp <= 0xFE0F) return 0;   /* 变体选择符 */
    if (cp == 0x200B || cp == 0x200C || cp == 0x200D) return 0; /* 零宽 */
    if (cp == 0xFEFF) return 0;                    /* BOM */

    /* 东亚全角：CJK 统一表意文字 */
    if (cp >= 0x1100 && cp <= 0x115F) return 2;    /* 谚文字母 */
    if (cp >= 0x2E80 && cp <= 0x303E) return 2;    /* CJK 部首、标点 */
    if (cp >= 0x3041 && cp <= 0x33FF) return 2;    /* 假名、注音、CJK 兼容 */
    if (cp >= 0x3400 && cp <= 0x4DBF) return 2;    /* CJK 扩展 A */
    if (cp >= 0x4E00 && cp <= 0x9FFF) return 2;    /* CJK 统一表意文字 */
    if (cp >= 0xA000 && cp <= 0xA4CF) return 2;    /* 彝文 */
    if (cp >= 0xAC00 && cp <= 0xD7A3) return 2;    /* 谚文音节 */
    if (cp >= 0xF900 && cp <= 0xFAFF) return 2;    /* CJK 兼容表意文字 */
    if (cp >= 0xFE30 && cp <= 0xFE6F) return 2;    /* CJK 兼容形式 */
    if (cp >= 0xFF00 && cp <= 0xFF60) return 2;    /* 全角 ASCII */
    if (cp >= 0xFFE0 && cp <= 0xFFE6) return 2;    /* 全角符号 */
    if (cp >= 0x1F300 && cp <= 0x1F64F) return 2;  /* emoji */
    if (cp >= 0x1F900 && cp <= 0x1F9FF) return 2;  /* emoji 补充 */
    if (cp >= 0x20000 && cp <= 0x3FFFD) return 2;  /* CJK 扩展 B 及以后 */

    return 1;
}

/* 计算 buf 前 `bytes` 个字节在终端上占多少列。 */
static int tcwin_u8_display_width(const char *buf, int bytes) {
    int i = 0, w = 0, adv, cp;
    while (i < bytes) {
        cp = tcwin_u8_decode(buf + i, &adv);
        if (adv < 1) adv = 1;
        w += tcwin_u8_width(cp);
        i += adv;
    }
    return w;
}

/* 追加一个码点到 buf（UTF-8 编码），返回写入的字节数。
 *
 * cap 是缓冲区总容量（含要为 NUL 留的那一字节）。
 * 空间不足时返回 0 且不写入。成功时**不写 NUL**（由调用方维护）。 */
static int tcwin_u8_encode(int cp, char *buf, int cap) {
    if (!buf || cap <= 0) return 0;
    if (cp < 0) return 0;

    if (cp < 0x80) {
        if (cap < 1) return 0;
        buf[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        if (cap < 2) return 0;
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        if (cp >= 0xD800 && cp <= 0xDFFF) return 0;   /* 代理区：不能编码 */
        if (cap < 3) return 0;
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    if (cp <= 0x10FFFF) {
        if (cap < 4) return 0;
        buf[0] = (char)(0xF0 | (cp >> 18));
        buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0;
}

/* 把一个 UTF-16 码元序列（Windows 控制台给的就是这个）转成 UTF-8。
 *
 * 返回写入 buf 的字节数（不含 NUL；本函数会写 NUL，需要 cap >= 1）。
 * 空间不足返回 0。
 *
 * 处理代理对：高位代理（D800..DBFF）+ 低位代理（DC00..DFFF）
 * 合成一个 U+10000 以上的码点。落单的代理按 U+FFFD 替换。 */
static int tcwin_u8_from_utf16(const unsigned short *w, int wlen,
                             char *buf, int cap) {
    int i = 0, n = 0, adv;
    if (!w || !buf || cap < 1) return 0;
    buf[0] = 0;

    while (i < wlen) {
        int cp = (int)w[i];

        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < wlen
            && w[i + 1] >= 0xDC00 && w[i + 1] <= 0xDFFF) {
            /* 合法代理对 */
            cp = 0x10000 + ((cp - 0xD800) << 10) + ((int)w[i + 1] - 0xDC00);
            i += 2;
        } else if (cp >= 0xD800 && cp <= 0xDFFF) {
            cp = 0xFFFD;                 /* 落单代理 */
            i++;
        } else {
            i++;
        }

        if (n + 4 >= cap) break;         /* 留 NUL */
        adv = tcwin_u8_encode(cp, buf + n, cap - n - 1);
        if (adv <= 0) break;
        n += adv;
    }

    buf[n] = 0;
    return n;
}

#endif /* TCWIN_RL_UTF8_H */
