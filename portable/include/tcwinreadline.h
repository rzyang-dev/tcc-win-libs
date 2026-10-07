/* tcwinreadline.h - 终端行编辑器（Windows 控制台）
 *
 * 为什么要自己写：linenoise / readline / libedit 都是 POSIX 实现，
 * 依赖 <termios.h> 与 <unistd.h>，Windows 上无法编译（实测 TCC 与
 * MinGW 都失败，见 docs/LIBRARIES.md）。Win32 没有等价的开箱库。
 *
 * ============================================================
 * UTF-8 输入（2025 轮新增）
 * ============================================================
 *   原来完全按字节处理，UTF-8 多字节字符会被退格删成半个、光标按
 *   字节数定位（与屏幕列数不符）。现在：
 *
 *     插入   按码点原子插入（u8_tail 带上序列的后续字节）
 *     退格   删光标左侧一个码点
 *     Delete 删光标处一个码点
 *     左右移 移动一个码点
 *     光标列 提示符宽度 + 光标左侧内容的**显示宽度**
 *
 *   纯逻辑在 include/tcwin_rl_utf8.h，可独立测试
 *   （见 examples/probes/probe_rl_utf8.c，98 项断言）。
 *
 *   控制台层原来只取 uChar.AsciiChar，非 ASCII 输入会被丢掉
 *   （"中" 变成 0x2D）。现在取 uChar.UnicodeChar 转 UTF-8。
 *
 * ============================================================ * ============================================================
 * 设计：分两层，核心逻辑纯函数、可确定性测试
 * ============================================================
 * UTF-8 输入（2025 轮新增）
 * ============================================================
 *   原来完全按字节处理，UTF-8 多字节字符会被退格删成半个、光标按
 *   字节数定位（与屏幕列数不符）。现在：
 *
 *     插入   按码点原子插入（u8_tail 带上序列的后续字节）
 *     退格   删光标左侧一个码点
 *     Delete 删光标处一个码点
 *     左右移 移动一个码点
 *     光标列 提示符宽度 + 光标左侧内容的**显示宽度**
 *
 *   纯逻辑在 include/tcwin_rl_utf8.h，可独立测试
 *   （见 examples/probes/probe_rl_utf8.c，98 项断言）。
 *
 *   控制台层原来只取 uChar.AsciiChar，非 ASCII 输入会被丢掉
 *   （"中" 变成 0x2D）。现在取 uChar.UnicodeChar 转 UTF-8。
 *
 * ============================================================ * ============================================================
 *
 *   第一层（tccwin.h 无关）：行编辑状态机
 *       - 一个固定大小的行缓冲区 + 光标位置 + 历史
 *       - 输入是「按键枚举」，输出是对缓冲区的修改
 *       - **不做任何 I/O**，所以可以完全确定性地测试（喂按键、断言缓冲区）
 *
 *   第二层：Win32 控制台驱动
 *       - SetConsoleMode 关掉行缓冲与回显
 *       - ReadConsoleInput 读按键，翻译成按键枚举
 *       - 按编辑结果重绘整行
 *
 * 这样分层的好处：行编辑逻辑（最容易出错的部分）不依赖终端，
 * 测试不需要交互式输入。这与 Nuklear 那层的验证策略一致。
 *
 * ============================================================
 * UTF-8 输入（2025 轮新增）
 * ============================================================
 *   原来完全按字节处理，UTF-8 多字节字符会被退格删成半个、光标按
 *   字节数定位（与屏幕列数不符）。现在：
 *
 *     插入   按码点原子插入（u8_tail 带上序列的后续字节）
 *     退格   删光标左侧一个码点
 *     Delete 删光标处一个码点
 *     左右移 移动一个码点
 *     光标列 提示符宽度 + 光标左侧内容的**显示宽度**
 *
 *   纯逻辑在 include/tcwin_rl_utf8.h，可独立测试
 *   （见 examples/probes/probe_rl_utf8.c，98 项断言）。
 *
 *   控制台层原来只取 uChar.AsciiChar，非 ASCII 输入会被丢掉
 *   （"中" 变成 0x2D）。现在取 uChar.UnicodeChar 转 UTF-8。
 *
 * ============================================================ * ============================================================
 * 用法
 * ============================================================
 * UTF-8 输入（2025 轮新增）
 * ============================================================
 *   原来完全按字节处理，UTF-8 多字节字符会被退格删成半个、光标按
 *   字节数定位（与屏幕列数不符）。现在：
 *
 *     插入   按码点原子插入（u8_tail 带上序列的后续字节）
 *     退格   删光标左侧一个码点
 *     Delete 删光标处一个码点
 *     左右移 移动一个码点
 *     光标列 提示符宽度 + 光标左侧内容的**显示宽度**
 *
 *   纯逻辑在 include/tcwin_rl_utf8.h，可独立测试
 *   （见 examples/probes/probe_rl_utf8.c，98 项断言）。
 *
 *   控制台层原来只取 uChar.AsciiChar，非 ASCII 输入会被丢掉
 *   （"中" 变成 0x2D）。现在取 uChar.UnicodeChar 转 UTF-8。
 *
 * ============================================================ * ============================================================
 *
 *     #include "tccwin.h"
 *     #include "tcwinreadline.h"
 *
 *     tcwin_rl_t rl;
 *     tcwin_rl_init(&rl, "> ");
 *     char line[512];
 *     for (;;) {
 *         int n = tcwin_rl_readline(&rl, line, sizeof line);
 *         if (n < 0) break;              // Ctrl-C 或 EOF
 *         if (n == 0) continue;          // 空行
 *         // 使用 line ...
 *         tcwin_rl_history_add(&rl, line);
 *     }
 *     tcwin_rl_free(&rl);
 *
 * 编译：scripts\tcc.cmd app.c -With readline
 *
 * ============================================================
 * UTF-8 输入（2025 轮新增）
 * ============================================================
 *   原来完全按字节处理，UTF-8 多字节字符会被退格删成半个、光标按
 *   字节数定位（与屏幕列数不符）。现在：
 *
 *     插入   按码点原子插入（u8_tail 带上序列的后续字节）
 *     退格   删光标左侧一个码点
 *     Delete 删光标处一个码点
 *     左右移 移动一个码点
 *     光标列 提示符宽度 + 光标左侧内容的**显示宽度**
 *
 *   纯逻辑在 include/tcwin_rl_utf8.h，可独立测试
 *   （见 examples/probes/probe_rl_utf8.c，98 项断言）。
 *
 *   控制台层原来只取 uChar.AsciiChar，非 ASCII 输入会被丢掉
 *   （"中" 变成 0x2D）。现在取 uChar.UnicodeChar 转 UTF-8。
 *
 * ============================================================ * ============================================================
 * 支持的按键
 * ============================================================
 * UTF-8 输入（2025 轮新增）
 * ============================================================
 *   原来完全按字节处理，UTF-8 多字节字符会被退格删成半个、光标按
 *   字节数定位（与屏幕列数不符）。现在：
 *
 *     插入   按码点原子插入（u8_tail 带上序列的后续字节）
 *     退格   删光标左侧一个码点
 *     Delete 删光标处一个码点
 *     左右移 移动一个码点
 *     光标列 提示符宽度 + 光标左侧内容的**显示宽度**
 *
 *   纯逻辑在 include/tcwin_rl_utf8.h，可独立测试
 *   （见 examples/probes/probe_rl_utf8.c，98 项断言）。
 *
 *   控制台层原来只取 uChar.AsciiChar，非 ASCII 输入会被丢掉
 *   （"中" 变成 0x2D）。现在取 uChar.UnicodeChar 转 UTF-8。
 *
 * ============================================================ * ============================================================
 *   可打印字符    插入到光标处
 *   ←/→          光标左/右移
 *   Home/End     行首/行尾
 *   Backspace    删除光标左侧字符
 *   Delete       删除光标处字符
 *   Ctrl-A/E     行首/行尾（emacs 风格）
 *   Ctrl-B/F     光标左/右移
 *   Ctrl-U       删除光标左侧全部
 *   Ctrl-K       删除光标右侧全部
 *   Ctrl-W       删除光标左侧一个词
 *   Ctrl-L       清屏并重绘
 *   ↑/↓          历史上下翻
 *   Enter        提交
 *   Ctrl-C       中断（返回 -1）
 *   Ctrl-D       空行时 EOF（返回 -1），非空时删除光标处字符
 *
 * ============================================================
 * UTF-8 输入（2025 轮新增）
 * ============================================================
 *   原来完全按字节处理，UTF-8 多字节字符会被退格删成半个、光标按
 *   字节数定位（与屏幕列数不符）。现在：
 *
 *     插入   按码点原子插入（u8_tail 带上序列的后续字节）
 *     退格   删光标左侧一个码点
 *     Delete 删光标处一个码点
 *     左右移 移动一个码点
 *     光标列 提示符宽度 + 光标左侧内容的**显示宽度**
 *
 *   纯逻辑在 include/tcwin_rl_utf8.h，可独立测试
 *   （见 examples/probes/probe_rl_utf8.c，98 项断言）。
 *
 *   控制台层原来只取 uChar.AsciiChar，非 ASCII 输入会被丢掉
 *   （"中" 变成 0x2D）。现在取 uChar.UnicodeChar 转 UTF-8。
 *
 * ============================================================ * ============================================================
 * 未验证边界
 * ============================================================
 * UTF-8 输入（2025 轮新增）
 * ============================================================
 *   原来完全按字节处理，UTF-8 多字节字符会被退格删成半个、光标按
 *   字节数定位（与屏幕列数不符）。现在：
 *
 *     插入   按码点原子插入（u8_tail 带上序列的后续字节）
 *     退格   删光标左侧一个码点
 *     Delete 删光标处一个码点
 *     左右移 移动一个码点
 *     光标列 提示符宽度 + 光标左侧内容的**显示宽度**
 *
 *   纯逻辑在 include/tcwin_rl_utf8.h，可独立测试
 *   （见 examples/probes/probe_rl_utf8.c，98 项断言）。
 *
 *   控制台层原来只取 uChar.AsciiChar，非 ASCII 输入会被丢掉
 *   （"中" 变成 0x2D）。现在取 uChar.UnicodeChar 转 UTF-8。
 *
 * ============================================================ * ============================================================
 *   - **UTF-8 已支持**（见下）。编辑操作按**码点**进行，光标定位按
 *     **显示宽度**（中文/emoji 算 2 列）
 *   - emoji（U+10000 以上）在真实控制台上会显示成 U+FFFD：
 *     控制台把代理对分两次送按键，而本实现不做跨事件拼合
 *   - 组合字符（变音符）、印度语系连写、零宽连接符按 1 列算，
 *     不是完整的 East Asian Width 表
 *   - 没有 Tab 补全、没有多行编辑、没有括号匹配
 *   - 只在 Windows 控制台（非 ConPTY 重定向）下工作。
 *     输入被重定向时 tcwin_rl_readline 会退化为 fgets
 */
