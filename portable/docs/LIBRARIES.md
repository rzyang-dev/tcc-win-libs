# 库清单

全部 18 个库都经过**实测编译 + 功能验证**，不是"列表里看着不错"。

- 验证环境：TCC 0.9.27 win64，Windows 10 22H2
- 用途：便携工具开发（单文件、零依赖）
- 选取标准：纯 C、宽松许可、与 TCC 兼容、填实际缺口

---

## 快速索引

| 类别 | 库 | `-With` 名字 |
|---|---|---|
| 数据结构 | stb_ds | `stb_ds` |
| JSON | cJSON / jsmn | `cjson` / `jsmn` |
| 配置 | inih | `inih` |
| 文本 | utf8.h / stb_sprintf | `utf8` / `stb_sprintf` |
| 计算 | tinyexpr | `tinyexpr` |
| 数学 | linmath.h | `linmath` |
| 哈希 | xxHash / lonesha256 | `xxhash` / `sha256` |
| 压缩 | miniz | `miniz` |
| 文件 | tinydir | `tinydir` |
| 图片 | stb_image / stb_image_write | `stb_image` / `stb_image_write` |
| 图形 | stb_rect_pack | `stb_rect_pack` |
| 解析 | stb_c_lexer | `stb_c_lexer` |
| 日志 | log.c | `log.c` |
| 参数 | argparse | `argparse` |
| 脚本 | Lua 5.5 | `lua` |

---

## 数据结构

### stb_ds — 动态数组 + 哈希表

**填补 C 标准库最根本的空白**：C 没有任何容器。

| 项 | 值 |
|---|---|
| 来源 | `nothings/stb` |
| 文件 | `include/stb_ds.h`（1 个） |
| 许可 | 公有领域 |
| 宏 | 需 `#define STB_DS_IMPLEMENTATION` |
| 验证 | 动态数组增删、字符串键哈希增删改查、结构体值，全部通过 |

```c
#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"

int *arr = NULL;
for (int i = 1; i <= 10; i++) arrput(arr, i * i);
arrins(arr, 0, 999);          /* 头插 */
arrdel(arr, 0);
arrfree(arr);

struct { char *key; int value; } *map = NULL;
shput(map, "lua", 5);
shput(map, "tcc", 27);
printf("%d\n", shget(map, "lua"));    /* 5 */
shdel(map, "lua");
shfree(map);
```

**实测性能**：5 万次插入 + 5 万次查找，C 手写开放寻址 2.3 ms，Lua table 8.1 ms
（stb_ds 与手写同级）。

---

## JSON

### cJSON — 解析与生成

| 项 | 值 |
|---|---|
| 来源 | `DaveGamble/cJSON` |
| 文件 | `lib/cjson/`（2 个） |
| 许可 | MIT |
| 宏 | `-DCJSON_HIDE_SYMBOLS`（**必需**，见下） |
| 验证 | 解析嵌套、生成、往返一致性全部通过 |

**为什么必须 `CJSON_HIDE_SYMBOLS`**：cJSON 默认给所有公开函数加
`__declspec(dllexport)`，而 TCC 0.9.27 **无法解析原型位置的 `__declspec`**：

```
error: declaration for parameter 'cJSON_Version' but no such parameter
```

`-With cjson` 已自动加上此宏。

```c
#include "cJSON.h"

cJSON *j = cJSON_Parse("{\"a\":[1,2,3],\"b\":true}");
printf("%d\n", cJSON_GetArraySize(cJSON_GetObjectItem(j, "a")));
cJSON_Delete(j);

cJSON *o = cJSON_CreateObject();
cJSON_AddStringToObject(o, "name", "tool");
cJSON_AddNumberToObject(o, "size", 1234);
char *s = cJSON_PrintUnformatted(o);
cJSON_free(s); cJSON_Delete(o);
```

### jsmn — 极简 JSON 分词器

零分配，只给 token 边界，适合超大 JSON 或流式场景。

| 项 | 值 |
|---|---|
| 来源 | `zserge/jsmn` |
| 文件 | `include/jsmn.h` |
| 许可 | MIT |
| 宏 | 无 |
| 验证 | 嵌套对象/数组正确切分出 12 个 token |

```c
jsmn_parser p; jsmntok_t tok[64];
jsmn_init(&p);
int r = jsmn_parse(&p, json, strlen(json), tok, 64);
/* tok[i].type / .start / .end / .size */
```

---

## 配置 / 文本

### inih — .ini 解析

| 项 | 值 |
|---|---|
| 来源 | `benhoyt/inih` |
| 文件 | `lib/inih/`（2 个） |
| 许可 | BSD-3 |
| 宏 | 无 |
| 验证 | 文件解析与字符串解析两个接口，多 section、注释、int/string/float 全部正确 |

```c
static int handler(void *ud, const char *sec, const char *key, const char *val) {
    if (!strcmp(key, "width")) *(int*)ud = atoi(val);
    return 1;                       /* 返回 0 会中止解析 */
}
int rc = ini_parse("app.ini", handler, &cfg);        /* 0 = 成功 */
int rc2 = ini_parse_string(text, handler, &cfg);     /* 从内存解析 */
```

**注意**：不要在回调里用 `strncpy` 后忘记补 NUL；也不要在同一个
`printf` 的格式串里混入被编码破坏的字符（会输出未初始化内存，
表现为"值每次运行都不同"）。

### utf8.h — Unicode 码点操作

| 项 | 值 |
|---|---|
| 来源 | `sheredom/utf8.h` |
| 文件 | `include/utf8.h` |
| 许可 | 公有领域 |
| 宏 | 无（函数是 `utf8_weak` 内联） |
| 验证 | 16 字节 → 8 码点，编解码往返，合法性校验 |

