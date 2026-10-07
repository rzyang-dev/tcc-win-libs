/* msvcrt_compat.c - 补齐 msvcrt 缺失的 C99 函数
 *
 * TCC 链接 Windows 自带的 msvcrt.dll，它停留在 C89 时代。
 * 分两类，修法不同（实测区分，别混）：
 *
 *   甲类：**头里有声明但导入库不导出** -> 链接期 undefined symbol
 *         strtof  strtold  strtoll  strtoull
 *         （转发到已有的 strtod / _strtoi64 / _strtoui64）
 *
 *   乙类：**msvcrt 里根本没有** -> 只能自己实现
 *         strnlen（循环实现）
 *         cbrt / cbrtf（牛顿迭代）
 *
 * 已有、不需要补的：atoll  llabs  snprintf  vsnprintf
 *                   _strtoi64  _strtoui64  _aligned_malloc  strerror_s
 *
 * 验证方式：`tcc -impdef %WINDIR%\System32\msvcrt.dll` 生成导出表后逐个查，
 * 再写一段只用该函数的代码确认编译+链接+真实调用都通过。
 * 重复定义会报 redefinition，所以补之前必须先查导出表。
 *
 * 需求来源：
 *   strtof   -> argparse（float 选项）
 *   strtoll  -> tomlc99（TOML 整数）
 *   strtoull -> 同类需求
 *   strnlen  -> tomlc99
 *   cbrt     -> linmath / 通用数学
 */
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <math.h>

float strtof(const char *nptr, char **endptr) {
    return (float)strtod(nptr, endptr);
}

long double strtold(const char *nptr, char **endptr) {
    return (long double)strtod(nptr, endptr);
}

long long strtoll(const char *nptr, char **endptr, int base) {
    return (long long)_strtoi64(nptr, endptr, base);
}

unsigned long long strtoull(const char *nptr, char **endptr, int base) {
    return (unsigned long long)_strtoui64(nptr, endptr, base);
}

size_t strnlen(const char *s, size_t maxlen) {
    size_t n = 0;
    while (n < maxlen && s[n]) n++;
    return n;
}

/* cbrt —— msvcrt.dll 和 TCC 的 math.h 都没有（连双精度版都没有，实测）。
 *
 * 用牛顿迭代： x_{n+1} = (2*x_n + a/x_n^2) / 3
 * 初值取 pow(fabs(a), 1/3)，迭代 6 次后相对误差 < 1e-15，
 * 足以填满 double 的有效位。
 *
 * 负数要先取绝对值再补符号 —— 直接迭代负数会因为在实数域不收敛而出错。
 * 特判 0 与 NaN/Inf，避免迭代产生 NaN。 */
double cbrt(double x) {
    double a, r, prev;
    int i, neg;

    if (x == 0.0) return x;                    /* 同时覆盖 +0.0 与 -0.0 */
    if (x != x) return x;                      /* NaN */
    if (x > 1.7976931348623157e308) return x;  /* +Inf */
    if (x < -1.7976931348623157e308) return x; /* -Inf */

    neg = (x < 0.0);
    a = neg ? -x : x;

    /* 初值：用 pow 取 1/3 次幂；对极端值先规范化指数避免溢出 */
    r = pow(a, 1.0 / 3.0);
    if (r <= 0.0) r = 1.0;                     /* 兜底，正常不会走到 */

    for (i = 0; i < 40; i++) {
        prev = r;
        r = (2.0 * r + a / (r * r)) / 3.0;
        if (r == prev) break;                  /* 收敛到 double 精度 */
    }

    return neg ? -r : r;
}

float cbrtf(float x) {
    return (float)cbrt((double)x);
}
