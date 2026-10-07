/* tccmath.h - 补齐 TCC 0.9.27 上真正不可用的 float 数学函数与 C99 比较宏
 *
 * ======================================================================
 * 精确边界（2025 轮用正确方法重测，方法见 docs/research/TCC-BASELINE.md）
 * ======================================================================
 *
 * 正确测法：`tcc.exe` **不加任何 -I**（否则测的是自己的垫片），
 * 链接**必须带 `-L lib\tccdefs`**（否则把"缺导入库"误判成"编译器不支持"），
 * 结论必须**同时过编译与链接**并真实调用一次。
 *
 * 逐一测 30 个 float 函数，**真正不可用的只有 4 个**：
 *
 *   fabsf       -> math.h:214 用 x87 内联汇编约束 "t"，TCC 报 unknown constraint
 *   truncf      -> undefined symbol（CRT 里没有）
 *   cbrt        -> undefined symbol（**连双精度版都没有**）
 *   nearbyintf  -> undefined symbol（**连双精度版都没有**）
 *
 * **其余 26 个 TCC 全都提供**（编译+链接都过）：
 *
 *   sqrtf sinf cosf tanf expf logf log10f powf floorf ceilf fmodf
 *   atan2f hypotf sinhf coshf tanhf asinf acosf atanf roundf
 *   copysignf fminf fmaxf ldexpf frexpf modff
 *
 * ----------------------------------------------------------------------
 * ⚠️ 本文件曾把上述 16 个函数写成"全部不可用"，**那是错的**。
 *    根因是当时用 `tcc.exe -I <portable>\include` 探测 —— 那测的是自己的
 *    垫片而不是 TCC；且漏了 `-L lib\tccdefs`，把缺导入库当成了缺实现。
 *    这条教训记在 PITFALLS P62 与 docs/research/TOOL-AUDIT.md。
 * ----------------------------------------------------------------------
 *
 * 下面那批 `#define` 因此**多数是冗余的**，保留它们只是为了：
 *   1. 兼容已有代码（宏改名不影响调用写法）
 *   2. 让"一律加 tccmath.h"这个简单规则继续成立
 * 代价是它们会**遮蔽 TCC 自带的真实现**（float 版转发到 double 版，
 * 精度略降、有轻微性能开销）。
 *
 * **想要 TCC 的原生 float 实现**：只包含本头而不依赖那批宏，
 * 或直接调用 26 个可用函数（见上），它们不需要本文件。
 *
 * ======================================================================
 * 用法
 * ======================================================================
 *
 *   #include "tccwin.h"     // 或 <windows.h>
 *   #include "tccmath.h"
 *
 * 需要 `cbrt` / `cbrtf` 时还要链上 `helper/msvcrt_compat.c`
 * （`-With argparse` / `-With toml` 会自动带；也可自己加进源文件列表）。
 * `nearbyint` / `nearbyintf` 转发到 `rint`（实测 rint 是银行家舍入
 * 2.5->2, 3.5->4，正是 C99 nearbyint 在默认舍入模式下的行为）。
 */
#ifndef TCCMATH_H
#define TCCMATH_H

#include <math.h>

#ifdef __TINYC__
/* 守卫**不能**只判 64 位。
 *
 * 踩过的坑：这里原来是 `#if defined(__x86_64) || defined(_WIN64)`，
 * 于是 32 位下这一整块被跳过 —— 而实测 32 位下 sqrtf / sinf / cosf /
 * powf / floorf / ceilf / fmodf / atan2f / logf / expf / truncf
 * **全都链接失败**，fabsf 报 unknown constraint 't'。
 *
 * 为什么（读 _mingw.h:54 并用 -E 展开确认）：
 *     #define __CRT_INLINE extern __inline__
 * 所以 math.h 里那些 `__CRT_INLINE float powf(...) {...}` **只是声明**，
 * 不带定义（`extern` 覆盖了 `inline` 的内部链接语义）。
 * math.h:247 的 `#ifndef __x86_64` 在 32 位下确实成立，但那只放行了
 * **声明**；msvcrt 里没有 powf 的实现，链接期一样 undefined。
 *
 * **结论：这个问题与位宽无关，32/64 位都需要这层转发。**
 * 原来只判 64 位，是因为当时只在 64 位下测过 —— 见 PITFALLS P73。 */
#if defined(__x86_64) || defined(_WIN64) || defined(__i386__) || defined(_X86_)