**API 名字容易记错**：是 `utf8len` / `utf8size` / `utf8codepoint` / `utf8catcodepoint`，
**不是** `utf8_strlen` 那一套。

```c
utf8len("héllo→世界")        /* 8  —— 码点数 */
utf8size("héllo→世界")       /* 16 —— 字节数 */
utf8valid(s)                 /* 0 = 合法；非 0 = 出错位置指针 */

char buf[16]; char *w = buf;
w = utf8catcodepoint(w, 0x4E16, 4);   /* 注意接住返回值 */
w = utf8catcodepoint(w, 0x754C, 4);
```

**坑**：`utf8catcodepoint` 返回**前进后的指针**，忽略返回值会导致第二个码点覆盖第一个。

### stb_sprintf — 可靠 snprintf

标准 printf 没有的能力，且截断行为安全。

| 项 | 值 |
|---|---|
| 来源 | `nothings/stb` |
| 文件 | `include/stb_sprintf.h` |
| 许可 | 公有领域 |
| 宏 | 需 `#define STB_SPRINTF_IMPLEMENTATION` |
| 验证 | 基础格式与 C 库一致；千分位 `%'d`、二进制 `%b`、截断返回值全部正确 |

```c
char buf[64];
stbsp_snprintf(buf, sizeof buf, "%'d", 1234567);   /* "1,234,567" */
stbsp_snprintf(buf, sizeof buf, "%b", 0xAB);       /* "10101011"   */
```

---

## 计算 / 数学

### tinyexpr — 数学表达式求值

| 项 | 值 |
|---|---|
| 来源 | `codeplea/tinyexpr` |
| 文件 | `lib/tinyexpr/`（2 个） |
| 许可 | zlib |
| 宏 | 无 |
| 验证 | 8/8 表达式正确（含 `^`、`sqrt`、`abs`、比较、除零得 inf）；变量绑定与重求值正确 |

```c
int err;
double v = te_interp("(1+2)*3 + sqrt(144)", &err);   /* 21 */

double x = 3, y = 4;
te_variable vars[] = {{"x", &x}, {"y", &y}};
te_expr *e = te_compile("sqrt(x*x+y*y)", vars, 2, &err);
printf("%g\n", te_eval(e));   /* 5 —— 变量改了再 te_eval 得到新值 */
te_free(e);
```

### linmath.h — 向量 / 矩阵 / 四元数

| 项 | 值 |
|---|---|
| 来源 | `datenwolf/linmath.h` |
| 文件 | `include/linmath.h` |
| 许可 | WTFPL2 |
| 宏 | 无，但**必须搭配 `tccmath.h`** |
| 验证 | 76/76：矩阵乘法、求逆（与手算值对照）、叉乘、正交基、归一化 |

```c
#include "tccwin.h"
#include "tccmath.h"      /* linmath 用到 fabsf/sqrtf，见坑 P5 */
#include "linmath.h"

vec3 a = {3,4,0};
printf("%f\n", vec3_len(a));         /* 5 */

mat4x4 m, inv, prod;
mat4x4_identity(m);
mat4x4_translate_in_place(m, 1, 2, 3);
mat4x4_invert(inv, m);
mat4x4_mul(prod, m, inv);            /* 应为单位阵 */
```

**实测**：`M * M⁻¹` 与单位阵总误差 `0.00000000`。

---

## 哈希

### xxHash — XXH32 / XXH64

| 项 | 值 |
|---|---|
| 来源 | `Cyan4973/xxHash` |
| 文件 | `include/xxhash.h` |
| 许可 | BSD-2 |
| 宏 | `-DXXH_NO_XXH3 -DXXH_STATIC_LINKING_ONLY`（`-With xxhash` 自动加 `XXH_NO_XXH3`） |
| 验证 | 已知测试向量全部匹配；流式 API 与一次性哈希一致 |

**为什么必须关掉 XXH3**：TCC 不提供任何 SIMD intrinsic 头，XXH3 要 `__m128i`：

```
error: '__m128i' undeclared
```

想用流式 API（`XXH32_createState`）必须加 `XXH_STATIC_LINKING_ONLY`，
否则 `XXH32_state_t` 是不完整类型。

```c
#define XXH_NO_XXH3
#define XXH_STATIC_LINKING_ONLY
#define XXH_IMPLEMENTATION
#include "xxhash.h"

unsigned h = XXH32(data, len, 0);
unsigned long long h64 = XXH64(data, len, 0);
```

### lonesha256 — SHA-256

| 项 | 值 |
|---|---|
| 来源 | `BareRose/lonesha256` |
| 文件 | `include/lonesha256.h`（头文件名是 lonesha256.h） |
| 许可 | 公有领域 |
| 宏 | 需 `#define LONESHA256_IMPLEMENTATION` |
| 验证 | `SHA256("abc")` 与标准向量**逐位一致**；并与 Windows 内置 `Get-FileHash` 对同一批文件比对通过 |

```c
#define LONESHA256_IMPLEMENTATION
#include "lonesha256.h"

unsigned char digest[32];
lonesha256(digest, (const unsigned char *)data, len);   /* 返回 0 = 成功 */
```

---

## 压缩

### miniz — DEFLATE / ZIP / CRC32

| 项 | 值 |
|---|---|
| 来源 | `richgel999/miniz` |
| 文件 | `lib/miniz/`（**11 个**，不是单文件！） |
| 许可 | MIT |
| 宏 | 无，但必须编译 `miniz.c` + `miniz_tdef.c` + `miniz_tinfl.c` + `miniz_zip.c` |
| 验证 | 4096 → 46 字节（1.1%），解压逐字节一致；CRC32 确定性；内存 ZIP 归档读写 |