#ifndef TCWINREADLINE_H
#define TCWINREADLINE_H

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 按键枚举（第一层的输入） ---- */
enum tcwin_rl_key {
    TCWIN_RL_KEY_NONE = 0,
    TCWIN_RL_KEY_CHAR,        /* 可打印字符，值在 .ch */
    TCWIN_RL_KEY_ENTER,
    TCWIN_RL_KEY_BACKSPACE,
    TCWIN_RL_KEY_DELETE,
    TCWIN_RL_KEY_LEFT,
    TCWIN_RL_KEY_RIGHT,
    TCWIN_RL_KEY_HOME,
    TCWIN_RL_KEY_END,
    TCWIN_RL_KEY_UP,
    TCWIN_RL_KEY_DOWN,
    TCWIN_RL_KEY_CTRL_A,
    TCWIN_RL_KEY_CTRL_B,
    TCWIN_RL_KEY_CTRL_C,
    TCWIN_RL_KEY_CTRL_D,
    TCWIN_RL_KEY_CTRL_E,
    TCWIN_RL_KEY_CTRL_F,
    TCWIN_RL_KEY_CTRL_K,
    TCWIN_RL_KEY_CTRL_L,
    TCWIN_RL_KEY_CTRL_U,
    TCWIN_RL_KEY_CTRL_W,
    TCWIN_RL_KEY_ESCAPE
};