/* 这一批 TCC 自带可用，加 #ifndef 守卫是为了**不遮蔽**它的真实现。
 * 只有在某个函数真的不可用时（理论上不该发生）才会用到宏版本。 */
#ifndef sqrtf
#  define sqrtf(x)    ((float)sqrt((double)(x)))
#endif
#ifndef sinf
#  define sinf(x)     ((float)sin((double)(x)))
#endif
#ifndef cosf
#  define cosf(x)     ((float)cos((double)(x)))
#endif
#ifndef tanf
#  define tanf(x)     ((float)tan((double)(x)))
#endif
#ifndef asinf
#  define asinf(x)    ((float)asin((double)(x)))
#endif
#ifndef acosf
#  define acosf(x)    ((float)acos((double)(x)))
#endif
#ifndef atanf
#  define atanf(x)    ((float)atan((double)(x)))
#endif
#ifndef expf
#  define expf(x)     ((float)exp((double)(x)))
#endif
#ifndef logf
#  define logf(x)     ((float)log((double)(x)))
#endif
#ifndef log10f
#  define log10f(x)   ((float)log10((double)(x)))
#endif
#ifndef log2f
#  define log2f(x)    ((float)(log((double)(x)) / 0.69314718055994530942))
#endif
#ifndef floorf
#  define floorf(x)   ((float)floor((double)(x)))
#endif
#ifndef ceilf
#  define ceilf(x)    ((float)ceil((double)(x)))
#endif
#ifndef roundf
#  define roundf(x)   ((float)floor((double)(x) + 0.5))
#endif
#ifndef fmodf
#  define fmodf(a,b)  ((float)fmod((double)(a), (double)(b)))
#endif
#ifndef powf
#  define powf(a,b)   ((float)pow((double)(a), (double)(b)))
#endif
#ifndef atan2f
#  define atan2f(a,b) ((float)atan2((double)(a), (double)(b)))
#endif
#ifndef hypotf
#  define hypotf(a,b) ((float)sqrt((double)(a)*(double)(a) + (double)(b)*(double)(b)))
#endif
#ifndef sqrtl
#  define sqrtl(x)    ((long double)sqrt((double)(x)))
#endif
#ifndef fabsl
#  define fabsl(x)    ((long double)fabs((double)(x)))
#endif

/* --- 32 位下额外缺的 5 个（64 位 TCC 自带，32 位没有）----------------
 *
 * 实测（scripts\\tcc.cmd -32）：sinhf / coshf / tanhf / copysignf / modff
 * 在 32 位下都 undefined symbol，而它们的**双精度版都可用**
 * （sinh / cosh / tanh 有导出；copysign / modf 需要两个参数，写测试时注意）。
 * 所以直接转发到双精度版即可。
 *
 * 这是 P73 那次"扩宽守卫"的收尾：扩宽后发现 32 位还差这 5 个。
 * ------------------------------------------------------------------ */
#ifndef TCWIN_FLOAT_EXTRA
#define TCWIN_FLOAT_EXTRA
#  ifndef sinhf
#    define sinhf(x)     ((float)sinh((double)(x)))
#  endif
#  ifndef coshf
#    define coshf(x)     ((float)cosh((double)(x)))
#  endif
#  ifndef tanhf
#    define tanhf(x)     ((float)tanh((double)(x)))
#  endif
#  ifndef copysignf
#    define copysignf(a,b) ((float)copysign((double)(a),(double)(b)))
#  endif
/* ⚠️ modff **故意不做成宏** —— 宏版本会造成内存越界写。
 *
 * 曾经写成：
 *     #define modff(x,ip) ((float)modf((double)(x), (double *)(ip)))
 * 看着像"转发到双精度版"，实际是**错的**：`modf` 会往第二个参数写
 * **8 字节**（double），而调用方按 `modff` 的签名给的是 **4 字节的
 * `float`** —— 于是：
 *   1. 整数部分的值不对（实测 `modff(3.75f,&f)` 得到 int=0.00）
 *   2. **越界写 4 字节栈内存** —— 静默破坏相邻变量
 *
 * 正确实现必须自己声明一个 double 中转：
 *     float modff(float x, float *ip) {
 *         double di; double df = modf((double)x, &di);
 *         if (ip) *ip = (float)di;
 *         return (float)df;
 *     }
 * 这需要真正的函数体，宏做不到 —— 所以放在 helper/math_compat.c 里，
 * 由 `-With math` 提供。**不在 -With math 时调用 modff 会链接失败
 * （这是有意的：宁可链接失败，不要静默越界写）。**
 *
 * 见 PITFALLS P74。 */