**注意**：`single_file_libs` 列表标注它是 1 个文件，**已经过时**。
实测需要 11 个文件（含 `miniz_export.h` —— 该文件由 CMake 生成，
上游仓库**没有**，直接下载会拿到 404 页面）。

**解压缓冲区不能用固定倍数**：压缩率可达 0.8%，按压缩后大小乘倍数会严重低估，
报 `MZ_BUF_ERROR (-5)`。用流式 `mz_inflate` 分块扩容。

```c
mz_ulong cap = mz_compressBound(n);
mz_compress(out, &cap, src, n);            /* 4 个参数，没有压缩级别参数 */

/* 流式解压（输出大小未知） */
mz_stream st; memset(&st, 0, sizeof st);
mz_inflateInit(&st);
/* 循环 mz_inflate，每次扩容，直到 MZ_STREAM_END */
```

---

## 文件

### tinydir — 目录遍历

| 项 | 值 |
|---|---|
| 来源 | `cxong/tinydir` |
| 文件 | `include/tinydir.h` |
| 许可 | BSD-2 |
| 宏 | 无 |
| 验证 | 遍历、排序遍历、扩展名过滤、单文件打开全部通过 |

**本环境打了两个补丁**（原版在 Windows 上有问题）：

1. 原版用 `#ifdef _MSC_VER` 判断 Windows。TCC/MinGW 不定义它，
   于是走 POSIX 分支去 `#include <dirent.h>` / `<libgen.h>`，而 TCC 自带头集里
   这两个头不可用 →
   改成 `#if defined(_MSC_VER) || defined(_WIN32)`

   > **此处曾写成"Windows 上不存在"，不准确。** 实测结论是"这条路在本工具链上
   > 走不通"，但两个头的原因不同：
   >
   > - `dirent.h` —— TCC **有**这个头，但它声明的 `opendir` / `closedir`
   >   **链接失败**（`undefined symbol`，msvcrt 里没有实现）。
   >   所以**"有头"不等于"能用"**。
   > - `libgen.h` —— TCC **没有**（只在 `tool/tcc/winapi-full-for-0.9.27/` 里可取到）。
   >
   > 结论（要打补丁）不变，理由要改对：不是"Windows 平台没有这两个头"，
   > 而是"TCC 自带头集里这两个头不可用 —— 一个缺实现、一个缺头"。
   >
   > **补充（后续轮次）**：`dirent.h` 这个缺口**已经修好了** ——
   > `helper/dirent_compat.c` 用 msvcrt 真正有导出的 `_findfirst` /
   > `_findnext` / `_findclose` 补齐了 `opendir` / `readdir` / `closedir` /
   > `rewinddir` / `telldir` / `seekdir`。用 `-With dirent` 启用，
   > 验证见 `examples/probes/probe_dirent.c`（24 项）与 PITFALLS P68。
   > 宽字符版（`_wopendir` 等 6 个）是**真实死路**，原因见 P68。
2. 即使走对分支，原版在 Windows 下**不暴露文件大小**。
   → 新增 `_size` 字段，从 `WIN32_FIND_DATA` 填充。

```c
tinydir_dir dir;
tinydir_open(&dir, ".");
while (dir.has_next) {
    tinydir_file f;
    tinydir_readfile(&dir, &f);
    if (!f.is_dir)
        printf("%-30s %10llu  %s\n", f.name, f._size,   /* ← 用 _size */
               f.extension ? f.extension : "");
    tinydir_next(&dir);
}
tinydir_close(&dir);
```

---

## 图片

### stb_image / stb_image_write

| 项 | 值 |
|---|---|
| 来源 | `nothings/stb` |
| 文件 | `include/stb_image.h`、`include/stb_image_write.h` |
| 许可 | 公有领域 / MIT |
| 宏 | `-DSTBI_NO_SIMD`（`-With stb_image` 自动加）；write 不需要宏 |
| 验证 | 解码 29/29、编码 23/23；PNG 往返**像素完全一致** |

**为什么必须 `STBI_NO_SIMD`**：stb_image 只为 `__GNUC__` + 32 位自动关闭 SSE2，
x64 下无条件 `#include <emmintrin.h>`，而 TCC 没有任何 intrinsic 头：

```
stb_image.h:726: error: include file 'emmintrin.h' not found
```

支持格式（实测）：PNG(RGB/RGBA/调色板)、JPEG、BMP、TGA、GIF、PSD、HDR、PIC、PNM。

```c
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_SIMD
#include "stb_image.h"

int w, h, comp;
unsigned char *px = stbi_load("in.png", &w, &h, &comp, 4);
stbi_image_free(px);

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
stbi_write_png("out.png", w, h, 4, px, w * 4);
stbi_write_jpg("out.jpg", w, h, 3, px, 90);   /* 末参数 = 质量 */
```

### stb_rect_pack — 矩形打包

| 项 | 值 |
|---|---|
| 来源 | `nothings/stb` |
| 文件 | `include/stb_rect_pack.h` |
| 许可 | 公有领域 / MIT |
| 宏 | 需 `#define STB_RECT_PACK_IMPLEMENTATION` |
| 验证 | 11/11：64 个 32×32 完美装入 256×256，无重叠、全部落在 32px 边界 |

用于纹理图集。实测密集网格能精确装入。

---

## 解析

### stb_c_lexer — C 风格词法分析器

| 项 | 值 |
|---|---|
| 来源 | `nothings/stb` |
| 文件 | `include/stb_c_lexer.h` |
| 许可 | 公有领域 / MIT |
| 宏 | 需 `#define STB_C_LEXER_IMPLEMENTATION` |
| 验证 | 38/38（TCC 与 MinGW gcc 结果一致） |