/* 一次按键事件 */
typedef struct tcwin_rl_event {
    int key;        /* enum tcwin_rl_key */

    /* key == TCWIN_RL_KEY_CHAR 时的字符。
     *
     * ch 是**第一个字节**。ASCII 字符（最常见路径）只用到 ch 就够了，
     * 已有的调用方与探针不受影响。
     *
     * 当输入是 UTF-8 多字节字符时，ch 是首字节，其余字节在 u8_tail 里。
     * 可以直接用这个结构体喂一个完整 UTF-8 序列：
     *
     *     tcwin_rl_event e;
     *     e.key = TCWIN_RL_KEY_CHAR;
     *     e.ch = (char)0xE4;           // "中" 的首字节
     *     e.u8_tail[0] = (char)0xB8;
     *     e.u8_tail[1] = (char)0xAD;
     *     e.u8_tail_len = 2;
     *     tcwin_rl_feed(&rl, e);
     *
     * 不设 u8_tail_len（或设为 0）时行为与以前完全一致 —— 只插入 ch 一个字节。
     * 这样旧代码无需修改。 */
    char ch;
    char u8_tail[3];    /* UTF-8 序列中 ch 之后的字节（最多 3 个） */
    int  u8_tail_len;   /* u8_tail 里有效字节数，0..3 */
} tcwin_rl_event;

