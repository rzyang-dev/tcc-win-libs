/* probe_mathgap.c —— float 数学函数在位宽间的差异（`-With math`）
 *
 * ============================================================
 * 这个探针要抓什么
 * ============================================================
 *
 * 项目原来的记录是"TCC 上真正不可用的 float 函数只有 4 个"
 * （`fabsf` / `truncf` / `cbrt` / `nearbyintf`）——**那个结论只在 64 位成立**。
 *
 * 实测 32 位（`scripts\tcc.cmd -32`）：
 *
 *     sqrtf sinf cosf tanf powf floorf ceilf fmodf atan2f
 *     logf log10f expf asinf acosf atanf
 *     → 全部 undefined symbol
 *     fabsf  → unknown constraint 't'
 *
 * 而 `tccmath.h` 的修复当时被 `#if defined(__x86_64) || defined(_WIN64)` 挡住了，
 * 所以 32 位整块被跳过 —— **一个位宽条件的判断错误，让一半目标不可用**。
 * 见 PITFALLS P73。
 *
 * 本探针**逐函数真实调用并检查数值结果**，两位宽都要过。
 *
 * 用法：
 *   scripts\tcc.cmd examples\probes\probe_mathgap.c -With math -Run
 *   scripts\tcc.cmd examples\probes\probe_mathgap.c -With math -Run -32
 */

#include "tccwin.h"
#include "tccmath.h"

#include <stdio.h>
#include <math.h>

static int pass = 0, fail = 0;

static void ck(const char *what, int ok)
{
    if (ok) { ++pass; printf("  [OK]   %s\n", what); }
    else    { ++fail; printf("  [FAIL] %s\n", what); }
}

/* 浮点比较：允许 1e-4 的相对误差（float 精度有限） */
static int near_f(float got, float want)
{
    float d = got - want;
    if (d < 0) d = -d;
    return d < 1e-4f;
}

static void ck_f(const char *what, float got, float want)
{
    if (near_f(got, want)) { ++pass; printf("  [OK]   %s\n", what); }
    else { ++fail; printf("  [FAIL] %-46s 得到 %.6f，期望 %.6f\n", what, (double)got, (double)want); }
}

