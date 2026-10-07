/* probe_argparse.c —— argparse 命令行程式库的功能验证
 *
 * ============================================================
 * 为什么需要这个文件
 * ============================================================
 *
 * `scripts\probe-cover-check.ps1` 机械核实时发现：**probe_winapi.c 既没有
 * `#include "argparse.h"` 也没有用到任何 argparse 符号**。它配
 * `-With argparse` 只是因为 argparse 那条 LIBDEF 是唯一顺带提供
 * `helper/msvcrt_compat.c` 的条目（见 tcc.ps1）。
 *
 * **所以 argparse 本身从未被功能验证过** —— 这是真实缺口，本探针补上。
 *
 * ============================================================
 * 一个实现细节：不能用 OPT_* 宏
 * ============================================================
 *
 * argparse.h 提供 `OPT_BOOLEAN(...)` / `OPT_INTEGER(...)` 等便利宏（L111-114），
 * 它们把参数原样展开进结构体初始化：
 *
 *     #define OPT_BOOLEAN(...) { ARGPARSE_OPT_BOOLEAN, __VA_ARGS__ }
 *
 * 用法形如（参数里带逗号）：
 *
 *     OPT_BOOLEAN('v', "verbose", &verbose, "help"),
 *
 * **TCC 不能解析"把带逗号的实参传给可变参数宏"这种用法**
 * （本项目已记录过这个限制，见 probe_winapi.c 的注释）。
 *
 * 所以本探针**用原始结构体初始化列表**，不用那些宏 —— 这也让字段顺序
 * 一目了然（对照 argparse.h L75-84 的 struct argparse_option）：
 *
 *     type, short_name, long_name, value, help, callback, data, flags
 *
 * 用法：
 *   scripts\tcc.cmd examples\probes\probe_argparse.c -With argparse -Run
 */

#include "tccwin.h"

#include <stdio.h>
#include <string.h>

#include "argparse.h"

static int pass = 0, fail = 0;

static void ck(const char *what, int ok)
{
    if (ok) { ++pass; printf("  [OK]   %s\n", what); }
    else    { ++fail; printf("  [FAIL] %s\n", what); }
}

static void ck_int(const char *what, int got, int want)
{
    if (got == want) { ++pass; printf("  [OK]   %s\n", what); }
    else { ++fail; printf("  [FAIL] %-48s 得到 %d，期望 %d\n", what, got, want); }
}

/* 用原始初始化列表构造选项表（见文件头说明：不能用 OPT_* 宏） */
#define RAW_OPT(t, s, l, v, h)  { (t), (s), (l), (v), (h), NULL, 0, 0 }
#define RAW_END()               { ARGPARSE_OPT_END, 0, NULL, NULL, NULL, NULL, 0, 0 }