**本环境已打一行上游 bug 修复**（字符字面量位于缓冲区末尾时越界）。详见 [`PITFALLS.md`](PITFALLS.md) P38。

---

## 日志 / 参数 / 脚本

### log.c — 分级日志

| 项 | 值 |
|---|---|
| 来源 | `rxi/log.c` |
| 文件 | `lib/log.c/`（2 个） |
| 许可 | MIT |
| 宏 | 无 |
| 验证 | 级别过滤、时间戳、颜色、文件名行号全部正确 |

```c
log_set_level(LOG_INFO);
log_info("启动 v%d", 1);
log_error("失败: %s", reason);
```

### argparse — 命令行解析

| 项 | 值 |
|---|---|
| 来源 | `cofyc/argparse` |
| 文件 | `lib/argparse/`（2 个）+ `helper/msvcrt_compat.c` |
| 许可 | MIT |
| 宏 | 无（垫片自动一起编译） |
| 验证 | 布尔/整数/字符串选项、位置参数、`-h` 帮助全部正确 |

**垫片说明**：argparse 用 C99 的 `strtof`，msvcrt **没有**。
本环境提供 `helper/msvcrt_compat.c`（用 `strtod` + 收窄实现），
`-With argparse` 会自动一起编译，**不需要改动 argparse.c**。

```c
struct argparse_option opts[] = {
    OPT_HELP(),
    OPT_BOOLEAN('v', "verbose", &verbose, "详细输出"),
    OPT_INTEGER('c', "count",   &count,   "次数"),
    OPT_STRING ('n', "name",    &name,    "名称"),
    OPT_END(),
};
struct argparse ap;
argparse_init(&ap, opts, NULL, 0);
argc = argparse_parse(&ap, argc, argv);
```

### Lua 5.5 — 脚本引擎

| 项 | 值 |
|---|---|
| 文件 | `include/lua/`（头文件）、`toolchain/lua-src/`（全部源码） |
| 许可 | MIT |
| 验证 | 标准库 12 类全通过；静态解释器、脚本打包、C 库绑定全部可用 |

**官方 `lua55.exe` 不是自包含的** —— 单独拷出来运行报 `0xC0000135`（缺 `lua55.dll`）。
本环境提供两条路：

```cmd
REM 静态解释器（392 KB 单文件，零依赖）
scripts\build-lua.cmd -Interpreter     -> bin\slua.exe

REM 把脚本打包成 exe
scripts\build-lua.cmd -Script app.lua -Bytecode -With cjson,miniz
```

打包后 Lua 侧可访问 `C` 表：`C.json_encode/decode`、`C.gzip_compress/decompress`、`C.crc32`。

---

## 未纳入的候选

以下库在列表里，但**未纳入**，原因如下：

| 库 | 原因 |
|---|---|
| Nuklear（GUI） | 未测试。GUI 是最大缺口，但工作量大，需要单独一轮验证 |
| CivetWeb / Mongoose（HTTP） | Mongoose 正在测（编译验证）。HTTP 服务超出"便携工具"常见需求 |
| SQLite amalgamation | 2 文件可用，但不在 single_file_libs 列表里（该列表 FAQ 专门解释了原因）。需要时单独引入 |
| duktape / pocketpy / s7 等脚本引擎 | 与已有 Lua 功能重叠 |
| 大量 `math` / `3d` 类库 | 与当前需求（工具开发）无关 |
| GPL 许可的库 | 许可传染性，不适合作为通用基础库 |

---

## 验证方式

每个库的验证程序都在开发过程中实际运行过。想复验环境是否完好：

```cmd
scripts\tcc.cmd examples\01-hello.c -Run
scripts\tcc.cmd examples\02-winapi.c -Run
scripts\tcc.cmd examples\03-multilib.c -With tinydir,sha256,xxhash,cjson,stb_ds -Run -- .
scripts\tcc.cmd examples\04-image.c -With stb_image,stb_image_write,linmath -Run
scripts\build-lua.cmd -Interpreter
bin\slua.exe -e "print('lua ok')"
```

---

## 第二轮补充（子代理独立测试，231 项检查 0 失败）

### tomlc99 — TOML 配置解析

| 项 | 值 |
|---|---|
| 来源 | `cktan/tomlc99` |
| 文件 | `lib/toml/`（2 个） |
| 许可 | MIT |
| 宏 | 无（`helper/msvcrt_compat.c` 提供 `strtoll`/`strnlen`，自动一起编译） |
| 验证 | 54/54：字符串/整数/浮点/布尔/嵌套表/错误信息 |

```c
char errbuf[256];
FILE *f = fopen("app.toml", "r");
toml_table_t *root = toml_parse_file(f, errbuf, sizeof errbuf);
fclose(f);
if (!root) { printf("parse error: %s\n", errbuf); return 1; }

printf("count = %lld\n", (long long)toml_int_in(root, "count").u.i);
toml_table_t *win = toml_table_in(root, "window");
printf("width = %lld\n", (long long)toml_int_in(win, "width").u.i);
toml_free(root);
```

比 .ini 表达力强得多（嵌套表、数组、原生类型），且解析器只有 2 个文件。

### stb_c_lexer — C 风格词法分析器（已修补）

| 项 | 值 |
|---|---|
| 来源 | `nothings/stb` |
| 文件 | `include/stb_c_lexer.h` |
| 许可 | 公有领域 / MIT |
| 宏 | 无 |
| 验证 | 38/38（TCC 与 MinGW gcc 结果一致） |