#  ifndef TCWIN_MODFF_DECLARED
#    define TCWIN_MODFF_DECLARED
float modff(float x, float *iptr);     /* 实现在 helper/math_compat.c */
#  endif
#endif /* TCWIN_FLOAT_EXTRA */

/* ---------------------------------------------------------------------
 * 以下 4 个是**真正必要**的（无守卫，必须覆盖）
 * --------------------------------------------------------------------- */

/* fabsf：math.h:214 的内联汇编用了 TCC 不支持的约束 "t"，
 * 必须先 #undef 掉它的声明再换成宏。 */
#undef fabsf
#define fabsf(x)    ((float)fabs((double)(x)))

/* truncf：CRT 里没有实现。向零取整。 */
#undef truncf
#define truncf(x)   ((float)((double)(x) < 0 ? ceil((double)(x)) : floor((double)(x))))

#endif /* __x86_64 || _WIN64 */
#endif /* __TINYC__ */

/* ------------------------------------------------------------------------
 * C99 浮点比较宏：TCC 上两条路都走不通，所以必须补
 *
 * TCC 的 math.h:668 用 `#if __GNUC__ >= 3` 分两条路，**两条都不可用**：
 *
 *   真分支：__builtin_isgreater 等 —— TCC 不实现这些内建
 *   假分支：__fp_unordered_compare 用 x87 内联汇编约束 "t" ——
 *           TCC x86_64 报 `unknown constraint \x27t\x27`
 *
 * TCC 不定义 __GNUC__，所以理论上是走假分支；但实测两条路的产物都不可用，
 * `isgreater(1.0, 2.0)` 报 undefined symbol \x27isgreater\x27。
 *
 * 这里的实现不用内建也不用内联汇编：NaN 参与比较时 C 的 `>` `<` `>=` `<=`
 * 全部返回 false（只有 `!=` 为 true），所以用 `x != x` 判 NaN 即可。
 * 语义与 C99 7.12.14 一致，且不依赖任何 GCC 扩展。
 *
 * 放在 __TINYC__ 块**外面**，因为它是无条件生效的垫片；
 * 用 #undef 先清掉 math.h 可能留下的坏定义。
 * ------------------------------------------------------------------------ */
#ifndef TCWIN_C99_COMPARE
#define TCWIN_C99_COMPARE
#undef isgreater
#undef isgreaterequal
#undef isless
#undef islessequal
#undef islessgreater
#undef isunordered
#define isgreater(x, y)       (!((x) != (x)) && !((y) != (y)) && ((x) >  (y)))
#define isgreaterequal(x, y)  (!((x) != (x)) && !((y) != (y)) && ((x) >= (y)))
#define isless(x, y)          (!((x) != (x)) && !((y) != (y)) && ((x) <  (y)))
#define islessequal(x, y)     (!((x) != (x)) && !((y) != (y)) && ((x) <= (y)))
#define islessgreater(x, y)   (!((x) != (x)) && !((y) != (y)) && ((x) < (y) || (x) > (y)))
#define isunordered(x, y)     (((x) != (x)) || ((y) != (y)))
#endif /* TCWIN_C99_COMPARE */


/* --- 双精度也缺的（连 msvcrt 都没有），实现在 helper/msvcrt_compat.c ---
 *
 * 实测：cbrt 与 nearbyint 的**双精度版在 msvcrt.dll 里也没有导出**，
 * 所以不能靠"转发到双精度版"解决，必须自己实现。
 *   cbrt / cbrtf        -> 牛顿迭代（见 msvcrt_compat.c）
 *   nearbyint/nearbyintf -> 默认舍入模式下等价于 rint（实测银行家舍入一致）
 *
 * 注意这里只给声明与宏，**实现要链 helper/msvcrt_compat.c**。
 * `-With argparse` / `-With toml` 会自动带上它。
 */
#ifndef TCWIN_CBRT_DECL
#define TCWIN_CBRT_DECL
double cbrt(double x);
float  cbrtf(float x);
#ifndef nearbyint
#  define nearbyint(x)  rint((double)(x))
#endif
#ifndef nearbyintf
#  define nearbyintf(x) ((float)rint((double)(x)))
#endif
#endif /* TCWIN_CBRT_DECL */

#endif /* TCCMATH_H */