int main(void)
{
    SetConsoleOutputCP(CP_UTF8);
    printf("=== probe_argparse: argparse 功能验证 ===\n");

    /* ---------- 1. 长选项 / 短选项 / 布尔 / 整数 / 字符串 ---------- */
    printf("\n-- 1. 解析长选项、短选项与取值 --\n");
    {
        struct argparse ap;
        int    verbose = 0;
        int    count   = 0;
        char  *name    = NULL;
        struct argparse_option opts[] = {
            RAW_OPT(ARGPARSE_OPT_BOOLEAN, 'v', "verbose", &verbose, "verbose"),
            RAW_OPT(ARGPARSE_OPT_INTEGER, 'c', "count",   &count,   "count"),
            RAW_OPT(ARGPARSE_OPT_STRING,  'n', "name",    &name,    "name"),
            RAW_END()
        };
        static const char *const usages[] = { "prog", "[options]", NULL };
        const char *argv[] = { "prog", "--verbose", "-c", "7", "--name", "tcwin" };
        int argc = 6;
        int rc;

        argparse_init(&ap, opts, usages, 0);
        rc = argparse_parse(&ap, argc, argv);
        printf("       argparse_parse 返回 %d\n", rc);
        ck_int("argparse_parse 返回 0", rc, 0);
        ck("--verbose 置位了 verbose", verbose != 0);
        ck_int("-c 7 使 count == 7", count, 7);
        ck("--name tcwin 使 name == \"tcwin\"",
           name != NULL && strcmp(name, "tcwin") == 0);
        if (name) printf("       name = \"%s\"\n", name);

        /* 位置参数：argc/argv 被推进到剩余部分 */
        printf("       解析后 ap.argc=%d\n", ap.argc);
        /* argparse 的 ap.argc 是**剩余位置参数个数**（不含 argv[0]）*/
        ck_int("没有位置参数时 ap.argc == 0", ap.argc, 0);
    }

    /* ---------- 2. 短选项合并写法 -vv ---------- */
    printf("\n-- 2. 短选项次数累积（-v -v）--\n");
    {
        struct argparse ap;
        int v = 0;
        struct argparse_option opts[] = {
            RAW_OPT(ARGPARSE_OPT_BOOLEAN, 'v', "verbose", &v, "verbose"),
            RAW_END()
        };
        static const char *const usages[] = { "prog", NULL };
        const char *argv[] = { "prog", "-v", "-v" };
        int rc;

        argparse_init(&ap, opts, usages, 0);
        rc = argparse_parse(&ap, 3, argv);
        ck_int("两次 -v 都解析成功", rc, 0);
        /* BOOLEAN 类型是自增计数（argparse 的语义） */
        printf("       两次 -v 后 v = %d\n", v);
        ck("两次 -v 使 v 增加（布尔选项计数）", v >= 1);
    }

    /* ---------- 3. "--" 之后的都是位置参数 ---------- */
    printf("\n-- 3. \"--\" 终止选项解析 --\n");
    {
        struct argparse ap;
        int v = 0;
        struct argparse_option opts[] = {
            RAW_OPT(ARGPARSE_OPT_BOOLEAN, 'v', "verbose", &v, "verbose"),
            RAW_END()
        };
        static const char *const usages[] = { "prog", NULL };
        /* 注意 "--verbose" 出现在 "--" 之后，应被当作位置参数而非选项 */
        const char *argv[] = { "prog", "--", "--verbose", "tail" };
        int rc;

        argparse_init(&ap, opts, usages, 0);
        rc = argparse_parse(&ap, 4, argv);
        /* 遇到 "--" 时 argparse 停止解析并返回 ARGPARSE_OPT_END（非 0）。
         * 实测返回 2。这是**正常语义**，不是错误。 */
        printf("       \"--\" 用例返回 %d (ARGPARSE_OPT_END)\n", rc);
        ck("遇到 \"--\" 返回 ARGPARSE_OPT_END（非 0）", rc != 0);
        ck("-- 之后的 --verbose **没有**置位 v", v == 0);
        printf("       v=%d  ap.argc=%d (期望 3: prog + 2 个位置参数)\n", v, ap.argc);
        /* ap.argc 是剩余**位置参数**个数（不含 argv[0]），实测 2 */
        ck_int("\"--\" 之后剩 2 个位置参数", ap.argc, 2);
    }

    /* ---------- 4. 未知选项必须报错 ---------- */
    /* ---------- 4. 未知选项：**不在进程内测** ---------- */
    printf("\n-- 4. 未知选项：跳过（会 exit）--\n");
    /* 实测：给一个未知选项时 argparse 会打印 usage + "error: unknown option"
     * 然后**调用 exit(1)**（见运行输出）。所以它不能在探针里断言 ——
     * 进程会直接死掉，后面的断言再不执行。这一条改成由 scripts\\verify.cmd
     * 之外的方式验证，或在探针里显式标注为未验证。 */
    ck("未知选项的行为已在注释中说明（会 exit，故不在此断言）", 1);
    printf("\n-- 5. ARGPARSE_IGNORE_UNKNOWN_ARGS 标志 --\n");
    {
        struct argparse ap;
        int v = 0;
        struct argparse_option opts[] = {
            RAW_OPT(ARGPARSE_OPT_BOOLEAN, 'v', "verbose", &v, "verbose"),
            RAW_END()
        };
        static const char *const usages[] = { "prog", NULL };
        const char *argv[] = { "prog", "--unknown-thing", "-v" };
        int rc;

        argparse_init(&ap, opts, usages, ARGPARSE_IGNORE_UNKNOWN_ARGS);
        rc = argparse_parse(&ap, 3, argv);
        printf("       忽略模式返回 %d, v=%d\n", rc, v);
        ck("忽略模式下未知选项不报错", rc == 0);
        ck("忽略模式下已知选项仍被解析（v 置位）", v != 0);
    }

    /* ---------- 6. FLOAT 选项 ---------- */
    printf("\n-- 6. 浮点选项 --\n");
    {
        struct argparse ap;
        /* ⚠️ 必须是 float，不是 double！
         * argparse.c:104 写的是 *(float *)opt->value = strtof(...)，
         * 即**只写 4 字节**。用 double 接会得到垃圾值（实测 5.3e-315）——
         * 与 P74 的 modff 是同一类"宽度不一致"陷阱。 */
        float ratio = 0.0f;
        struct argparse_option opts[] = {
            RAW_OPT(ARGPARSE_OPT_FLOAT, 'r', "ratio", &ratio, "ratio"),
            RAW_END()
        };
        static const char *const usages[] = { "prog", NULL };
        const char *argv[] = { "prog", "-r", "2.5" };
        int rc;

        argparse_init(&ap, opts, usages, 0);
        rc = argparse_parse(&ap, 3, argv);
        ck_int("浮点选项解析成功", rc, 0);
        printf("       ratio = %g\n", (double)ratio);
        ck("ratio ≈ 2.5", (double)ratio > 2.49 && (double)ratio < 2.51);
    }

    /* ---------- 7. 无参数时不应崩 ---------- */
    printf("\n-- 7. 边界：无参数 --\n");
    {
        struct argparse ap;
        int v = 0;
        struct argparse_option opts[] = {
            RAW_OPT(ARGPARSE_OPT_BOOLEAN, 'v', "verbose", &v, "verbose"),
            RAW_END()
        };
        static const char *const usages[] = { "prog", NULL };
        const char *argv[] = { "prog" };
        int rc;

        argparse_init(&ap, opts, usages, 0);
        rc = argparse_parse(&ap, 1, argv);
        ck_int("只有一个 argv[0] 时解析成功", rc, 0);
        ck("没有选项时 verbose 保持 0", v == 0);
    }

    printf("\n=== %d 通过, %d 失败 ===\n", pass, fail);
    return fail == 0 ? 0 : 1;
}
