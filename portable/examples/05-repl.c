/* 05-repl.c - 带行编辑与历史的交互式命令行（readline 用法示例）
 *
 * 演示 tcwinreadline 的完整用法：提示符、行编辑、历史、Ctrl-C/Ctrl-D。
 *
 * 运行：scripts\tcc.cmd examples\05-repl.c -With readline -Run
 *
 * 试试这些：
 *   - 输入几条命令，按 ↑ / ↓ 翻历史
 *   - ← → 移动光标，在行中间插入
 *   - Ctrl-A / Ctrl-E 跳到行首/行尾
 *   - Ctrl-W 删一个词，Ctrl-U 删到行首，Ctrl-K 删到行尾
 *   - Ctrl-L 清屏
 *   - Ctrl-C 取消当前行，Ctrl-D 退出
 *
 * 也可以管道输入（此时自动退化为逐行读取，不显示提示符）：
 *   echo help | 05-repl.exe
 */
#include "tccwin.h"
#include "tccmath.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "tcwinreadline.h"
#include "tinyexpr.h"

static tcwin_rl_t rl;

static void cmd_help(void) {
    printf(
        "可用命令：\n"
        "  help            显示本帮助\n"
        "  echo <文本>     原样输出\n"
        "  calc <表达式>   用 tinyexpr 求值，例如 calc (1+2)*3\n"
        "  history         列出历史\n"
        "  quit / exit     退出（也可以 Ctrl-D）\n"
        "空白行会被忽略。\n");
}

static void cmd_history(void) {
    int n = tcwin_rl_history_count(&rl);
    if (n == 0) { printf("（还没有历史）\n"); return; }
    for (int i = n - 1; i >= 0; i--) {
        printf("  %3d  %s\n", n - i, tcwin_rl_history_get(&rl, i));
    }
}

/* 计算一个表达式。返回 0 表示成功。 */
static int cmd_calc(const char *expr) {
    int err = 0;
    double v;
    if (!expr || !*expr) { printf("用法: calc <表达式>\n"); return 1; }
    v = te_interp(expr, &err);
    if (err != 0) {
        printf("解析出错，位置 %d\n", err);
        return 1;
    }
    printf("= %g\n", v);
    return 0;
}

int main(void) {
    /* 让控制台按 UTF-8 解释输出，否则中文在 GBK(936) 控制台上乱码。
     * 见 docs/PITFALLS.md P59。 */
    SetConsoleOutputCP(CP_UTF8);    char line[512];
    int n;

    printf("tcwinreadline 演示 —— 输入 help 看可用命令，Ctrl-D 退出\n\n");

    tcwin_rl_init(&rl, "tcwin> ");

    for (;;) {
        n = tcwin_rl_readline(&rl, line, (int)sizeof line);
        if (n < 0) {               /* Ctrl-C 或 Ctrl-D */
            printf("\n");
            break;
        }
        if (n == 0) continue;      /* 空行 */

        /* 计入历史（在离开 readline 之后，这样 ↑ 能立刻翻到） */
        tcwin_rl_history_add(&rl, line);

        if (strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0) break;

        if (strcmp(line, "help") == 0) { cmd_help(); continue; }
        if (strcmp(line, "history") == 0) { cmd_history(); continue; }

        if (strncmp(line, "echo ", 5) == 0) { printf("%s\n", line + 5); continue; }
        if (strncmp(line, "calc ", 5) == 0) { cmd_calc(line + 5); continue; }
        if (strcmp(line, "echo") == 0 || strcmp(line, "calc") == 0) {
            printf("用法: %s <参数>\n", line);
            continue;
        }

        printf("未知命令: %s（输入 help 看帮助）\n", line);
    }

    tcwin_rl_free(&rl);
    printf("再见。\n");
    return 0;
}