/* 编辑器的处理结果（第一层的输出） */
enum tcwin_rl_status {
    TCWIN_RL_CONTINUE = 0,    /* 还需要更多输入 */
    TCWIN_RL_SUBMIT,          /* 用户按了 Enter，行内容可用 */
    TCWIN_RL_ABORT,           /* Ctrl-C */
    TCWIN_RL_EOF,             /* Ctrl-D 在空行 */
    TCWIN_RL_REDRAW           /* 需要整行重绘（Ctrl-L） */
};

/* 行缓冲区上限（含结尾 NUL） */
#ifndef TCWIN_RL_LINE_MAX
#define TCWIN_RL_LINE_MAX 1024
#endif

/* 历史条数上限 */
#ifndef TCWIN_RL_HISTORY_MAX
#define TCWIN_RL_HISTORY_MAX 128
#endif

/* ---- 第一层：行编辑状态机（纯逻辑，不做 I/O） ---- */
typedef struct tcwin_rl {
    char  line[TCWIN_RL_LINE_MAX];   /* 当前行（NUL 结尾） */
    int   len;                     /* 当前长度 */
    int   cursor;                  /* 光标位置 0..len */

    char  prompt[64];              /* 提示符 */

    /* 历史：环形缓冲，hist[0] 是最新的 */
    char  hist[TCWIN_RL_HISTORY_MAX][TCWIN_RL_LINE_MAX];
    int   hist_len;                /* 已存条数 */
    int   hist_head;               /* 下一条要写入的下标 */

    /* 历史浏览状态：-1 表示不在浏览中 */
    int   browse;                  /* 当前浏览到 hist 的下标，-1 = 不在浏览 */
    char  saved[TCWIN_RL_LINE_MAX];  /* 进入浏览前用户正在编辑的内容 */
    int   saved_len;
    int   saved_cursor;
} tcwin_rl_t;

/* 初始化。prompt 可为 NULL（用空串）。 */
void tcwin_rl_init(tcwin_rl_t *rl, const char *prompt);

/* 清空当前行（保留历史） */
void tcwin_rl_clear_line(tcwin_rl_t *rl);

/* 向历史追加一条（空串会被忽略）。重复的相邻条目会被跳过。 */
void tcwin_rl_history_add(tcwin_rl_t *rl, const char *line);

/* 历史条数 */
int  tcwin_rl_history_count(const tcwin_rl_t *rl);

/* 取第 i 条历史（0 = 最新）。越界返回 NULL。 */
const char *tcwin_rl_history_get(const tcwin_rl_t *rl, int i);

/* 处理一个按键事件。返回 enum tcwin_rl_status。
 * 这是第一层的唯一入口 —— 不做任何 I/O，可在测试里随意调用。 */
int  tcwin_rl_feed(tcwin_rl_t *rl, tcwin_rl_event ev);

/* 当前行内容（方便测试断言） */
const char *tcwin_rl_line(const tcwin_rl_t *rl);
int         tcwin_rl_length(const tcwin_rl_t *rl);
int         tcwin_rl_cursor(const tcwin_rl_t *rl);

/* ---- 第二层：Win32 控制台驱动 ---- */

/* 读取一行（阻塞）。
 * 返回 >=0 = 行长；-1 = Ctrl-C 或 EOF。
 * 输入被重定向（非控制台）时退化为 fgets，此时不支持编辑与历史 —— 
 * 这对管道输入是正确的行为。 */
int  tcwin_rl_readline(tcwin_rl_t *rl, char *out, int outsz);

/* 把一条 Win32 控制台输入记录翻译成按键事件。
 * 返回 0 表示该记录不产生事件（非按键、按键抬起、被忽略的组合键）。
 *
 * 公开出来是为了可测试：这是控制台层唯一有分支逻辑的部分，
 * 有了它就不需要真实终端也能验证翻译正确性。 */
int  tcwin_rl_translate_key(const INPUT_RECORD *rec, tcwin_rl_event *ev);

/* 释放内部资源（当前实现没有堆分配，保留是为了 API 稳定） */
void tcwin_rl_free(tcwin_rl_t *rl);

#ifdef __cplusplus
}
#endif
#endif /* TCWINREADLINE_H */