**本环境已打一行上游 bug 修复**。原版在"字符字面量位于缓冲区末尾"时
会越界并产出垃圾 token。详见 [`PITFALLS.md`](PITFALLS.md) 的 P38。

### 未纳入：网络库

`mongoose`、`civetweb` 等网络库**未纳入**，原因是 **Winsock 头文件曾经缺（**已修**，见下）**

> **更正（实测）**：本条写作时 TCC 确实没有任何 Winsock 头。
> 整批纳入 `winapi-full-for-0.9.27` 之后已经补上，实测：
>
>     winapi\winsock2.h   存在
>     winapi\winsock.h    存在
>     winapi\ws2tcpip.h   存在
>     winapi\ws2spi.h     存在
>     ws2def.h            **不存在**（但 winsock2.h 自足，不影响）
>
> 且只用 TCC 自带头（**不加项目 -I**）+ `-L lib\tccdefs` 编下面这段
> 两个位宽都通过：
>
>     #include <winsock2.h>
>     #include <ws2tcpip.h>
>     int main(void){ WSADATA w; return sizeof(w) > 0 ? 0 : 1; }
>
> 所以「因为没有 Winsock 头而不纳入网络库」这个理由**不再成立**。
>
> ---
>
> **一条核对时的假警报，记下来免得后人重踩。**
>
> 核对过程中我用这样一个最小程序去问「`GetCPInfo` 是不是 TCC 自带」：
>
>     #include <windows.h>
>     int main(void){ CPINFO c; GetCPInfo(0,&c); ... }
>
> 结果两个位宽都报 **`'CPINFO' undeclared`**。看起来像"TCC 缺 CPINFO"，
> **但这是假警报** —— `CPINFO` 定义在 `winnls.h` 里，而
> `#include <windows.h>` **不会**把它拉进来（`tccwin.h` 才显式包含
> `<winnls.h>`）。加上那个 include 就能过。
>
> **教训**：用"只包含头 X 能否用"来判定能力时，要确认 X 真的会把目标
> 声明拉进来。TCC 的 `windows.h` 在这点上比 MSVC 的更"窄"——
> 这正是 `tccwin.h` 存在的原因之一（见 PITFALLS P61）。
>
> 所以：**`CPINFO` / `GetCPInfo` 可用，但需要 `<winnls.h>`。**
> 网络库未纳入的原因要重新评估（不是头文件问题）。
（见 P35）。它们需要手写 `winsock2.h` 垫片才能编译，而手写结构体的 ABI 正确性
只在**真实收发数据时**才暴露 —— 未经验证就打包是不负责任的。

需要网络能力时，建议：
1. 单独做一轮 Winsock 垫片验证（真实建立连接、收发数据）
2. 或改用别的工具链编译网络部分，本环境负责其余逻辑
---

## P1 已完成 · socket（网络能力）

| 项 | 值 |
|---|---|
| 来源 | **本环境自带垫片**（`include/winsock2.h`），非第三方库 |
| 为什么需要 | TCC 的 `include/` 下**没有任何** winsock 头文件 |
| 依赖 | 系统自带 `ws2_32.dll`（实测 195 个导出），无需额外文件 |
| 启用 | `-With socket`（自动追加 `-lws2_32`） |
| 验证 | 38/38，含**真实 TCP/UDP 收发**而非仅编译 |

```c
#include "tccwin.h"      /* 先 */
#include "winsock2.h"    /* 后 */

WSADATA wsa;
WSAStartup(MAKEWORD(2, 2), &wsa);

/* TCP 客户端 */
SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
struct sockaddr_in a;
memset(&a, 0, sizeof a);
a.sin_family = AF_INET;
a.sin_port = htons(80);
inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
connect(s, (struct sockaddr *)&a, sizeof a);

send(s, "GET / HTTP/1.0\r\n\r\n", 18, 0);
char buf[4096];
int n = recv(s, buf, sizeof buf, 0);

closesocket(s);
WSACleanup();
```

**已验证**：TCP/UDP 回环收发（内容逐字节比对）、错误码路径、
`getaddrinfo` DNS、`inet_pton`/`inet_ntop`、`select`、`fd_set` 宏。

**未验证边界**：`WSAAsyncSelect`/`WSAEventSelect`/`WSAPoll`、
重叠 I/O（Overlapped/IOCP）、原始套接字、多播、IPv6 实际收发。
需要这些能力时请先单独验证。

**结构体布局**已与 MSDN 交叉核对（`sockaddr_in6`=28、`sockaddr_storage`=128、
`WSADATA`=408、`fd_set`=520），详见 [`PITFALLS.md`](PITFALLS.md) P41。

### 可用于网络能力的第三方库

垫片就绪后，之前因缺 winsock 头而无法编译的库现在可以评估了：
mongoose、civetweb、zed_net、enet。

**但本环境暂不纳入它们** —— 垫片只验证了基础 socket 原语，
这些库还可能用到未覆盖的 API（如 `WSAEventSelect`、IOCP）。
纳入前需要针对该库做完整的真实收发验证。
---

## P1 已完成 · nuklear（GUI 窗口组件）