int main(void)
{
    float ip, fr, guard;

    SetConsoleOutputCP(CP_UTF8);
    printf("=== probe_mathgap: float 数学函数（位宽差异）===\n");
    printf("    指针大小 = %d 字节 -> 这是 %d 位构建\n",
           (int)sizeof(void *), (int)(sizeof(void *) * 8));

    printf("\n-- 1. 位宽守卫确实生效了 --\n");
    /* 这个断言是探针的核心目的：确认 tccmath.h 的那批宏**在本位宽下可用**。
     * 如果守卫又只判 64 位，32 位这里会编译/链接失败。 */
    {
        volatile float four = 4.0f;
        ck_f("sqrtf(4) == 2（32 位曾 undefined）", sqrtf(four), 2.0f);
    }

    printf("\n-- 2. 原报告里就标为不可用的 4 个 --\n");
    ck_f("fabsf(-3.5) == 3.5", fabsf(-3.5f), 3.5f);
    ck_f("truncf(2.9) == 2", truncf(2.9f), 2.0f);
    ck_f("truncf(-2.9) == -2（向零取整）", truncf(-2.9f), -2.0f);
    {
        float c = (float)cbrt(27.0);
        ck("cbrt(27) ≈ 3（需 helper/msvcrt_compat.c 或 math_compat）", c > 2.99f && c < 3.01f);
    }
    {
        float n = nearbyintf(2.5f);
        ck("nearbyintf(2.5) 是 2 或 3（取决于舍入模式）", n == 2.0f || n == 3.0f);
    }

    printf("\n-- 3. 32 位曾大面积不可用的那些（逐个真实调用）--\n");
    ck_f("sinf(0) == 0",                    sinf(0.0f), 0.0f);
    ck_f("cosf(0) == 1",                    cosf(0.0f), 1.0f);
    ck_f("tanf(0) == 0",                    tanf(0.0f), 0.0f);
    ck_f("expf(0) == 1",                    expf(0.0f), 1.0f);
    ck_f("logf(1) == 0",                    logf(1.0f), 0.0f);
    ck_f("log10f(100) == 2",                log10f(100.0f), 2.0f);
    ck_f("powf(2,10) == 1024",              powf(2.0f, 10.0f), 1024.0f);
    ck_f("floorf(1.9) == 1",                floorf(1.9f), 1.0f);
    ck_f("ceilf(1.1) == 2",                 ceilf(1.1f), 2.0f);
    ck_f("fmodf(5,2) == 1",                 fmodf(5.0f, 2.0f), 1.0f);
    ck_f("atan2f(1,1) == pi/4",             atan2f(1.0f, 1.0f), 0.7853982f);
    ck_f("hypotf(3,4) == 5",                hypotf(3.0f, 4.0f), 5.0f);
    ck_f("asinf(1) == pi/2",                asinf(1.0f), 1.5707963f);
    ck_f("acosf(1) == 0",                   acosf(1.0f), 0.0f);
    ck_f("atanf(0) == 0",                   atanf(0.0f), 0.0f);
    ck_f("log2f(8) == 3",                   log2f(8.0f), 3.0f);

    printf("\n-- 4. 双曲函数（本轮新补的 3 个，32 位曾缺）--\n");
    ck_f("sinhf(0) == 0",                   sinhf(0.0f), 0.0f);
    ck_f("coshf(0) == 1",                   coshf(0.0f), 1.0f);
    ck_f("tanhf(0) == 0",                   tanhf(0.0f), 0.0f);
    {
        /* sinhf(1) = 1.1752012, coshf(1) = 1.5430806, tanhf(1) = 0.7615942 */
        ck("sinhf(1) ≈ 1.17520", near_f(sinhf(1.0f), 1.1752012f));
        ck("coshf(1) ≈ 1.54308", near_f(coshf(1.0f), 1.5430806f));
        ck("tanhf(1) ≈ 0.76159", near_f(tanhf(1.0f), 0.7615942f));
    }

    printf("\n-- 5. copysignf（本轮新补，32 位曾缺）--\n");
    ck_f("copysignf(1,-1) == -1",           copysignf(1.0f, -1.0f), -1.0f);
    ck_f("copysignf(1,1) == 1",             copysignf(1.0f, 1.0f), 1.0f);
    ck_f("copysignf(-5,1) == 5",            copysignf(-5.0f, 1.0f), 5.0f);

    printf("\n-- 6. modff —— 宏做不到，必须用 helper/math_compat.c --\n");
    /* 这是本探针最重要的行为断言。
     *
     * 曾经 tccmath.h 里写成宏：
     *     #define modff(x,ip) ((float)modf((double)(x), (double *)(ip)))
     * 后果：modf 往 4 字节的 float 里写 8 字节 double ——
     *   (a) 整数部分错误（实测得到 0.00 而不是 3.00）
     *   (b) **越界写 4 字节栈内存**
     * 而小数部分恰好是对的，所以极难发现。见 PITFALLS P74。 */
    ip = -1.0f;
    guard = 12345.0f;
    fr = modff(3.75f, &ip);
    ck_f("modff(3.75) 小数部分 == 0.75", fr, 0.75f);
    ck_f("modff(3.75) 整数部分 == 3（宏版本会给 0）", ip, 3.0f);
    ck_f("哨兵变量未被越界写破坏", guard, 12345.0f);

    ip = -1.0f;
    fr = modff(9.5f, &ip);
    ck_f("modff(9.5) 小数部分 == 0.5", fr, 0.5f);
    ck_f("modff(9.5) 整数部分 == 9", ip, 9.0f);

    ip = -1.0f;
    fr = modff(-2.25f, &ip);
    ck_f("modff(-2.25) 小数部分 == -0.25", fr, -0.25f);
    ck_f("modff(-2.25) 整数部分 == -2", ip, -2.0f);

    printf("\n-- 7. C99 比较宏（与位宽无关，一并回归）--\n");
    ck("isgreater(2,1)", isgreater(2.0, 1.0) != 0);
    ck("!isgreater(1,2)", isgreater(1.0, 2.0) == 0);
    ck("isunordered(nan,1)", isunordered(0.0 / 0.0, 1.0) != 0);
    ck("!isgreater(nan,1)", isgreater(0.0 / 0.0, 1.0) == 0);

    printf("\n=== %d 通过, %d 失败 ===\n", pass, fail);
    return fail == 0 ? 0 : 1;
}
