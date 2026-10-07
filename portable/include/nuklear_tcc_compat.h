/* nuklear_tcc_compat.h - Nuklear 在 TCC 0.9.27 上的适配垫片
 *
 * 用法（顺序固定）：
 *     #include "tccwin.h"
 *     #include "nuklear_tcc_compat.h"   // 本文件：修类型 + 给推荐配置
 *     #define NK_IMPLEMENTATION         // 只在某一个翻译单元里定义
 *     #include "nuklear.h"
 *
 * ============================================================
 * 问题一：平台判定不识别 TCC，x64 上 nk_size 变成 4 字节
 * ============================================================
 * nuklear.h:345-363 决定 NK_SIZE_TYPE 的分支依次是：
 *   1. _WIN64 && _MSC_VER                     -> TCC 无 _MSC_VER
 *   2. _WIN64 && (__MINGW64__ || __clang__)   -> TCC 两者皆无
 *   3. (_WIN32 || WIN32) && _MSC_VER          -> TCC 无 _MSC_VER
 *   4. (_WIN32 || WIN32) && (__MINGW32__ || __clang__)
 *                                             -> TCC 两者皆无
 *   5. __GNUC__ || __clang__                  -> TCC 皆无
 *   6. else: unsigned long   <- 命中。Windows 上 long 是 4 字节
 *
 * 关键细节：**真正的病根是每一档都还额外要求一个 TCC 没有的宏** ——
 * 第 1/3 档要 `_MSC_VER`，第 2/4 档要 `__MINGW64__`/`__MINGW32__` 或
 * `__clang__`，第 5 档要 `__GNUC__`/`__clang__`。TCC 一个都没有，
 * 所以统统命不中，落到第 6 档的 `unsigned long`（Windows 上 4 字节）。
 *
 * ⚠️ **此处曾写成"TCC 在 x64 上只定义 _WIN64，不定义 _WIN32"，那是错的。**
 * 实测（`#if defined(_WIN32) #error` 会触发）TCC 0.9.27 x64 **同时定义了
 * `_WIN32` 与 `_WIN64`**，与 MSVC / MinGW 在 x64 下的行为一致。
 * 错因是把"命不中分支"错误归因成了"宏没定义"——实际是宏定义了，
 * 但同一分支还要求别的宏。见 PITFALLS P70。
 *
 * 这个区别有实际后果：以为 `_WIN32` 没定义，就会写出
 * `#if defined(_WIN64) && !defined(_WIN32)` 这类多余的绕法。
 * 所以连第 3、4 条 Windows 分支都进不去。
 *
 * 后果是 nuklear.h:420 的静态断言失败：
 *     NK_STATIC_ASSERT(sizeof(nk_size) >= sizeof(void*));
 * 报错信息只有一句 "invalid array size"，很难反推到真正原因。
 *
 * 修法：Nuklear 用 #ifndef NK_SIZE_TYPE 守卫，包含前预定义即可覆盖。
 *
 * ============================================================
 * 问题二：默认配置下 nk_font_atlas 等类型不可见
 * ============================================================
 * 不定义 NK_INCLUDE_FONT_BAKING 时，`struct nk_font_atlas` 只有前置声明，
 * 直接声明该类型变量会报 "unknown type size"，同样难以反推。
 * 本垫片直接给出可用配置，避免使用者逐个试宏。
 */
#ifndef NUKLEAR_TCC_COMPAT_H
#define NUKLEAR_TCC_COMPAT_H

/* ---- 修正零：让裸 stb_truetype 调用不会解引用空指针 ----
 *
 * Nuklear 把 stb_truetype 的分配器重定向到自己的包装函数
 * （nuklear.h:6490-6504）：
 *
 *     #ifndef STBTT_malloc
 *     nk_stbtt_malloc(nk_size size, void *user_data) {
 *         struct nk_allocator *alloc = (struct nk_allocator *) user_data;
 *         return alloc->alloc(alloc->userdata, 0, size);   // user_data 为 NULL 就崩
 *     }
 *     #define STBTT_malloc(x,u)  nk_stbtt_malloc(x,u)
 *
 * Nuklear 自己调用时会传 atlas 的 allocator，没问题；
 * 但**使用者直接调用 stbtt_* 时通常传 NULL**（stb 的惯例就是传 NULL），
 * 于是解引用空指针 —— 表现为**访问违例 0xC0000005，且没有任何错误信息**，
 * 极难定位（实测在 stbtt_GetCodepointBitmap 处崩溃）。
 *
 * nuklear.h:6490 有 `#ifndef STBTT_malloc` 守卫，所以在这里抢先定义
 * 成系统 malloc/free 即可。Nuklear 内部的字体烘焙走自己的
 * atlas allocator，不受影响（它自带 stbtt 实现时用的是内部路径）。
 *
 * 想用别的分配器就自行定义这两个宏。 */
#ifndef STBTT_malloc
#  include <stdlib.h>
#  define STBTT_malloc(x,u)  ((void)(u), malloc(x))
#  define STBTT_free(x,u)    ((void)(u), free(x))
#endif

/* ---- 修正一：64 位类型宽度 ---- */
#if defined(__TINYC__) && defined(_WIN64)
#  ifndef NK_SIZE_TYPE
#    define NK_SIZE_TYPE unsigned long long
#  endif
#  ifndef NK_POINTER_TYPE
#    define NK_POINTER_TYPE unsigned long long
#  endif
#endif

/* ---- 修正二：推荐配置（缺省打开，可用 -D 覆盖）----
 *
 * NK_INCLUDE_FONT_BAKING  启用字体烘焙。Nuklear 自带 stb_truetype 与
 *                         stb_rect_pack 的实现，不需要额外文件。
 * NK_INCLUDE_DEFAULT_FONT 内置 ProggyClean 位图字体，无需外部 .ttf
 *                         就能显示文字 —— 对"单文件零依赖"很关键。
 * NK_INCLUDE_DEFAULT_ALLOCATOR  用 malloc/free，无需自备分配器。
 *
 * 想关掉就在编译命令里加 -DNK_NO_AUTO_CONFIG 并自行定义这些宏。 */
#ifndef NK_NO_AUTO_CONFIG
#  ifndef NK_INCLUDE_FONT_BAKING
#    define NK_INCLUDE_FONT_BAKING
#  endif
#  ifndef NK_INCLUDE_DEFAULT_FONT
#    define NK_INCLUDE_DEFAULT_FONT
#  endif
#  ifndef NK_INCLUDE_DEFAULT_ALLOCATOR
#    define NK_INCLUDE_DEFAULT_ALLOCATOR
#  endif
#endif

#endif /* NUKLEAR_TCC_COMPAT_H */