| 项 | 值 |
|---|---|
| 来源 | [Immediate-Mode-UI/Nuklear](https://github.com/Immediate-Mode-UI/Nuklear) v4.13.3 |
| 许可 | **公有领域**（public domain） |
| 文件 | `include/nuklear.h`（1.2 MB）+ 自带垫片 `include/nuklear_tcc_compat.h` |
| 依赖 | 系统 `gdi32`、`user32`；**自带 stb_truetype/stb_rect_pack 与位图字体** |
| 启用 | `-With nuklear` |
| 验证 | 43/43（几何、命中、状态、绘制命令、顶点缓冲） |

即时模式 GUI，控件与布局由库负责。**渲染后端需自行提供**
（GDI / OpenGL / 软件光栅）。

### 最小用法

```c
#include "tccwin.h"
#include "nuklear_tcc_compat.h"   /* 必须：修 nk_size + 开字体与分配器 */
#define NK_IMPLEMENTATION
#include "nuklear.h"

int main(void) {
    struct nk_font_atlas atlas;
    struct nk_context ctx;
    nk_font_atlas_init_default(&atlas);
    nk_font_atlas_begin(&atlas);
    struct nk_font *font = nk_font_atlas_add_default(&atlas, 13.0f, NULL);
    int w, h;
    nk_font_atlas_bake(&atlas, &w, &h, NK_FONT_ATLAS_RGBA32);
    nk_font_atlas_end(&atlas, nk_handle_id(0), NULL);
    nk_init_default(&ctx, &font->handle);

    /* 每帧 */
    nk_input_begin(&ctx);
        nk_input_motion(&ctx, mx, my);
        nk_input_button(&ctx, NK_BUTTON_LEFT, mx, my, mouse_down);
    nk_input_end(&ctx);

    nk_begin(&ctx, "窗口", nk_rect(0,0,400,300), NK_WINDOW_TITLE);
    nk_layout_row_dynamic(&ctx, 30, 1);
    if (nk_button_label(&ctx, "点我")) { /* ... */ }
    nk_end(&ctx);

    /* 取出绘制命令 -> 交给你的后端渲染 */
    const struct nk_command *cmd;
    nk_foreach(cmd, &ctx) { /* 画矩形/文本/线 */ }

    nk_clear(&ctx);          /* 必须 */
}
```

### 必须先读的坑

包含顺序、字体烘焙顺序、每帧顺序、按钮触发时机、
`nk_widget` 会推进布局、`nk_convert` 成功值是 0 ——
这些都会失败得很难反推。**动手前请读 [`PITFALLS.md`](PITFALLS.md) P42-P50。**

### 已验证范围

类型宽度、字体烘焙（真实位图 512x64 / 224 字形）、布局几何、
输入命中与完整点击序列、控件状态位、绘制命令与顶点缓冲输出。

### 未验证边界

- **不创建真实窗口**：探针验证的是 GUI 逻辑层。实际显示需自行接后端
- 文本输入、滚动条、下拉框、树、表格等复杂控件
- 多窗口、弹出层、上下文菜单
- **中文等非 ASCII 字形**：内置 ProggyClean 只有 ASCII，
  需自备 `.ttf` 并配置 `nk_font_config` 的 `range`

### 关于 GDI 后端

官方 `demo/gdi/nuklear_gdi.h` 存在，使用 Win32 GDI 做渲染。
**本环境暂未纳入** —— 它需要真实的窗口与消息循环，
把"能否显示"的验证变成需要人工观察的测试，与本环境
"全部自动化验证"的风格冲突。需要时可自行取用。
---

## P2 已完成 · sqlite（嵌入式数据库）

| 项 | 值 |
|---|---|
| 版本 | **SQLite 3.53.4** amalgamation（2026-07-24，官方原始包） |
| 许可 | **公有领域**（public domain） |
| 文件 | `lib/sqlite/`：`sqlite3.c`（9.5 MB）+ `sqlite3.h` + `sqlite3ext.h` + `sqlite3_tcc.c`（包装，含必需定义） |
| 依赖 | 无（纯 C，自带 VFS） |
| 启用 | `-With sqlite` |
| 验证 | 26/26（真实建库、事务、约束、持久化） |
| 编译 | **1.6 秒** / 2.2 MB 目标文件 |

**来源**：官方 `sqlite.org` 原始包（`sqlite-amalgamation-3530400.zip`），
未做任何改写。用 CPython 的 OpenSSL 下载（本机 Schannel 不可用，
见 `PITFALLS.md` P57）：

```powershell
py -c "import urllib.request,sys; open(sys.argv[2],'wb').write(urllib.request.urlopen(sys.argv[1],timeout=90).read())" `
   "https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip" "sq.zip"
```

升级步骤（换版本时）：解压取出 `sqlite3.c` / `sqlite3.h` / `sqlite3ext.h`
覆盖 `lib/sqlite/`，**保留自研的 `sqlite3_tcc.c`**，然后重跑
`scripts\verify.cmd`。新版本若引入 TCC 不支持的内建，
报错会出现在链接期（如 `__umulh`，见 P58）。
### 用法

```c
#include <stddef.h>   /* 顺序重要，见 PITFALLS P51 */
#include <stdint.h>
#include "tccwin.h"
#include "sqlite3.h"

sqlite3 *db;
sqlite3_open("app.db", &db);
sqlite3_exec(db, "CREATE TABLE t(a TEXT, b INT);", NULL, NULL, NULL);

sqlite3_stmt *st;
sqlite3_prepare_v2(db, "INSERT INTO t VALUES(?1, ?2);", -1, &st, NULL);
sqlite3_bind_text(st, 1, "hello", -1, SQLITE_TRANSIENT);
sqlite3_bind_int(st, 2, 42);
sqlite3_step(st);
sqlite3_finalize(st);
sqlite3_close(db);
```

**注意**：不要直接编译 `lib\sqlite\sqlite3.c`，用 `sqlite3_tcc.c`
（`-With sqlite` 已自动处理）。原因见 PITFALLS P51。

### 已验证范围

打开/关闭、建表、批量插入、预处理语句、参数绑定、结果集迭代、
回调式查询、BEGIN/ROLLBACK/COMMIT、UNIQUE 约束、错误码与
`sqlite3_errmsg`、**关闭后重开确认数据落盘**。

### 未验证边界

- 多线程模式：实测 `sqlite3_threadsafe()` 返回 **1（multi-thread）**。
  序列化模式的并发压测未做
- WAL 模式、VFS 自定义、扩展加载、JSON1/FTS5 等可选模块
- 大于内存的数据库、大 BLOB

---

## P2 已完成 · regex（正则表达式）

| 项 | 值 |
|---|---|
| 来源 | [kokke/tiny-regex-c](https://github.com/kokke/tiny-regex-c) |
| 许可 | **Unlicense / 公有领域** |
| 文件 | `lib/regex/re.c`（14 KB）+ `re.h`（1.8 KB） |
| 启用 | `-With regex` |
| 验证 | 56/56 通过 + 2 项已知限制 |

接口只有三个函数，返回**匹配起始下标**（-1 表示不匹配）：

```c
re_t re_compile(const char* pattern);
int  re_matchp(re_t pattern, const char* text, int* matchlength);
int  re_match(const char* pattern, const char* text, int* matchlength);
```

### 已验证支持的语法

字面量、`^` `$` 锚点、`[abc]` / `[a-z]` / `[^...]` 字符类、
`\d \D \w \W \s \S` 预定义类、`.` 点号、`* + ?` 量词、
`\.` `\*` 转义、大小写敏感、`re_compile` 后安全复用。

实测场景：C 标识符、浮点数、十六进制、邮箱形状、URL 前缀。

### 已知限制（本环境无法绕过，是库的设计）

- **不支持分支/交替 `|`** —— `re.c:135` 是注释掉的实现，
  尽管 README 把它列在"支持的运算符"里（文档与实现不一致，见 PITFALLS P54）
- **不支持捕获组**（没有分组提取）。返回值只有位置与长度

需要这两项时请换用其它正则库（如 PCRE2，但那是另一个量级的依赖）。

---

## P3 已完成 · 字体渲染（stb_truetype）

**不需要额外文件** —— Nuklear 已内置 stb_truetype 与 stb_rect_pack
的完整实现（`nuklear.h:6477-6486` 生效，1222 个 `stbtt_` 符号）。

| 项 | 值 |
|---|---|
| 来源 | Nuklear 内置（原 stb_truetype，公有领域） |
| 启用 | `-With nuklear` |
| 验证 | 24/24（含外部 TTF 的真实光栅化） |

### 已验证

- **内置 ProggyClean**：无需外部文件即可显示 ASCII 文字
- **外部 TTF 加载**：`stbtt_InitFont` / `GetFontVMetrics` /
  `FindGlyphIndex` / `ScaleForPixelHeight` / `GetFontBoundingBox`
- **真实光栅化**：`stbtt_GetCodepointBitmap` 对 arial.ttf 渲染 'A'
  得到 21x21 位图、40.8% 非零像素、最大灰度 255（完整抗锯齿）
- 度量正确性：'W'(27px) 宽于 'i'(4px)
- `nk_font_atlas_add_from_memory` 把外部 TTF 加入 Nuklear 图集

### 未验证边界

- **中文等非 ASCII 字形**：内置 ProggyClean 只有 ASCII。
  用外部 CJK 字体需配置 `nk_font_config` 的 `range`（图集会显著变大）
- 字体 hinting、kerning 的高级用法（`stbtt_GetCodepointKernAdvance`
  可用但只验证了不崩溃；arial 的 'A'+'V' 返回 0）
- 可变字体、彩色字体（COLR/CPAL）、位图字体（CBDT）

---

## P2 未完成 · readline（终端行编辑）

**结论：linenoise 无法在 Windows 上编译，需要移植。**

| 项 | 值 |
|---|---|
| 候选 | [antirez/linenoise](https://github.com/antirez/linenoise)（BSD-2-Clause） |
| 状态 | **未纳入** —— 平台不兼容 |

linenoise 是**纯 POSIX** 实现，依赖 `<termios.h>` 与 `<unistd.h>`。
实测两个编译器都失败：

```
TCC:        include file 'termios.h' not found
MinGW gcc:  fatal error: termios.h: No such file or directory
```

它是**平台问题，不是编译器问题** —— TCC 与 MinGW 都缺这些头。
README 里写的 "Win32 support" 只是 TODO，代码里没有任何 `_WIN32` 分支。

### 后续路径（未实施）

要补这个能力需要在二者中选一：

1. **移植 linenoise 的终端层到 Win32 控制台 API**
   （`SetConsoleMode` 关行缓冲、`ReadConsoleInput` 逐键读取、
   处理方向键/退格/Ctrl-C、把 `write()` 换成 `WriteConsoleA`）。
   核心编辑逻辑可保留，约 400-600 行。
   建议**仿照 Nuklear 的分层**：把"按键 → 行状态"做成纯逻辑可确定性测试，
   把控制台 I/O 隔离在薄层，这样验证不依赖交互式终端。
2. **自己写一个最小行编辑器** —— 功能只需历史、左右移动、退格、Home/End。

本环境当前的替代方案：直接用 `fgets` + 自己维护历史数组，
或在 Lua 里用 `C` 表配合简单的 `io.read`。

---

## P2 已完成 · readline（终端行编辑器，自研）

**没有采用第三方库** —— linenoise / readline / libedit 都是 POSIX 实现，
Windows 上无法编译（见下方"为什么不用 linenoise"）。Win32 也没有
等价的成熟单文件库，所以自研了一个最小实现。

| 项 | 值 |
|---|---|
| 文件 | `include/tcwinreadline.h` + `lib/readline/tcwinreadline.c`（约 400 行） |
| 许可 | 本环境自带（无第三方代码） |
| 依赖 | 仅 Win32 控制台 API |
| 启用 | `-With readline` |
| 验证 | **94/94**（含控制台按键翻译层） |

### 设计：分两层，核心逻辑可确定性测试

这是本次能在无交互终端的情况下把行编辑器**测透**的关键：

```
第一层  tcwin_rl_feed(rl, 按键事件)   → 纯状态机，不碰任何 I/O
第二层  tcwin_rl_readline(rl, out, n) → Win32 控制台驱动
        tcwin_rl_translate_key(rec,ev) → INPUT_RECORD → 按键事件
```

- **第一层**输入是按键枚举、输出是对缓冲区的修改。测试可以随意喂
  按键序列并断言缓冲区内容与光标位置，完全确定、不需要终端。
- **第二层**里唯一有分支逻辑的是 `tcwin_rl_translate_key`
  （虚拟键码映射、Ctrl 组合判定、抬起/非按键过滤），
  所以把它做成**公开 API**，用构造的 `INPUT_RECORD` 测试。

实测 94 项通过，覆盖：光标移动与边界、中间插入、Backspace/Delete、
Ctrl-A/B/D/E/F/K/L/U/W、历史环形缓冲与浏览（含"↓ 越过最新一条恢复
原编辑内容"）、缓冲上限、控制字符过滤、按键翻译全部分支、
以及"翻译 → 编辑"的端到端串联。

### 用法

```c
#include "tccwin.h"
#include "tcwinreadline.h"

tcwin_rl_t rl;
char line[512];
int n;

tcwin_rl_init(&rl, "tcwin> ");
for (;;) {
    n = tcwin_rl_readline(&rl, line, sizeof line);
    if (n < 0) break;              /* Ctrl-C 或 Ctrl-D */
    if (n == 0) continue;          /* 空行 */
    tcwin_rl_history_add(&rl, line); /* 计入历史，↑ 立刻可翻到 */
    /* 处理 line ... */
}
tcwin_rl_free(&rl);
```

完整可运行示例见 [`examples/05-repl.c`](../examples/05-repl.c)
（一个带历史与表达式求值的交互式 shell）。

### 支持的按键

| 按键 | 作用 |
|---|---|
| 可打印字符 | 插入到光标处 |
| ← / → | 光标左/右移 |
| Home / End | 行首/行尾 |
| Backspace / Delete | 删除光标左侧 / 光标处字符 |
| Ctrl-A / Ctrl-E | 行首 / 行尾 |
| Ctrl-B / Ctrl-F | 光标左 / 右移 |
| Ctrl-U / Ctrl-K | 删除光标左侧 / 右侧全部 |
| Ctrl-W | 删除光标左侧一个词 |
| Ctrl-L | 清屏重绘 |
| ↑ / ↓ | 历史上下翻 |
| Enter | 提交 |
| Ctrl-C | 中断（返回 -1） |
| Ctrl-D | 空行时 EOF；非空时删除光标处字符 |

### 非交互输入自动退化

输入被重定向（管道 / 文件）时 `GetConsoleMode` 会失败，
`tcwin_rl_readline` 自动退化为 `fgets` 逐行读取 ——
此时没有控制台按键可读，行编辑本来就无意义。
这让同一个程序既能交互使用又能被脚本驱动：

```cmd
echo calc 1+1 | bin\05-repl.exe
```

### 未验证边界（重要）

- **交互式控制台路径（真实按键、真实重绘）未自动化验证** ——
  它需要人坐在终端前操作，与本环境"全部自动化验证"的风格冲突。
  已验证的是：核心编辑逻辑（94 项）、按键翻译（全分支）、
  以及非交互退化路径（管道驱动跑通 REPL）。
  实际重绘效果请用 `bin\05-repl.exe` 手动确认
- **只支持单字节字符集**。UTF-8 输入会把每个字节当一个字符，
  多字节字符的光标定位与删除会不准
- 没有 Tab 补全、多行编辑、括号匹配、语法高亮
- 长行超过窗口宽度时重绘只按单行处理，不折行（`\r` 覆盖法）
- 只适用于传统 Windows 控制台；ConPTY / MSYS 终端未测

### 为什么不用 linenoise

[antirez/linenoise](https://github.com/antirez/linenoise) 是 BSD-2-Clause
的优秀实现，但**纯 POSIX**，依赖 `<termios.h>` 与 `<unistd.h>`。
实测两个编译器都失败：

```
TCC:        include file 'termios.h' not found
MinGW gcc:  fatal error: termios.h: No such file or directory
```

这是**平台问题不是编译器问题**。README 里写的 "Win32 support" 只是 TODO，
代码里没有任何 `_WIN32` 分支。移植它约需 400-600 行，
与自研一个最小实现的成本相当，而自研可以做到分层可测 ——
所以选择了自研。将来若需要更完整的功能（补全、多行、UTF-8），
仍可考虑移植 linenoise 的编辑逻辑（那部分是纯字节操作，可以保留）。
### 许可（澄清）

SQLite 代码是**公有领域**（public domain），`sqlite3.h` 开头有官方声明：

```
** a legal notice, here is a blessing:
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
```

本环境用的是官方原始包，`lib/sqlite/` 里**没有**第三方 LICENSE 文件 ——
早先用镜像仓库时曾带入过打包者的 BSD-3 许可，升级到官方包后已移除。

`sqlite3.h` 开头就有 SQLite 官方的公有领域声明：

```
** a legal notice, here is a blessing:
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
```

保留那个镜像 LICENSE 文件只是为了**如实记录来源**（我们从该镜像取的包），
不是因为它对 SQLite 代码有任何约束。
两个许可都是宽松的，本项目不含任何 GPL 代码。
