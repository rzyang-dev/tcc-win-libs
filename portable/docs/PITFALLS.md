# 工具链已知坑与修法

本文件记录 TCC 0.9.27 + Lua 5.5 在 Windows 上的**全部已知问题**，
每条都经过实测定位（不是推测）。按"会不会静默出错"排序。

---

## 严重级：会静默出错（编译通过但结果错误）

### P1. `__declspec(dllexport)` 在函数原型上无法解析

TCC 0.9.27 解析不了原型位置的 `__declspec`：

```c
__declspec(dllexport) int g(void);        /* error: declaration for parameter 'g' but no such parameter */
__declspec(dllexport) const char* f(void);/* 同样失败 */
```

**影响**：cJSON 默认给所有公开函数加 `dllexport`，导致连 `#include "cJSON.h"` 都过不去。

**修法**：
- 用 cJSON：加 `-DCJSON_HIDE_SYMBOLS`（本环境的 `-With cjson` 已自动加）
- 自己写 DLL：把 `dllexport` 加在**函数定义**上，不要加在原型上

```c
/* 不要：__declspec(dllexport) int luaopen_mymod(luaState*); */
/* 要：  */
__declspec(dllexport) int luaopen_mymod(lua_State *L) { ... }
```

### P2. 缺少原型时 `GetTickCount64` 返回垃圾值

这是最危险的一条。不带原型调用 64 位返回值函数，TCC 按 `int` 处理返回值，
编译**毫无警告**，运行时输出错误数字：

```
uptime=18446744071675329820      ← 未声明原型时的典型垃圾输出
```

**修法**：包含 `tccwin.h`（它补齐了 27 个现代 API 的原型）。

---

## 严重级：硬性阻塞（无法链接）

### P3. 自带导入库是 2006 年的

| DLL | TCC 自带 | 重新生成后 |
|---|---|---|
| kernel32 | 767 | **1636** |
| user32 | 655 | **1005** |
| gdi32 | 334 | **973** |

抽查 25 个现代 API：旧库能链接 **0** 个，新库 **21** 个。

**注意：不是所有"现代 API 用不了"都是导入库的问题。实测区分出两种机制**
（方法：`tcc.exe` 不加 `-I`，分别在带/不带 `-L` 的条件下测编译+链接）：

| 函数 | 不带 `-L` | 带 `-L lib\tccdefs` | 真正原因 |
|---|---|---|---|
| `GlobalMemoryStatusEx` | 失败 | **可用** | **只是缺导入库** —— 头里有声明，`-L` 就解决 |
| `GetTickCount64` | 失败 | **仍失败** | **头文件里没声明** —— Vista API，2009 年的头文件集没有 |
| `InitializeCriticalSectionEx` | 失败 | **仍失败** | 同上（Vista API） |
| `GetActiveProcessorCount` | 失败 | **仍失败** | 同上 |
| `GetSystemTimePreciseAsFileTime` | 失败 | 失败 | 同上（Win8 API） |

> **此前本条把 `GetTickCount64` 和 `GlobalMemoryStatusEx` 并列成"无法链接"，
> 那是不准确的。** 前者无论怎么加 `-L` 都不行，必须补声明（Vista/7 API，
> 2009 年的头文件集里没有）；后者加 `-L` 即可。
> 两者机制不同，修法也不同 —— 混为一谈会让人以为"重新生成导入库就够了"。

**本环境已预置** `lib/tccdefs/`（25 个 DLL 的重新生成版本），
`scripts/tcc.cmd` 自动带上 `-L`。若要自己重新生成：

```cmd
toolchain\tcc\tcc.exe -impdef C:\Windows\System32\kernel32.dll -o kernel32.def
```

32 位需用 `lib/tccdefs32/`（从 `%WINDIR%\SysWOW64\` 的 32 位 DLL 生成），
见 P60。

### P4. `Interlocked*` 系列：**64 位缺 2 个，32 位缺 3 个**

`winbase.h:927`（x86_64 分支）和 `winnt.h`（`_AMD64_` 分支）把所有
`Interlocked*` 名字宏改写成编译器内建拼写：

```c
#define InterlockedExchangeAdd _InterlockedExchangeAdd
```

但 TCC 只为其中一部分提供内建，且**没有任何系统 DLL 导出**这些名字。
实测结果（`tcc.exe` / `i386-win32-tcc.exe`，不加 `-I`，链接带 `-L`）：

| 操作 | 64 位 | 32 位 |
|---|---|---|
| `InterlockedIncrement` / `Decrement` | 可用 | **可用** |
| `InterlockedExchange` | 可用 | **可用** |
| `InterlockedCompareExchange` | 可用 | **可用** |
| `InterlockedOr` | 可用 | **失败** |
| `InterlockedAnd` | 可用 | **失败** |
| `InterlockedXor` | 可用 | **失败** |
| `InterlockedExchangeAdd` | **失败** | **可用** |
| `InterlockedExchangeAdd64` | **失败** | **可用** |
| `InterlockedIncrement64` | 可用 | **可用** |
| `InterlockedCompareExchange64` | 可用 | **可用** |
| `InterlockedExchangePointer` | 可用 | **可用** |
| `InterlockedCompareExchangePointer` | 可用 | **可用** |

> **本条曾写成"32 位下 14 个全废"，那是错的。** 实测 32 位下 12 个里 **9 个可用**。
> 完整矩阵见 [`research/TCC-BASELINE.md`](research/TCC-BASELINE.md)。

**真实缺口只有 5 格**，且两边的失败信息形态不同：

```
64 位:  undefined symbol '_InterlockedExchangeAdd'      <- 有前导下划线
        undefined symbol '_InterlockedExchangeAdd64'
32 位:  undefined symbol 'InterlockedOr'                <- 无前导下划线
        undefined symbol 'InterlockedAnd'
        undefined symbol 'InterlockedXor'
```

前导下划线的差别说明两者机制不同（64 位是宏改写成内建名后内建缺失；
32 位是根本走不到内建路径）。

**修法**：`tccwin.h` 已处理 —— 但只需覆盖那 5 格，不是"全部 14 个"。
- 64 位：`#undef` 宏后用 `InterlockedCompareExchange` 实现 CAS 循环
- 32 位：用 `lock xaddl` / `lock orl` 等内联汇编补齐 `Or`/`And`/`Xor`

原子性已用 8 线程 × 20 万次竞争验证（对照组无保护变量每次都丢失 30 万次以上更新）。

### P5. float 数学函数：**只有 4 个不可用**（不是 16 个）

> **本条曾在多个文档里被写成"全部 float 数学函数不可用（16 个）"，那是错的。**
> 2024 轮用正确方法（`tcc.exe` **不加任何 `-I`**、链接**带 `-L lib\tccdefs`**、
> 同时过编译与链接）逐一重测 30 个函数，结论如下。
> 完整矩阵见 [`research/TCC-BASELINE.md`](research/TCC-BASELINE.md)。

**实测只有 4 个不可用**：

| 函数 | 失败信息 | 机制 |
|---|---|---|
| `fabsf` | `unknown constraint 't'` | `math.h:214` 用 x87 内联汇编约束 `"t"`，TCC x86_64 不支持 |
| `truncf` | `undefined symbol 'truncf'` | CRT 里没有实现 |
| `cbrt`（float 版） | `undefined symbol 'cbrt'` | CRT 里没有实现 |
| `nearbyintf` | `undefined symbol 'nearbyintf'` | CRT 里没有实现 |

**注意这 4 个是两种不同机制** —— `fabsf` 是**编译器约束**问题，
另 3 个是**缺实现**。不要混为一谈：改 `math.h` 的守卫条件只能修 `fabsf`。

**实测可用的 26 个**（编译+链接都过）：

```
sqrtf sinf cosf tanf expf logf log10f powf floorf ceilf
fmodf atan2f hypotf sinhf coshf tanhf asinf acosf atanf
roundf copysignf fminf fmaxf ldexpf frexpf modff
```

以及全部双精度版本（`sqrt` `sin` `cos` `pow` `fabs` `fmod` `atan2` …）。

**修法**：`#include "tccmath.h"`。该头**仍然必要**，但只需覆盖那 4 个函数 ——
早期版本把它写成覆盖 16 个，属于把 4 个真实缺口夸大成了 16 个。


### P6. 不能加 `-lm`

Windows 上数学函数在 CRT 里：

```
tcc: error: library 'm' not found
```

LuaRocks 的 MinGW 构建路径对每个 C 模块无条件追加 `-lm`，
这就是它在这套工具链上跑不通的第二步。

### P7. C99 的 `strtof` / `strtoll` / `strtoull` 在 msvcrt 里不存在

argparse 用 `strtof` 解析 float 选项。

**实测确认**（`tcc.exe` 不加 `-I`，链接带 `-L lib\tccdefs`，编译+链接都过才算可用）：

| 函数 | 结果 |
|---|---|
| `strtof` | **缺** |
| `strtoll` | **缺** |
| `strtoull` | **缺** |
| `strnlen` | **缺** |
| `setlocale` | 可用（TCC 自带） |

**修法**：本环境提供 `helper/msvcrt_compat.c` + `helper/msvcrt_compat.h`。

> 此前本条（以及 `LIBRARIES.md`）写的是 `argparse_msvcrt_compat.c`，
> **文件名不对** —— 实际实现文件是 `helper/msvcrt_compat.c`。
> 目录里那个 `argparse_msvcrt_compat.c` 只是一行注释的**残留文件**，
> 内容是"保留此文件名以兼容既有引用，实现已合并到 msvcrt_compat.c"。

**未验证边界**：`msvcrt_compat.c` 目前只在 `-With argparse` / `-With toml`
时通过 `-include` 生效，**不是全局可用**。直接调 `strtoll` 的程序仍需
自己 `#include "msvcrt_compat.h"` 并链接该源文件。是否提到 `tccwin.h`
使其全局生效，待定（见 ROADMAP A3）。

---

## 中等：需要特定编译开关

### P8. SIMD intrinsics 完全不存在

TCC 不提供 `emmintrin.h` 等任何 intrinsic 头，也不认 `__m128i`。

| 库 | 需要的开关 |
|---|---|
| xxHash | `-DXXH_NO_XXH3`（否则报 `'__m128i' undeclared`） |
| stb_image | `-DSTBI_NO_SIMD`（否则报 `include file 'emmintrin.h' not found`） |

### P9. 头文件库常需要 `*_IMPLEMENTATION` 宏

C 的单文件库普遍是"声明 + 实现"放同一个文件，靠宏控制实现部分。
用错会得到 `undefined symbol`：

| 库 | 需要的宏 |
|---|---|
| stb_ds | `#define STB_DS_IMPLEMENTATION` |
| stb_sprintf | `#define STB_SPRINTF_IMPLEMENTATION` |
| xxhash | `#define XXH_IMPLEMENTATION`（+ `XXH_STATIC_LINKING_ONLY` 才能用流式 API） |
| lonesha256 | `#define LONESHA256_IMPLEMENTATION` |
| miniz | 不需要（但要编译 4 个 .c 文件） |

### P10. `XXH32_state_t` 是不完整类型

想用 xxHash 的流式 API（`XXH32_createState` 等），必须定义
`XXH_STATIC_LINKING_ONLY`，否则结构体定义被
`#if defined(XXH_STATIC_LINKING_ONLY) && ...` 挡住。

### P11. tinydir 在 Windows 上框架判断错误 + 不提供文件大小

**两个独立问题**：

1. 原版用 `#ifdef _MSC_VER` 判断 Windows。TCC 与 MinGW 都不定义它，
   于是走 POSIX 分支去 `#include <dirent.h>` / `<libgen.h>`，但**这条路在本工具链上走不通**。实测：`dirent.h` TCC **有**，但它声明的 `opendir`/`closedir` **链接失败**（`undefined symbol`，msvcrt 里没有实现）；`libgen.h` TCC **没有**（只在 `winapi-full` 里）。

   → 本环境已 patch 成 `#if defined(_MSC_VER) || defined(_WIN32)`。

2. 即使走对了 Windows 分支，`tinydir_file` **也不暴露文件大小**
   （`struct stat _s` 是 POSIX 专有字段）。

   → 本环境已加 `_size` 字段，从 `WIN32_FIND_DATA` 填充，语义对齐 `_s.st_size`。

**所以用 `f._size`，不是 `f.size`，也不是 `f._s.st_size`。**

---

## 环境级：与编译器无关

### P12. `GetLastError()` 会被无关调用污染

Windows 的 `GetLastError` **不会**被成功调用清零，所以：

```c
/* 错误：条件里第二次调用 GetLastError 会覆盖第一次的语义 */
if (GetTokenInformation(...) && GetLastError() == ERROR_INSUFFICIENT_BUFFER)

/* 正确：先捕获 */
GetTokenInformation(...);
DWORD err = GetLastError();
if (err == ERROR_INSUFFICIENT_BUFFER) ...
```

同理，测试宏里无条件调用 `GetLastError()` 会把失败归因到错误的函数上。

### P13. `CRITICAL_SECTION` 复制会破坏互斥

```c
CRITICAL_SECTION cs;
InitializeCriticalSection(&cs);
g_cs = cs;              /* ← 灾难：得到两个独立锁 */
```

副本的 `RecursionCount` / `OwningThread` **不随原对象更新**，
两个线程实际锁的是不同对象，互斥静默失效。永远只用一个规范对象。

### P14. 未显示过的窗口收不到 `WM_PAINT`

没有可见区域可失效。需要先 `ShowWindow(hwnd, SW_SHOWNA)`
（`SW_SHOWNA` = 显示但不激活，不会抢用户焦点），再 `InvalidateRect`。

### P15. `SetTimer(hwnd, 0, ...)` 的自动 ID 恒为 1

且 `KillTimer(hwnd, 1)` 失败；连续建多个自动定时器全部返回 1、互相覆盖。

**修法**：永远用显式非零 ID。

```c
UINT_PTR id = SetTimer(hwnd, 5001, 50, NULL);   /* 显式 ID */
KillTimer(hwnd, 5001);                          /* 成功 */
```

### P16. `GetModuleHandleA("xxx.dll")` 可能返回 NULL

TCC 只为**实际调用到的函数**建立导入。所以进程里可能根本没加载 `user32.dll`，
`GetModuleHandleA` 返回 NULL（错误码 126）。

**修法**：用 `LoadLibraryA` 确保加载。

### P17. 桌面环境的 `SetThreadErrorMode` 返回值不可靠

出参始终正确，但返回值恒为 `0x1`，与文档不符。
已排除是 TCC 代码生成问题（`GetProcAddress` 调用结果相同，其他返回 DWORD 的 API 正常）。

**修法**：信任出参，不要用返回值。

### P18. 注册表写入被沙箱拒绝

本工作区的 agent 沙箱拒绝**所有**注册表写入（HKCU 也是），
与访问掩码无关（`KEY_ALL_ACCESS` / `KEY_WRITE` / `SET_VALUE` / `CREATE_SUB_KEY` 乃至掩码 `0` 全拒）。
读取正常。这是环境策略，不是工具链问题。

### P19. `GetEnvironmentVariableA` 缓冲区不足时不含 NUL

返回值语义不对称：
- 缓冲区不足 → 返回**含 NUL** 的所需长度，且**不设置** `ERROR_INSUFFICIENT_BUFFER`
- 成功 → 返回**不含 NUL** 的长度

所以 `成功返回值 == 不足时的返回值 - 1`。

---

## 脚本 / 构建期间的坑（写工具时必踩）

### P20. PowerShell 的 `>` 写 UTF-16

```powershell
gh api ... > file.c        # ← 文件变成 UTF-16，源码彻底损坏
```

**修法**：永远用显式编码
```powershell
[System.IO.File]::WriteAllText($p, $text, (New-Object System.Text.UTF8Encoding($false)))
```

### P21. `Set-Content` 默认编码可能是 ANSI

中文注释会变乱码，进而导致**莫名其妙的编译错误**
（例如 `%` 被吃掉导致 printf 格式串错位，输出未初始化内存——表现为"值每次运行都不同"）。

**修法**：测试源码用纯 ASCII；需要中文时显式指定编码。

### P22. PowerShell 5.1 读无 BOM 的 UTF-8 脚本会按 ANSI 解释

本环境所有 `.ps1` 都带 UTF-8 BOM，就是为了这个。

### P23. `.ps1` 脚本受执行策略限制

未签名脚本默认被拒。本环境全部提供 `.cmd` 包装器。

### P24. PowerShell 会把 `-o` 解析成 `-Output`/`-Obj` 的歧义缩写

```
Parameter cannot be processed because the parameter name 'o' is ambiguous.
Possible matches include: -Output -Obj -OutVariable -OutBuffer.
```

所以 `scripts/tcc.ps1` **故意不使用 `param()` 绑定**，手工遍历 `$args`。

### P25. `cmd` 的 `REM` 注释不认 UTF-8

`.cmd` 文件里有中文会导致 `'制）' is not recognized as an internal or external command`。
`.cmd` 一律纯 ASCII。

### P26. PowerShell 的 `@(a, b)` 不是数组字面量

`@()` 是**数组子表达式运算符**，会逐条求值：

```powershell
$pairs = @(@("a","b"), @("c","d"))   # ← 错误，等价于执行后取最后结果
$pairs = @( ,@("a","b") ,@("c","d") ) # ← 正确，或用逗号数组
```

### P27. 依赖库元数据不可信

`single_file_libs` 列表标注的文件数经常过时：

| 库 | 列表标注 | 实际 |
|---|---|---|
| miniz | 1 个文件 | **12 个文件**（已模块化） |
| utf8.h | — | API 是 `utf8len`，不是 `utf8_strlen` |
| cJSON | 1 个文件 | 正确，但需 `-DCJSON_HIDE_SYMBOLS` |

**下手前先 `gh api repos/X/contents` 看真实结构。**

### P28. GitHub 下载的两个陷阱

```powershell
# 错误 1：base64 路径会静默截断（16700 字节的文件只拿到一半）
gh api repos/X/Y/contents/f.h -q '.content'

# 错误 2：> 重定向写 UTF-16
gh api repos/X/Y/contents/f.h -H "Accept: application/vnd.github.raw" > f.h

# 正确
$t = gh api "repos/X/Y/contents/f.h" -H "Accept: application/vnd.github.raw" 2>$null | Out-String
[System.IO.File]::WriteAllText($target, $t, (New-Object System.Text.UTF8Encoding($false)))
```

下载后必须校验：文件以 `/*` 或 `//` 开头、体积合理、
且**不包含** `"message":"Not Found"`（404 页面会被当源码保存，
miniz 的 `miniz_export.h` 就踩过这个）。

### P29. `gh api -q '...'` 会被 PowerShell 拆参数

```
gh : accepts 1 arg(s), received 2
```

**修法**：用 `--jq='...'` 等号形式。

---

## Lua 相关的坑

### P30. Lua 的 `arg[1]` 是第一个参数，不是 exe 路径

嵌入 Lua 时若按 C 的 `argv[0]` 惯例填表，脚本会把 exe 路径当成命令名。

### P31. Lua 有 200 个局部变量的上限

```lua
local function f1() end
local function f2() end
... 200 个之后 ...
-- too many local variables (limit is 200) in main function
```

**修法**：用 `do ... end` 分块，块内局部变量随作用域释放。

### P32. 字节码有版本校验

字节码头 5 字节是 `1b 4c 75 61 <版本>`（ESC "Lua" + 版本号，5.5 = 0x55）。
用 `luac55` 编的字节码必须由 5.5 解释器加载。
本环境的 `luac55` 与解释器源码版本一致。

### P33. 字节码嵌入的阈值

Lua 源码解析约 **6.7 µs/行**。实测进程启动时间：

| 脚本行数 | 源码嵌入 | 字节码嵌入 | 节省 |
|---|---|---|---|
| 52 | 21 ms | 20 ms | 无差别 |
| 500 | 31 ms | 21 ms | 10 ms |
| 2000 | 57 ms | 26 ms | 30 ms |
| 5000 | 116 ms | 34 ms | 83 ms |
| 10000 | 222 ms | 49 ms | **173 ms** |

**经验阈值：脚本 < 500 行不必用字节码；> 1000 行应当用。**
进程启动本身要 ~19 ms，小脚本的解析节省会被淹没。

### P34. miniz 解压不能用固定倍数缓冲

压缩率可达 0.8%（6000 → 49 字节），按压缩后大小乘固定倍数分配解压缓冲会
严重低估并报 `MZ_BUF_ERROR (-5)`。

**修法**：用流式 `mz_inflate` 分块扩容。`build-lua.ps1` 生成的绑定已这样做。

---

## LuaRocks 相关（结论：本环境下不适用）

- LuaRocks 是 pip 风格，装进**共享目录树**，靠 `LUA_PATH` 查找 —— 与"单文件零依赖"根本冲突。
- Windows 官方只支持 MSVC / MinGW，**不支持 TCC**。
- 即使覆盖 `CC=tcc`：① 它对每个 C 模块追加 `-lm`（见 P6）；
  ② MinGW ld 会自动导出 `luaopen_*`，**TCC 不会**，所以已发布的 rock 源码
  大多缺 `dllexport`，编出来的 DLL 无法 `require`（报"找不到指定的程序"）。
- 结论：**纯 Lua 库直接 vendoring（复制 .lua 文件）**；C 扩展用本环境手动编译。

---

## 快速自查清单

遇到问题时按顺序检查：

1. `tccwin.h` 是不是第一个 include？
2. 用到 float 数学函数了吗？加 `tccmath.h`。
3. 是不是加了 `-lm`？去掉。
4. 用 `scripts\tcc.cmd` 编译，而不是直接调 `tcc.exe`（否则缺 `-L lib\tccdefs`）。
5. 输出结果异常但编译通过 → 查 P2（缺原型）、P12（GetLastError 污染）。
6. 链接失败 → `scripts\tcc.cmd -ShowFlags` 看实际参数。


---

## 补充发现（第二轮库测试）

### P35. TCC 完全没有 Winsock 头文件

`include/` 下**没有** `winsock2.h`、`ws2tcpip.h`、`winsock.h` 中的任何一个。

结果：任何用 socket 的库（mongoose、civetweb 等）都**无法直接编译**。
Mongoose 会报：

```
mongoose.h:956: error: include file 'winsock2.h' not found
```

**修法**：自己写一个最小 Winsock 头（声明 `SOCKET`、`sockaddr_in`、
`WSAStartup`、`socket`、`bind`、`listen`、`accept`、`send`、`recv`、`closesocket`
及 BSD 的 `u_*` 类型），然后链接：

```
-I <winsock-shim-dir> ... -lws2_32
```

**但要注意**：手写的结构体布局若与真实 ABI 不符，问题只在**真实收发数据时**才暴露。
本环境**未纳入**网络库，需要时请单独验证。

### P36. `strnlen` 在 TCC 的 string.h 里被 `#if 0` 屏蔽

头文件里有，但条件编译掉了，导致 `toml.c` 报隐式声明。

**修法**：`helper/msvcrt_compat.c` 已实现，`helper/msvcrt_compat.h` 已声明，
wrapper 对需要的库自动 `-include` 该头。

### P37. `S_ISDIR` 其实**存在**（纠正一个易犯的误判）

`sys/stat.h:255` 定义了它 —— 但那一行是**制表符缩进**的。
如果用"空格 + 名字"去 grep 就会漏掉，进而误判为"TCC 没有 S_ISDIR"。

这个误判有实际后果：mongoose 在 `S_ISDIR` 未定义时会**整个编译掉** `p_stat()`，
于是文件状态支持静默失效。

**教训**：`grep 'define S_ISDIR'` 会漏，应该用 `grep 'S_ISDIR'` 再人工看。

### P38. stb_c_lexer 有一个真实的越界 bug（已修补）

**不是 TCC 的问题** —— 用 MinGW gcc 6.3.0 编译同一份探测程序，
行为逐字节相同。已定位到 `stb_c_lexer.h:675`：

```c
/* 原版 */
return stb__clex_token(lexer, CLEX_charlit, start, p+1);
/* 此时 p 已经指向闭合引号，于是 parse_point = eof+1， */
/* 而 EOF 判定是 if (p == lexer->eof)，p 一旦超过 eof 就永远不成立 */
```

**后果**：当字符字面量是缓冲区**最后一个**内容时，lexer 会越界并产出
128 个垃圾 token，且不终止。加个尾随空格或分号就正常。

**修法**：改成 `p`。本环境的 `include/stb_c_lexer.h` 已打此补丁并附说明。

### P39. mongoose 有一个与编译器无关的 bug

`mongoose.h` 第 1583 行：

```c
#define mg_str(s) mg_str_s(s)
```

但 `mg_str_s` **只在 `mongoose.c:15495` 定义，头文件里从未声明**。
于是每个调用文档中 `mg_str()` 宏的用户都会得到"隐式声明一个返回结构体的函数"，
这是所有编译器上的 UB，在 GCC 14+ / C23 下直接是硬错误。

**修法**：自己补声明 `struct mg_str mg_str_s(const char *s);`

---

---

## P40-P41 · 网络能力（P1，已验证）

### P40. TCC 完全没有 Winsock 头文件

`toolchain\tcc\include\` 下**不存在**任何一个：
`winsock2.h` / `winsock.h` / `ws2tcpip.h` / `ws2def.h` / `mstcpip.h`。

但 `ws2_32.dll` 是系统自带的（实测导出 195 个符号），链接没问题 ——
**缺的只是编译期声明**。

**修法**：本环境提供 `include/winsock2.h` 垫片，用 `-With socket` 自动启用
（会自动追加 `-lws2_32`）。

### P41. 垫片的包含顺序与结构体布局（踩过两次）

**包含顺序**：

```c
#include "tccwin.h"      /* 先：引入 <windows.h>，类型体系就位 */
#include "winsock2.h"    /* 后：只用已存在的类型 */
```

**反过来会失败**，而且我实测踩了两次，两种错法不同：

| 错法 | 报错 |
|---|---|
| `winsock2.h` 在前且自行定义 DWORD/WORD 等 | `windef.h:94: incompatible redefinition of 'DWORD'` |
| 试图让 `winsock2.h` 完全自给自足 | 一旦后续引入 `windows.h`，同样的重定义 |

**根本原因**：TCC 的 `basetsd.h`（被 `winnt.h` 包含）会定义 `UINT_PTR` /
`SIZE_T` 等；`windef.h` 会定义 `DWORD` / `WORD` / `BOOL`。
垫片若也定义就会冲突。
**正确做法是复用 TCC 自带的定义**（它们描述的就是真实 Win32 ABI），
只在垫片里定义 Windows 没有的东西（`SOCKET`、`sockaddr_in`、`WSADATA` 等）。

**结构体布局必须与 ABI 一致** —— 已用 `sizeof` 逐项校验，并与 MSDN 交叉核对：

| 结构体 | 大小 | 依据 |
|---|---|---|
| `sockaddr` | 16 | `sa_family`(2) + `sa_data[14]` |
| `sockaddr_in` | 16 | `sin_family`(2) + `sin_port`(2) + `sin_addr`(4) + `sin_zero[8]` |
| `sockaddr_in6` | 28 | Windows 特有点，注意 `AF_INET6` 是 **23** 不是 Linux 的 10 |
| `sockaddr_storage` | 128 | MSDN `SOCKADDR_STORAGE_XP`：family(2) + pad1(6) + align(8) + pad2(112) |
| `WSADATA` | 408 | 4×WORD(8) + ptr(8) + 257 + 129 = 402，按 8 对齐 |
| `fd_set` | 520 | `u_int`(4) + `SOCKET[64]`(512)，按 8 对齐 |
| `timeval` | 8 | Windows 上两个成员都是 `long`（各 4 字节） |

**注意 `WSADATA` 是 408 而不是 400** —— 我一开始按 400 写测试期望值，
结果 MISMATCH。查 MSDN 的成员列表后手算才确认 408 是对的（x64 对齐）。

### 垫片覆盖范围与未验证边界

**已验证**（`examples/probes/probe_socket.c`，38 项）：
- 真实 TCP 回环：`bind`/`listen`/`accept`/`connect`/`send`/`recv`，内容逐字节比对
- 真实 UDP 回环：`sendto`/`recvfrom`，含来源地址回填
- 错误路径：连接无监听端口返回 `WSAECONNREFUSED`(10061)
- `getaddrinfo` / `freeaddrinfo` DNS 解析
- `inet_pton` / `inet_ntop` 地址转换往返
- `select` 超时语义、`fd_set` 宏
- `WSAStartup` / `WSACleanup` 配对

**未覆盖**（真实项目若用到需自行验证）：
`WSAAsyncSelect`、`WSAEventSelect`、`WSAPoll`、
重叠 I/O（`Overlapped` / IOCP）、原始套接字、多播、IPv6 实际收发。

---

## P42-P50 · GUI 能力（P1，Nuklear）

Nuklear v4.13.3，**公有领域**，单文件 `nuklear.h`（1.2 MB / 31468 行）。
自带 stb_truetype 与 stb_rect_pack 实现，字体开箱可用。

```c
#include "tccwin.h"
#include "nuklear_tcc_compat.h"   /* 必须：修类型 + 给推荐配置 */
#define NK_IMPLEMENTATION         /* 只在一个翻译单元里定义 */
#include "nuklear.h"
```

编译：`scripts\tcc.cmd app.c -With nuklear`（自动链接 `gdi32`、`user32`）

### P42. TCC 不认 Nuklear 的 64 位判定，`nk_size` 变成 4 字节

`nuklear.h:345-363` 判定 `NK_SIZE_TYPE` 的分支依次是：

| # | 条件 | TCC |
|---|---|---|
| 1 | `_WIN64 && _MSC_VER` | 无 `_MSC_VER` |
| 2 | `_WIN64 && (__MINGW64__ \|\| __clang__)` | 两者皆无 |
| 3 | `(_WIN32 \|\| WIN32) && _MSC_VER` | 无 `_MSC_VER` |
| 4 | `(_WIN32 \|\| WIN32) && (__MINGW32__ \|\| __clang__)` | 两者皆无 |
| 5 | `__GNUC__ \|\| __clang__` | 皆无 |
| 6 | `else: unsigned long` | **命中 → 4 字节** |

**关键细节：TCC 在 x64 上只定义 `_WIN64`，不定义 `_WIN32`**，
所以连第 3、4 条 Windows 分支都进不去。

后果是 `nuklear.h:420` 静态断言失败：

```
nuklear.h:420: error: invalid array size
```

只有这一句，完全看不出真正原因（断言是 `typedef char _dummy_array[(exp)?1:-1]`）。

**修法**：Nuklear 用 `#ifndef NK_SIZE_TYPE` 守卫，包含前预定义即可。
`nuklear_tcc_compat.h` 已代劳。**同类报错先查这个**。

### P43. 不定义 `NK_INCLUDE_FONT_BAKING` 时 `nk_font_atlas` 只有前置声明

直接声明该类型变量会报 `unknown type size`，同样难以反推。
`nuklear_tcc_compat.h` 默认打开：

- `NK_INCLUDE_FONT_BAKING` —— 字体烘焙
- `NK_INCLUDE_DEFAULT_FONT` —— 内置 ProggyClean 位图字体，**无需外部 .ttf**
- `NK_INCLUDE_DEFAULT_ALLOCATOR` —— 用 malloc/free

用 `-DNK_NO_AUTO_CONFIG` 可关掉自动配置。

### P44. 字体烘焙的顺序不能反

```c
nk_font_atlas_init_default(&atlas);
nk_font_atlas_begin(&atlas);
struct nk_font *font = nk_font_atlas_add_default(&atlas, 13.0f, NULL);  /* 先加入 */
const void *img = nk_font_atlas_bake(&atlas, &w, &h, NK_FONT_ATLAS_RGBA32); /* 再烘焙 */
nk_font_atlas_end(&atlas, nk_handle_id(0), NULL);
nk_init_default(&ctx, &font->handle);
```

先 `bake` 会得到空图集，`font->info.height` 为 0，
渲染时触发断言 `you forgot to add a font`（nuklear.h:20930）。
实测正确顺序得到 512x64 图集、224 个字形、12% 非透明像素。

**字体度量在 `font->info`（`nk_baked_font`）里，不是 `font->height`。**

### P45. 每帧的正确顺序（三条都是硬性的）

```c
nk_input_begin(&ctx);
    /* 在这里报告输入事件 */
    nk_input_motion(&ctx, x, y);
    nk_input_button(&ctx, NK_BUTTON_LEFT, x, y, down);
nk_input_end(&ctx);

nk_begin(&ctx, "标题", bounds, flags);
    /* 控件 */
nk_end(&ctx);
nk_clear(&ctx);          /* 必须 */
```

- **每帧必须 `nk_clear()`**：否则第二帧撞
  `Assertion failed: win->seq != ctx->seq`（nuklear.h:20968-20975）
- **调了 `nk_input_begin/end` 就必须走一帧**：否则同样触发上面的断言
- **输入事件必须在 `nk_input_begin` 与 `nk_input_end` 之间报告**：
  `nk_input_begin` 清零 `buttons[].clicked`（18296），
  而 `nk_input_button` 在"状态未变化"时直接返回（18358）。
  在 begin/end 之外调用会让 `clicked` 永远为 0，按钮永不触发

### P46. 默认按钮行为是**按下即触发**，不是抬起

`NK_BUTTON_DEFAULT` 走 `nk_input_is_mouse_pressed`（nuklear.h:25010），
即 `down && clicked`。断言要按"按下帧计数"写。
需要"抬起才触发"就在包含 nuklear 前定义 `NK_BUTTON_TRIGGER_ON_RELEASE`。

### P47. `nk_widget()` 会推进布局 —— 别在同一帧里既取几何又画控件

```c
/* 错：多调一次 nk_widget，按钮会判断到下一个槽位 -> 永远点不动 */
nk_widget(&r, &ctx);
nk_button_label(&ctx, "OK");

/* 对：要几何就在独立的"发现帧"里取 */
```

这个坑的表现是"按钮完全无响应"，而几何打印出来又是对的，很难查。

### P48. `last_widget_state` 是单个变量，后面的控件会覆盖前面的

它不是数组。渲染多个控件时只能拿到**最后一个**控件的状态。
要检查某个控件的状态，就得单独渲染它。另外它**每帧重置**，
必须在帧内（`nk_end` 之前）读，出了 `nk_clear` 就是 0。

状态位实际取值（`NK_FLAG(n) == 1 << (n-1)`，nuklear.h:3315-3324）：
`MODIFIED=1`, `INACTIVE=2`, `ENTERED=4`, `HOVER=8`, `ACTIVED=16`, `LEFT=32`,
`HOVERED = HOVER|MODIFIED = 9`, `ACTIVE = ACTIVED|MODIFIED = 17`。
实测悬停帧 26、按下帧 98。

### P49. `nk_convert` 的"成功"是 0

```c
enum nk_convert_result {
    NK_CONVERT_SUCCESS = 0,      /* 注意是 0！ */
    NK_CONVERT_INVALID_PARAM = 1,
    ...
};
```

写 `if (conv)` 会把成功当失败。实测正确调用产出
顶点缓冲 4000 字节、索引缓冲 1476 字节（1476 % 3 == 0）。

配套的两个易错点：
- 配置字段是 **`cfg.tex_null.texture`**，不是 `cfg.null.tex`
- **Nuklear 不自带顶点结构体**（没有 `nk_draw_vertex`），
  顶点格式由使用者定义，照文档示例：`position[2] / uv[2] / col[4]`
- 需要顶点输出时定义 `NK_INCLUDE_VERTEX_BUFFER_OUTPUT`

### P50. TCC 无法解析宏参数里以分号分隔的多条语句

这是**测试脚手架**层面的坑，但很容易浪费大量时间。最小复现：

```c
#define FRAME(body) do { fn(); body fn(); } while (0)
FRAME(fn(); g = 5);      /* error: ';' expected (got "fn") */
```

单条语句（含函数调用）没问题，多条语句必然失败。
`do { ... } while (0)` 作为宏参数**是**可以的（实测通过），
所以限制只在"分号分隔的多语句"。

**规避**：宏参数只放**一条**语句；需要多步就封装成函数。

### 已验证范围与未验证边界

**已验证**（`examples/probes/probe_nuklear.c`，43 项，全部确定性、无需窗口）：
- 类型宽度（`nk_size`/`nk_ptr` 8 字节）
- 字体烘焙真实产出位图（512x64，12.14% 非透明像素）
- 布局几何（按钮占满列宽、纵向不重叠、多列按权重分配）
- 输入命中（悬停判定、按下/抬起、重复点击、空白区不误触）
- 控件状态位（悬停 26、按下 98）
- 绘制命令（矩形 + 文本）与顶点/索引缓冲输出

**未验证边界**（真实项目若用到需自行验证）：
- **实际窗口显示**：探针全程不创建窗口，验证的是 GUI 逻辑层。
  像素级渲染交给后端（GDI/OpenGL），本环境未纳入 GDI 后端
- 文本输入、滚动条、下拉框、树、表格等复杂控件未覆盖
- 多窗口、弹出层（popup）、上下文菜单
- 中文等非 ASCII 字形（内置 ProggyClean 只有 ASCII，需自备 .ttf 并
  配置 `nk_font_config` 的 `range`）

---

## P51-P54 · SQLite / 正则 / 字体（已验证）

### P51. 编译 SQLite 必须用包装文件，且 stddef.h 要最先包含

直接编译 `sqlite3.c` 会失败，**两个原因叠加，报错都很难反推**：

**原因一**：`sqlite3.c` 在 Windows 上会包含 `<windows.h>`，这条链会把
TCC 的 `stddef.h` 卷进来两次。第二次虽被 `_STDDEF_H` 守卫跳过，
但 TCC 的解析器仍报错：

```
';' expected (got "typedef")
```

错误位置显示为 **`stddef.h:4`**（因为 `#line` 指令指向那里），
完全看不出是 sqlite3.c 引起的。

**原因二**：TCC 缺 `MultiByteToWideChar` / `WideCharToMultiByte` 的声明，
报 `'MultiByteToWideChar' undeclared`。

**修法**：用 `lib\sqlite\sqlite3_tcc.c` 包装文件，**不要**直接编译
`sqlite3.c`。`-With sqlite` 已自动处理：

```c
#include <stddef.h>     /* 必须最先 —— 顺序是关键 */
#include <stdint.h>
#include "tccwin.h"
#include "sqlite3.c"
```

实测编译 `sqlite3.c`（1.9 MB 目标文件）约 **0.9 秒**（MinGW gcc 需 10 秒）。

### P52. TCC 缺宽字符转换 API 与代码页常量

TCC 的 `windows.h` 既不提供 `MultiByteToWideChar` / `WideCharToMultiByte`
的声明，也不提供 `CP_UTF8` 等常量，但 `kernel32.dll` 里有这些导出。
已在 `tccwin.h` 补齐（`CP_UTF8` / `CP_ACP` / `MB_ERR_INVALID_CHARS` 等
+ 两个函数声明）。实测 UTF-8 往返正确：

```
MultiByteToWideChar(CP_UTF8) "hello 中文" -> 9 宽字符
WideCharToMultiByte 回转 -> 13 字节，往返一致
```

### P53. `__faststorefence` 在 TCC 下链接失败 —— 用 weak 符号修

TCC 的 `winnt.h:1161` 只**声明**了它，没有实现：

```c
VOID __faststorefence(VOID);
#define MemoryBarrier __faststorefence
```

于是链接期报 `tcc: error: undefined symbol '__faststorefence'`。

但**直接定义成普通函数又会报 `'__faststorefence' defined twice`** ——
TCC 在单文件编译时确实会提供一份。两个现象并存。

**修法**：用 **weak 符号**（`tccwin.h` 已内置）。有 TCC 自带版本时用它，
没有时用我们的。这是"补缺口但不与编译器抢定义"的标准做法：

```c
__attribute__((weak)) void __faststorefence(void) {
    __asm__ __volatile__("xchgq %%rax, %%rax" ::: "memory", "rax");  /* x64 */
}
```

### P54. tiny-regex-c 不支持分支 `|`（文档与实现不一致）

README 把 `|` 列在"支持的运算符"里，但 `re.c:135` 是注释掉的实现：

```c
/*    case '|': {    re_compiled[j].type = BRANCH;          } break; <-- not working properly */
```

同一个 README 的 TODO 第一条又写着 "Fix implementation of branches (`|`)"。
**不要相信它的功能列表** —— 需要交替时改用字符类。

其余语法（锚点、字符类、`\d\w\s` 等预定义类、`* + ?` 量词、转义、
大小写敏感）实测都正常，共 56 项验证通过。

### P55. 在 Nuklear 同一翻译单元里用裸 stb_truetype 会崩

Nuklear 把 stb_truetype 的分配器重定向到自己的包装函数：

```c
nk_stbtt_malloc(nk_size size, void *user_data) {
    struct nk_allocator *alloc = (struct nk_allocator *) user_data;
    return alloc->alloc(alloc->userdata, 0, size);   /* user_data 为 NULL 就崩 */
}
#define STBTT_malloc(x,u)  nk_stbtt_malloc(x,u)
```

Nuklear 自己调用时传 atlas 的 allocator，没问题；
但使用者直接调 `stbtt_*` 时按 stb 惯例**传 NULL**，于是解引用空指针 ——
表现为**访问违例 0xC0000005，没有任何错误信息**，极难定位
（实测崩在 `stbtt_GetCodepointBitmap`）。

**修法**：`nuklear_tcc_compat.h` 已在包含 nuklear.h 之前抢定义
（`nuklear.h:6490` 有 `#ifndef STBTT_malloc` 守卫）：

```c
#define STBTT_malloc(x,u)  ((void)(u), malloc(x))
#define STBTT_free(x,u)    ((void)(u), free(x))
```

### P56. TCC 无法解析宏参数里以分号分隔的多条语句

最小复现（**纯 TCC 限制，与具体库无关**）：

```c
#define FRAME(body) do { fn(); body fn(); } while (0)
FRAME(fn(); g = 5);      /* error: ';' expected (got "fn") */
```

单条语句（含函数调用）没问题；`do { } while (0)` 作为宏参数**也**可以。
限制只在"分号分隔的多语句"。**规避**：宏参数只放一条语句，
多步就封装成函数。

---

## P57-P58 · 网络与 SQLite 升级（补充）

### P57. 本环境 HTTPS 失败的真正原因：Schannel 凭据句柄创建失败

**这是环境限制，不是网络或对端问题。** 表现为：

```
curl.exe : curl: (35) schannel: AcquireCredentialsHandle failed:
           SEC_E_NO_CREDENTIALS (0x8009030E)
```

`AcquireCredentialsHandle` 是 SSPI 里为 Schannel 获取**客户端凭据句柄**的调用
（这里的"凭据"不是网站账号密码，而是 Schannel 建立 TLS 安全上下文用的
内部对象）。**故障发生在这个句柄的创建阶段 —— TLS ClientHello 都还没发出**，
后续的服务器证书验证根本没开始。

**因此这些做法都碰不到问题**（我逐个试过，全部无效）：

| 尝试 | 结果 |
|---|---|
| 改 CA 证书 / 关吊销检查 | 无效 —— 还没到证书验证 |
| 切 TLS 版本 | 无效 —— 还没到握手 |
| 换 http 而非 https | sqlite.org 强制 301 回 https，无效 |
| 换 URL / 换镜像 | 无效 —— 与本机 SSPI 状态有关，与对端无关 |
| `Invoke-WebRequest` | 同样失败（.NET 也走 Schannel） |

上游成因未能确定 —— 受限令牌与用户配置状态都可能参与，
具体是哪一项 Windows 状态无法访问没有查清。此错误**影响所有
使用 Schannel 的客户端**。

**绕过方法：换用不依赖 Schannel 的 TLS 实现。** 实测：

| 工具 | TLS 后端 | 结果 |
|---|---|---|
| **CPython**（`py` / `python`） | **OpenSSL 3.0.18** | **可用** —— sqlite.org 直接 200 |
| `gh` | Go `crypto/tls` | 可用（本会话一直用它取 GitHub 内容） |
| Node.js | 自己的 OpenSSL 构建 | 用户报告可用（本次未验证） |
| `curl.exe` | Schannel | 不可用 |
| `Invoke-WebRequest` | Schannel | 不可用 |

**下载文件的标准做法（已实测）**：

```powershell
# 用 CPython 的 urllib，不要用 curl / Invoke-WebRequest
py -c "import urllib.request,sys; open(sys.argv[2],'wb').write(urllib.request.urlopen(sys.argv[1],timeout=90).read())" `
   "https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip" "out.zip"
```

确认 TLS 后端：

```powershell
py -c "import ssl; print(ssl.OPENSSL_VERSION)"
# OpenSSL 3.0.18 30 Sep 2025
```

### P58. SQLite 3.53.x 需要定义 SQLITE_DISABLE_INTRINSIC

从 SQLite 3.53 起，`sqlite3Multiply128()` 有三条实现路径
（见 `sqlite3.c` 里 `SQLITE_USE_UINT128` 那段）：

```c
#if defined(SQLITE_USE_UINT128)              /* 要求 __GNUC__ / __clang__ */
  __uint128_t 乘法
#elif defined(_WIN64) && !defined(SQLITE_DISABLE_INTRINSIC)
  __umulh(a, b)                              /* MSVC 内建 */
#else
  手工 32 位分段乘法（可移植）
#endif
```

**TCC 既没有 `__GNUC__` 也没有 `__clang__`**，所以第一条不成立；
但它定义了 `_WIN64`，于是落到第二条用了 MSVC 内建的 `__umulh` ——
TCC 同样没有，链接期报：

```
tcc: error: undefined symbol '__umulh'
```

**修法**：定义 `SQLITE_DISABLE_INTRINSIC`，走到可移植的手工实现。
`lib/sqlite/sqlite3_tcc.c` 已内置这个定义（在包含 `sqlite3.c` 之前），
使用者不需要知道。

注意 **3.38.2 及更早没有这个问题** —— 升级到 3.53.x 才出现。
本环境已升级到 3.53.4 并验证。

---

## P59. 中文在 GBK(936) 控制台上乱码 —— 根因与两种修法

### 现象

同一个程序，在**某些终端正常、某些终端全是乱码**：

```
tcwinreadline 婕旂ず 鈥斺€?杈撳叆 help 鐪嬪彲鐢ㄥ懡浠わ紝Ctrl-D 閫€鍑?
```

### 根因（三层叠加）

1. **本环境源码是 UTF-8（无 BOM）**，而 TCC 对字符串字面量
   **不做任何字符集转换** —— 源文件里的字节原样进 .exe。
   TCC 也**没有** `-fexec-charset` / `-finput-charset` 之类的选项
   （实测 `tcc --help` 里没有任何 charset 开关），所以无法在编译期解决。
2. **中文 Windows 的 ANSI 代码页默认是 936（GBK）**：
   ```
   GetACP()             = 936
   GetConsoleCP()       = 936
   GetConsoleOutputCP() = 936 或 65001（取决于终端）
   ```
3. `printf("中文")` 把 UTF-8 的 `e4 b8 ad` 写到控制台。
   **GBK 控制台按双字节解释它** → 乱码。
   而 OutputCP=65001 的终端（Windows Terminal、VSCode 终端）
   按 UTF-8 解释 → 正常。

**这就是"我这儿好好的、你那儿全是乱码"的根源** —— 与代码无关，
取决于运行时的控制台代码页。实测的字节对照：

| OutputCP | `printf` 输出的字节 | 结果 |
|---|---|---|
| 65001 | `e4 b8 ad` | 正确 |
| 936 | `e4 b8 ad` | **乱码**（GBK 的"中"是 `d6 d0`） |

### 修法 A：把控制台输出代码页设成 UTF-8（一行，推荐）

```c
#include "tccwin.h"
#include <stdio.h>

int main(void) {
    SetConsoleOutputCP(CP_UTF8);   /* 之后所有 printf 都正确 */
    printf("中文正常\n");
    return 0;
}
```

实测从 936 切换成功（返回 1，`GetConsoleOutputCP()` 变为 65001）。
**本环境的全部探针与 examples 都已加这一行。**

注意这会改变**当前控制台**的代码页（程序退出后仍保持）。
对命令行工具是常见做法；不希望在用户终端留下副作用的程序用修法 B。

### 修法 B：不改控制台，输出时转换（`include/utf8io.h`）

```c
#include "tccwin.h"
#include <stdarg.h>
#include "utf8io.h"

u8_puts("中文正常\n");
u8_printf("值 = %d\n", 42);
```

`u8_console()` 把 UTF-8 转成**当前** `GetConsoleOutputCP()` 所指的代码页
（65001 时原样返回，不做无谓转换）。实测在 OutputCP=936 下产出 `d6 d0`
（GBK 的"中"），控制台正确显示。

适合：不想动用户终端设置的库/工具，或需要精确控制输出的场景。

### 输出被重定向时

重定向到文件/管道时没有控制台，两种修法都退化为**直接写 UTF-8 字节**
（`utf8io.h` 检测到拿不到控制台句柄就原样输出）。这对文件是正确的 ——
文件通常期望 UTF-8。

### 调试提示

拿到"乱码"先别急着改代码，**先确认代码页**：

```c
printf("ACP=%u OCP=%u ICP=%u\n", GetACP(), GetConsoleOutputCP(), GetConsoleCP());
```

另外：**用 PowerShell 的 `& exe | ForEach-Object` 看输出会二次解码**，
可能把正确的输出显示成乱码（或反过来）。要判断真实字节，
必须重定向到文件再按 `[System.IO.File]::ReadAllBytes()` 读：

```powershell
cmd /c "chcp 936 >nul && app.exe > out.bin 2>&1"
$b = [System.IO.File]::ReadAllBytes("out.bin")
($b | ForEach-Object { $_.ToString('x2') }) -join ' '   # 看真实字节
```

### 未验证边界

- 只处理**输出**。**输入**中文没做转换 —— `tcwinreadline` 的行编辑
  按字节处理，多字节字符的删除与光标定位仍不准
- 目标代码页里没有的字符（如 GBK 下的 emoji）会变成 `?`
- `SetConsoleOutputCP` 对 ConPTY / MSYS 终端的行为未测

### P59 附：缺原型如何伪装成"编码计算错误"（调试实录）

排查上面那个编码问题时，探针报了一个看起来荒谬的结果：
`MultiByteToWideChar(CP_UTF8, 0, "中", ...)` 返回 **U+002D**（连字符），
而不是 U+4E2D。字节是 `2D 00 00 00`，像是只读到了一个 `-`。

第一反应是"这个 API 在 TCC 下坏了"。**实际原因是缺原型**：

```
probe_encoding.c:40: warning: implicit declaration of function 'GetACP'
```

`GetACP()` 在 TCC 的 headers 里**没有声明**（`GetConsoleOutputCP` 有，
`GetACP` 没有）。C89 隐式声明会按 `int` 返回、参数按默认提升传递 ——
而这发生在**同一个函数**里，随后的 `MultiByteToWideChar()` 调用
也跟着受影响，返回了错误结果。

去掉对 `GetACP()` 的调用，同一个 `MultiByteToWideChar` 立刻正常。
**表面上是转换 API 坏了，实际是缺原型。**

已给 `tccwin.h` 补齐这些（kernel32.def 里本来就有导出）：

```
GetACP  GetOEMCP  GetCPInfo  IsDBCSLeadByte  IsDBCSLeadByteEx
GetSystemDefaultLCID  GetUserDefaultLCID  GetThreadLocale
```

**教训**：
1. **任何 `implicit declaration` 警告都要当成错误处理**，
   它会污染同一个函数里的其它调用。本环境的 `verify.ps1`
   已经把 `redefined` 警告当失败，但 `implicit declaration` 还没纳入 ——
   如果你在排查诡异结果，先用
   `scripts\tcc.cmd app.c -ShowFlags` 看一遍有没有这类警告。
2. `WCHAR` 是 **2 字节**。写断言时 `(unsigned char)wide[1]` 取的是
   **下一个码元**（终止符 0），不是同一个码元的第二个字节。
   要取字节得 `((const unsigned char *)wide)[0..1]`。
   我在这里写错过一次断言，浪费了一轮排查。

---

## P60. 32 位编译必须用 lib\tccdefs32（曾经完全不可用）

### 症状：三种表现，靠得很近但原因不同

审计 32 位能力时发现 `-32` 实际上是**坏的**，而且坏法不止一种：

| 做法 | 结果 |
|---|---|
| `tcc -m32` 或 `i386-win32-tcc.exe`，**不加** `-L` | 纯 CRT 程序 OK |
| 同上，`-L lib\tccdefs` | ❌ **一切**都失败：`undefined symbol '_except_handler3'` |
| 同上，不加 `-L`，但用 `GetTickCount64` / `GlobalMemoryStatusEx` / `InitializeCriticalSectionEx` | ❌ 全部失败（TCC 自带的 32 位 def 只有 767 个导出，没有这些） |

也就是说：**32 位能编译，但要么加 -L 就全崩，要么不加就用不了现代 API。**
而 README 声称支持 32 位交叉编译 —— 这个声称当时是不成立的。

### 根因

`lib\tccdefs\` 里的导入库是用
`tcc -impdef %WINDIR%\System32\<dll>.dll` 从 **64 位 DLL** 生成的。
把它加到 32 位链接的搜索路径后，这些 64 位导入库先于 TCC 自带的
32 位库被搜到，破坏了符号解析（`_except_handler3` 是 32 位 CRT 的
SEH 入口，本来该从 `libtcc1-32.a` 解析）。

注意 `-L` 只是**追加**搜索路径，所以问题不是"覆盖了内置路径"，
而是解析顺序/候选集被改变了。

### 修法

Windows 64 位系统在 `%WINDIR%\SysWOW64\` 下**保留了 32 位 DLL**，
用它生成一套独立的 32 位导入库：

```powershell
$t32 = "toolchain\tcc\i386-win32-tcc.exe"
foreach ($d in 'kernel32','user32','gdi32','advapi32','ws2_32', ...) {
    & $t32 -impdef "$env:WINDIR\SysWOW64\$d.dll" -o "lib\tccdefs32\$d.def"
}
```

实测生成 44 个。导出数与 64 位版接近但不完全相同
（kernel32：32 位 1610 / 64 位 1636）—— **Export Name 表与位数无关，
差异来自 WOW64 子集**，所以两边都要各自生成，不能共用。

然后让 `scripts\tcc.ps1` 按目标位数选目录：

```powershell
$defDir = if ($is32) { 'lib\tccdefs32' } else { 'lib\tccdefs' }
$tccArgs.Add('-L'); $tccArgs.Add((Join-Path $Root $defDir))
```

### 验证

`scripts\verify.cmd` 已加入 32 位检查，断言运行输出恰为 `4 1 1`：

```
4  = sizeof(void*)        → 确实是 32 位
1  = GetTickCount64() > 0 → 现代 API 可用
1  = GlobalMemoryStatusEx 成功
```

**教训**：`README` 里"支持 32 位交叉编译"这类声称必须有自检覆盖，
否则会长期处于"文档说有、实际不能用"的状态。
本环境的 `examples` 自检现在同时跑 64 位与 32 位。

---

## P61. **缺声明先查 winapi-full，不要自己打垫片**

这是本轮审计中最重要的发现，也是一条**方法论**教训。

### 问题的起源

`portable/toolchain/tcc/include/winapi/` 里只有 **18 个头文件** ——
这是 **TCC 0.9.27 发行版自带的那一份**（带 TCC 的修改），**不是被裁减的**
（依据官方 `winapi-full-for-0.9.27/readme.txt`）。
TCC 另外提供了 `winapi-full-for-0.9.27/include/winapi/` 共 **755 个**头文件，
官方说明"可以复制到 TCC 安装目录，或用 -I/-isystem"，但"兼容性未验证"。

因为缺头文件，`tccwin.h` 里积累了大量**手写声明**来补缺 ——
包括 `GetACP` / `GetOEMCP` / `GetCPInfo` / `IsDBCSLeadByte` /
`GetSystemDefaultLCID` / `MultiByteToWideChar` / `WideCharToMultiByte`
共 10 个函数，还自造了一个 `TCWIN_CPINFO` 结构体。

**这些全都在真实 SDK 的 `winnls.h` 里。** 只是那个头文件没被装进来。

### 垫片化的代价（实测踩到两类错误）

把真 `winnls.h` 加进来后，立刻出现两个冲突：

**1. 结构体定义漂移**

```
垫片:  typedef struct { DWORD dwNLSVersionInfoSize; DWORD dwNLSVersion;
                        DWORD dwDefinedVersion; DWORD dwEffectiveId;
                        GUID guidCustomVersion; } NLSVERSIONINFO;   /* 20 字节 */
真头:  typedef struct { DWORD dwNLSVersionInfoSize; DWORD dwNLSVersion;
                        DWORD dwDefinedVersion; } NLSVERSIONINFO;   /* 12 字节 */
```

两者不可共存：`incompatible redefinition of 'NLSVERSIONINFO'`。
而且**真头文件必须优先** —— 结构体大小与 API 期望不一致会静默读错内存。
（真 SDK 另有 `NLSVERSIONINFOEX` 表达扩展版本。）

**2. 签名漂移**

```
垫片:  GetCPInfo(UINT, TCWIN_CPINFO *)
真头:  GetCPInfo(UINT, LPCPINFO)
```

→ `incompatible types for redefinition of 'GetCPInfo'`，
而且是**所有**包含 `tccwin.h` 的文件都编不过。

### 正确做法

**缺声明时，先查 `tool/cc/winapi-full-for-0.9.27/` 里有没有对应头文件。**

```powershell
# 查一个符号在哪个头文件里
Select-String -Path tool\tcc\winapi-full-for-0.9.27\include\winapi\*.h `
              -Pattern 'CompareStringA' | Select-Object -First 3
```

有的话就**把头文件装进 `portable/toolchain/tcc/include/winapi/`**，
然后在 `tccwin.h` 里包含它，并**删掉垫片里的重复声明**。

本轮已按这个方式处理：装入 `winnls.h` / `aclapi.h` / `iphlpapi.h`，
`tccwin.h` 顶部 `#include <winapi/winnls.h>`，并移除 10 个重复函数声明
与 `TCWIN_CPINFO`。回归 65/65 通过，A1 的缺口一次补齐：

```
CompareStringA        成功（返回 1 = CSTR_LESS_THAN）
GetStringTypeA/W      成功（t0=0x0382，C1_UPPER|C1_ALPHA|C1_DIGIT 类标志）
NLSVERSIONINFO        成功（sizeof == 12，与真 SDK 一致）
LCMapStringEx         成功
CompareStringOrdinal  成功
```

### 一个容易踩的细节

`windows.h:70` 有一行 **被注释掉的** `//gr #include <winnls.h>`。
所以 `windows.h` **不会**自动带入 `winnls.h` —— 装了头文件也必须
显式包含。曾误以为 `_WINNLS_` 已定义（那是包含之后的状态），
绕了一圈才定位到。

### 仍需注意的重叠

`tccwin.h` 里还有其它可能与真头文件重叠的定义（`INIT_ONCE`、
`CONDITION_VARIABLE`、`SYNCHRONIZATION_BARRIER`、`FILE_INFO_BY_HANDLE_CLASS`
等）。它们目前不冲突，是因为对应头文件（`synchapi.h` / `fileapi.h` 等）
在这个 TCC 发行版里**不存在**（那是 Win7+ SDK 的 API 分区头，
0.9.27 时代还是 `winbase.h` 一体的）。若将来扩充头文件集，
需要逐个重新评估。
---

## P62. 自写 `winsock2.h` 垫片已删除 —— 及三个连带踩到的坑

### 结论

`include/winsock2.h`（397 行自写垫片）**已删除**。winsock 现在统一用官方头：

```c
#include "tccwin.h"          /* 或 <windows.h> */
#include <winsock2.h>        /* 官方头，在 toolchain/tcc/include/winapi/ */
#include <ws2tcpip.h>        /* 需要 sockaddr_in6 / addrinfo 时 */
```

编译用 `-With socket`（自动加 `-lws2_32`）。

删除原因：`toolchain/tcc/include/winapi/` 里已有官方 `winsock2.h`（56 KB），
自写垫片反而**与它冲突**（`typedef UINT_PTR SOCKET` vs `typedef INT_PTR SOCKET`
-> `incompatible redefinition`）。官方头还多提供 `_WINSOCK2API_` 宏、
206 个 `LPFN_*` 回调 typedef、重叠 I/O 与 IOCP 声明。

### 坑 1：**"转发头"方案不可行**（实测踩到）

本想留一个薄壳 `include/winsock2.h` 只做 `#include <winsock2.h>`，
既兼容旧写法又只保留一份定义。**行不通**：

```
-I portable\include          <- 排在前面
-I portable\toolchain\tcc\include\winapi

include/winsock2.h 里的 #include <winsock2.h>
  -> 又解析回 include/winsock2.h 自己（因为它在包含路径上且排更前）
  -> 自身的 #ifndef _PORTABLE_WINSOCK2_H 守卫已生效，整个文件被跳过
  -> 官方头从未被处理
```

症状很有迷惑性：`sizeof(WSADATA)` 报 `undeclared`、`struct in_addr` 报
`unknown type size`，看起来像官方头有问题，实际是自包含成环。

**规则：只要 `portable\include` 在包含路径上，就不要在那里放任何与
`winapi/` 同名的头文件。** 要用官方头就用尖括号形式让包含路径解析。

（这个坑还害我误判过一次：我以为是官方头缺声明，查了半天
`ipexport.h` / `s_addr` 宏，其实根因是这个环。）

### 坑 2：`ipexport.h` 与 `winsock2.h` 争夺 `struct in_addr`

两者都用**同一个 `#ifndef s_addr` 守卫**定义 `struct in_addr`：

```
ipexport.h:31   #ifndef s_addr
ipexport.h:32   struct in_addr { ... };
ipexport.h:39   #define s_addr S_un.S_addr
...
winsock2.h:189  #ifndef s_addr
winsock2.h:191  struct in_addr { ... };
winsock2.h:199  #define s_addr S_un.S_addr
```

**谁先被包含谁赢，后者整个跳过。** 如果先走到 `ipexport.h`（经
`iphlpapi.h:14`），`winsock2.h` 就没有 `struct in_addr`，随后
`ws2ipdef.h:11` 的 `struct ip_mreq { struct in_addr imr_multiaddr; }`
报 `field 'imr_multiaddr' has incomplete type`。

这是 winapi-full 自身的不一致（官方 readme 已声明"兼容性未经验证"）。
**规避方式：先包含 `<winsock2.h>` 再包含 `<iphlpapi.h>`。**

### 坑 3：`inet_pton` / `inet_ntop` 在整个头文件集里都没有

**实测：1012 个头文件（含 `winapi/` 755 个）里搜不到这两个的任何声明**，
但 `ws2_32.dll` 里**有导出**。原因是本头文件集来自 2009 年的 mingw-64，
那时这两个函数还没进 `ws2tcpip.h`（`ws2tcpip.h` 里只有 `INET_ADDRSTRLEN` 宏）。

⚠️ **这不是"缺个声明"那么轻，是静默出错**：

```c
PCSTR inet_ntop(...)     /* 返回的是**指针** */
```

缺原型时 C89 隐式声明按 `int` 返回 —— 32 位下侥幸能用，
**64 位下指针被截断成 4 字节**，随后解引用就是访问违例或读到垃圾。
与 P2 记录的 `GetACP` 隐式声明污染同函数其它调用是同一类问题。

已在 `tccwin.h` 补上声明：

```c
__declspec(dllimport) INT   __stdcall inet_pton(INT Family, LPCSTR pszAddrString, PVOID pAddrBuf);
__declspec(dllimport) PCSTR __stdcall inet_ntop(INT Family, const VOID *pAddr, PSTR pStringBuf, size_t StringBufSize);
```

### 补声明时踩的一个子坑：**不能加 `#if defined(_WINSOCK2API_)` 守卫**

第一版我写成"只在已包含 winsock 时声明"，结果声明**永远不会生效**：

```
tccwin.h 按约定是第一个被包含的头 -> 那时 _WINSOCK2API_ 还没定义
winsock2.h 之后又 #include "tccwin.h" -> 被 TCCWIN_H 守卫挡住，不再处理
```

**规则：`tccwin.h` 里不要用"稍后才会定义的宏"做守卫。**
`socklen_t` 用 typedef 自身的守卫（`TCWIN_SOCKLEN_T`）防重复定义即可。

另外 `WINSOCK_API_LINKAGE` / `WSAAPI` 也是 `winsock2.h` 才定义的，
在 `tccwin.h` 里用会报 `';' expected (got "INT")`，
所以直接用 `__declspec(dllimport)` + `__stdcall`。

### 附带修掉的

`verify.ps1` 原来检查 `include\winsock2.h` 是否存在，现改为检查
`toolchain\tcc\include\winapi\winsock2.h` 与 `ws2tcpip.h`（自检 66 -> 67 项）。

### 未验证边界

- 重叠 I/O（`WSARecv`/`WSASend` 配 `OVERLAPPED`）与 IOCP 的**运行行为**
  未做端到端测试，只确认声明可用。自写垫片时期也未测过。
- `inet_pton`/`inet_ntop` 只测了 `AF_INET` 的 `127.0.0.1` 与 `AF_INET6` 的 `::1`
  两种往返，未测非法输入的错误码路径。
---

## P63. **要验证某样东西是否必要，必须先把它拿掉再测**

这是本项目**第 3 次同类方法错误**，单独记一条，因为它的适用范围远超本环境。

### 三次犯的错误

| # | 错误测法 | 得到的错误结论 | 正确测法 |
|---|---|---|---|
| 1 | `tcc.exe -I <portable>\include` 测 TCC 能力 | "TCC 不支持 `InterlockedIncrement`" 等 | **不加任何 `-I`** |
| 2 | 漏 `-L lib\tccdefs` 测链接 | "`strtoll` / `GlobalMemoryStatusEx` 编译器不支持" | 链接**必带 `-L`** |
| 3 | 用**含 `tccwin.h`** 的测试判"垫片能否删" | "这 5 个垫片可删" | **删掉垫片再测** |

三次都是同一个病根：**测试环境里带着被测对象本身**，
于是"能用"是被测对象自己造成的，却被当成了"不需要它"的证据。

### 第 3 次的具体经过（值得完整看一遍）

`tccwin.h` 里有 5 个共享垫片：`InterlockedExchangePointer`、
`InterlockedCompareExchangePointer`、`InterlockedAnd64`、`InterlockedOr64`、
`InterlockedXor64`。

我用 `#include "tccwin.h"` 写测试 → 5 个两位宽都 OK → 判定可删。
删掉之后 `scripts\tcc.cmd t.c -Quiet -32` 立刻报：

```
warning: implicit declaration of function 'InterlockedExchangePointer'
undefined symbol 'InterlockedExchangePointer'
undefined symbol 'InterlockedAnd64'          （5 个全报）
```

**教训**：带着垫片测出来的 "OK" 只证明"垫片在工作"，
完全不证明"没有垫片也行"。

### 规则

**判定"X 是否必要"的测试，源文件里不能含 X，也不能含引入 X 的东西。**

对本环境具体就是两种测法分工明确，不要混：

| 测法 | 用途 | 写法 |
|---|---|---|
| **A** | 判"TCC 是否自带" | 源文件只 `#include <windows.h>`，**不含 `tccwin.h`**；用 `toolchain\tcc\tcc.exe -L lib\tccdefs` 与 `i386-win32-tcc.exe -L lib\tccdefs32` |
| **B** | 判"用 `tccwin.h` 时能不能用" | `scripts\tcc.cmd t.c -Quiet [-32]` |

**两者结果不同是正常的，也是关键信号** —— 差异恰好说明垫片在做实事。

### 附带发现的依赖链（下一轮别踩）

`tccwin.h` 的 i386 块里，`InterlockedExchangeAdd` 看着"32 位自带、可删"，
但它其实是**整块的基础**：

```
i386: InterlockedExchangeAdd              <- LOCK xaddl，基础
        └─ InterlockedAdd
        └─ InterlockedExchangeAdd64 ──> InterlockedIncrement64 / Decrement64
      InterlockedCompareExchange64        <- LOCK cmpxchg8b，基础
        └─ InterlockedExchange64
```

**删基础会连带打断 64 位整数的四个操作。** 判定单个函数"自带"不等于"可删"，
还要看块内有没有别的实现依赖它。

完整判据与判定流程见 [`research/NEXT-shim-cleanup.md`](research/NEXT-shim-cleanup.md)。
---

## P64. UTF-8 输入：`uChar.AsciiChar` 会静默吃掉非 ASCII 字符

### 症状

`tcwinreadline` 读不进中文 —— 输入"中"得到的是 `0x2D`（`'-'`），而不是报错。
这是**静默的错误数据**，比崩溃更难查。

### 原因

Windows 控制台的 `KEY_EVENT_RECORD.uChar` 是一个 `union`：

```c
union {
    WCHAR UnicodeChar;   /* 真正的 UTF-16 字符 */
    CHAR  AsciiChar;     /* 只有低字节 */
} uChar;
```

原来的代码是：

```c
ev->ch = rec->Event.KeyEvent.uChar.AsciiChar;   /* ← 只取低字节 */
```

"中" 的 UTF-16 是 `U+4E2D`，低字节正是 `0x2D`。于是字符被**静默替换**，
既不报错也不崩溃。这与 P59 记录的编码类问题是同一族。

### 修法

改取 `UnicodeChar`，经 `tcwin_u8_from_utf16()` 转 UTF-8，
多字节部分放进事件结构新增的 `u8_tail`：

```c
WCHAR wc = rec->Event.KeyEvent.uChar.UnicodeChar;
if (wc >= 0x20 && wc != 0x7F) {
    char u8[8];
    int n8 = tcwin_u8_from_utf16(&wc, 1, u8, (int)sizeof u8);
    if (n8 >= 1) {
        ev->ch = u8[0];
        if (n8 > 1) { /* 其余字节进 ev->u8_tail[0..2]，长度进 u8_tail_len */ }
    }
}
```

**向后兼容设计**：`u8_tail_len` 默认 0 时行为与以前完全一致（只插 `ch` 一个字节），
所以已有的 94 项 readline 断言无需修改就继续通过。

### 顺带修掉的：光标按字节数定位

行编辑器原来所有编辑操作都按字节：

| 操作 | 原行为 | 现行为 |
|---|---|---|
| 退格 | 删 1 字节（中文留下半个 -> 乱码） | 删 1 个**码点** |
| Delete | 删 1 字节 | 删 1 个码点 |
| 左右移 | 移动 1 字节（光标停在半个字里） | 移动 1 个码点 |
| 光标列 | `strlen(prompt) + cursor`（字节数） | **显示宽度**（中文算 2 列） |

纯逻辑抽到 `include/tcwin_rl_utf8.h`（不依赖 `tccwin.h`，可独立测试），
逐函数断言见 `examples/probes/probe_rl_utf8.c`（98 项）。

### 未验证边界

- **emoji（U+10000 以上）在真实控制台上会显示成 U+FFFD**：控制台把代理对
  分两次送按键，本实现不做跨事件拼合。
- 组合字符（变音符）、印度语系连写、零宽连接符按 1 列算 ——
  不是完整的 East Asian Width 表。做终端表格对齐不能依赖它。
- `WriteConsoleA` 输出多字节 UTF-8 的**真实显示效果未在真机控制台验证**
  （自动化测试只能验证字节与状态，验证不了屏幕）。需要人工确认一次。

---

## P65. 测试辅助函数的缓冲区溢出会伪装成库的 bug

### 现象

给 `probe_readline.c` 加 UTF-8 用例时，同一段代码在**单字符**输入下正常、
在**两字符**输入下失败：

```
[FAIL] UTF-8：中文 6 字节
[FAIL] UTF-8：退格删掉整个 文（3 字节）   得到 "" 光标=0，期望 "中" 光标=3
```

看起来很像是行编辑器的码点处理有 bug。

### 真因在测试侧

测试辅助函数把自己写溢出：

```c
/* 错误：u8_tail 只有 3 字节，却按整串长度拷贝 */
e.u8_tail_len = n - 1;
for (k = 1; k < n; k++) e.u8_tail[k - 1] = s[k];    /* n=6 -> 写 5 字节 */
```

而结构体布局是：

```c
typedef struct tcwin_rl_event {
    int  key;
    char ch;
    char u8_tail[3];     /* ← 只有 3 字节 */
    int  u8_tail_len;    /* ← 紧跟其后，被溢出覆盖 */
} tcwin_rl_event;
```

写 5 字节越过 `u8_tail[3]`，**把紧邻的 `u8_tail_len` 覆盖成垃圾值**。
实测抓到 `u8_tail_len = 34710`。

**为什么只在两字符时暴露**：3 字节的 UTF-8 字符（如"中"）只需 2 个 tail 字节，
不溢出；6 字节（两个汉字）需要 5 个，才踩到。所以现象是"有的用例过、有的不过"。

### 教训

1. **"有的用例过、有的不过"要先怀疑测试数据长度，而不是库逻辑。**
   同一函数在不同输入长度下行为分叉，往往是因为测试侧有长度相关的内存问题。
2. **加打印 `tail_len` 这种"中间值"比加打印最终结果更快定位** ——
   最终结果 `len=3` 看不出问题，中间值 `34710` 一眼就知道是被覆盖了。
3. 辅助函数也要按生产代码的标准做边界钳位：

```c
tail = n - 1;
if (tail > (int)sizeof e.u8_tail) tail = (int)sizeof e.u8_tail;   /* 必须钳位 */
e.u8_tail_len = tail;
for (k = 0; k < tail; k++) e.u8_tail[k] = s[1 + k];
```

### 附带：一次按键事件只送一个字符

同一轮里还纠正了一个**测试语义**错误：一开始把 6 字节（两个汉字）
塞进**一个**事件，然后断言长度是 6。但真实控制台是**每个字符一次
`KEY_EVENT`**，所以正确做法是分两次喂。

编辑器那时插入第一个字符、丢弃多余的 tail，**行为是对的**，
错的是测试的期望值。教训同上：断言不符合真实协议时，先改测试。
---

## P66. SQLite 序列化模式**不保护跨语句的事务**

### 结论

`sqlite3_threadsafe()` 返回 1 时 SQLite 是序列化模式，承诺"多线程可安全
共用同一个连接"。但**这个承诺只覆盖每一次 API 调用，不覆盖由多次调用组成的事务**。

实测（`probe_sqlite_mt.c` 场景 1 的第一版，8 线程共享一个连接）：

```c
/* 每个线程这样写 —— 看起来合理，实际会失败 */
sqlite3_exec(db, "BEGIN IMMEDIATE", ...);   /* 线程 A 拿到锁，BEGIN，让出 */
sqlite3_exec(db, "UPDATE counter SET n = n + 1", ...);
sqlite3_exec(db, "COMMIT", ...);
```

结果 8 个线程里只有 3 个跑完，其余立刻拿到：

```
SQLITE_ERROR (1): cannot start a transaction within a transaction
```

线程 A 做完 `BEGIN` 后让出连接锁，线程 B 也做 `BEGIN` —— 于是在**同一个
连接上开出了嵌套事务**。序列化模式不会阻止这件事，因为两次 `BEGIN`
各自都是原子的。

最终计数 153（期望 4000），大量更新丢失。

### 正确用法

**共享连接时，每个线程只发一条自身原子的话句：**

```c
sqlite3_exec(db, "UPDATE counter SET n = n + 1", ...);   /* 单条语句自身原子 */
```

改成这样后场景 1 通过：8 线程 × 500 次 = **4000，一次不丢**。

**如果确实需要跨语句事务，就必须自己加外部互斥**把整个
BEGIN..COMMIT 包起来，不能依赖 SQLite 的连接级锁。

### 各场景实测结论（`probe_sqlite_mt.c`，28 项断言）

| 场景 | 配置 | 结果 |
|---|---|---|
| 1 | 8 线程**共享一个连接**，各发单条原子 UPDATE | 4000 次计数**不丢**，完整性 ok |
| 2 | 4 线程**各用独立连接**写同一文件库（WAL） | 800 次写入全成功，0 BUSY，0 错误，行数与成功次数交叉校验一致 |
| 3 | WAL 下 3 读者 + 1 写者并发 | 读者取样 **600000 次**，**从未读到半个事务**（行数始终是批次倍数） |
| 4 | 线程模式声明 | `THREADSAFE=1` + `MUTEX_W32`，即序列化模式 |
| 5 | 把上面那个坑固化成断言 | 第二次 `BEGIN` 报 `SQLITE_ERROR`，消息含 `within a transaction` |

**结论**：本环境的 SQLite 构建在并发下是**可靠的**，但有上述使用约束。
场景 2/3 说明多连接并发与 WAL 读写并发都没有问题。

### 顺带记两个测试自身的坑（都不是 SQLite 的问题）

**坑 1：写者异常退出时读者永久空转。**
场景 3 原来只在函数末尾置 `stop = 1`，于是任何提前 `break`
都会让读者卡在 `while (!c->stop)` 里**永久空转**，探针挂死。
现象很像"并发死锁"，实际是测试 bug。

修法：**每条退出路径都置位**（改成 `goto out` 单一出口），
并给读者加**硬上限**兜底。

**坑 2：把"当前工作目录可写"当成了前提。**
探针原本用相对路径建库。自检脚本会 `Push-Location` 到
`$env:TEMP` 下的随机子目录 —— 那里通常可写，但**依赖调用方的 cwd
是脆弱的**。实测在某个受限目录下运行时 SQLite 报 `SQLITE_CANTOPEN`，
现象像"SQLite 不能建库"，其实是路径不可写。

修法：用 `GetTempPathA` 显式取临时目录（见探针里的 `tmp_db_path()`）。

### 未验证边界

- 只测到 **8 线程**、**600000 次读样本**。更高并发/更长时长未测。
- 未测 `SQLITE_BUSY` 的**重试退避策略**（场景 2 里 busy_timeout=5s
  足够所以一次 BUSY 都没出现；没有把超时压到必然触发的程度）。
- 未测多进程并发（只测了多线程）。
- 未测 `PRAGMA journal_mode=MEMORY` / `OFF` 等非 WAL 模式下的行为。
- `probe_sqlite_mt.c` 在自检里跑约几十秒，是本环境最慢的探针。
---

## P67. Nuklear 加载 CJK 字体：两个约束与一个 260 倍的代价

### 结论速查

| 项 | 结论 |
|---|---|
| 能否渲染中文 | **能**。`simhei.ttf`（9.3 MB）加载正常，汉字字形齐全 |
| 图集代价 | **取决于 `nk_font_config.range`** —— 见下表，差 **260 倍** |
| 中文宽度 | 是 ASCII 的**两倍**（18px vs 9px @ 18pt），符合全角特征 |
| 硬约束 | `range` 里每个区间必须 **f <= t**，否则 abort |
| 集成约束 | 用 `stbtt_*` 必须先 `#define NK_IMPLEMENTATION` |

### 图集体积的实测对比（simhei.ttf，16px，ALPHA8）

| range 配置 | 图集尺寸 | 代价 |
|---|---|---|
| 只 ASCII（0x20-0x7E） | 512 × 32 | **0.02 MB** |
| ASCII + 25 个界面汉字（显式列举） | 512 × 64 | **0.03 MB** |
| nuklear 内置 `nk_font_default_glyph_ranges()` | 512 × 64 | 0.03 MB |
| nuklear 内置 `nk_font_chinese_glyph_ranges()` | 1024 × 8192 | **8.00 MB** |
| ASCII + CJK 基本区（0x4E00-0x9FFF） | 1024 × 8192 | **8.00 MB** |

**全量中文 8 MB，只烘焙用到的字 0.03 MB —— 差 260 倍。**

所以工程上的正确做法是：**明确列出界面里真正会显示的汉字**，
而不是图省事用 `nk_font_chinese_glyph_ranges()`。

（如果确实需要任意中文输入/显示，那 8 MB 就是必须付的代价。
这一点要在设计阶段决定，不是运行时能省的。）

### 约束 1：每个区间必须 f <= t

`nk_font_config.range` 是一个 `nk_rune` 数组，成对出现（起、止），以 0 结尾。
nuklear 在 `nk_range_count()`（`nuklear.h:16962`）上有断言：

```c
nk_rune f = range[(i*2)+0];
nk_rune t = range[(i*2)+1];
NK_ASSERT(t >= f);              /* ← 不满足就直接 abort */
```

实测踩到：手写码点时写成 `0x6587, 0x6587 - 2`（t 比 f 小 2），探针直接：

```
Assertion failed: t >= f, file C:/.../nuklear.h, line 16962
```

**注意不要求整体升序** —— nuklear 内置的 `nk_font_chinese_glyph_ranges()`
自己就把 `0xFF00` 排在 `0x4E00` 前面：

```c
NK_STORAGE const nk_rune ranges[] = {
    0x0020, 0x00FF,
    0x3000, 0x30FF,
    0x31F0, 0x31FF,
    0xFF00, 0xFFEF,      /* ← 比下一行大，但合法 */
    0x4E00, 0x9FAF,
    0
};
```

**只要求每个区间自身 `f <= t`。**

### 约束 2：用 `stbtt_*` 必须先 `#define NK_IMPLEMENTATION`

`nuklear.h` 的**声明段**会先包含 `stb_truetype.h` 并设下
`__STB_INCLUDE_STB_TRUETYPE_H__` 守卫，此时 `stbtt_fontinfo` 只有前置声明。

于是只写 `#define STB_TRUETYPE_IMPLEMENTATION`（想自己提供实现）
是**没用的** —— 随后的 `#include "stb_truetype.h"` 会被守卫整个跳过：

```
error: 'stbtt_fontinfo' undeclared
```

**正确做法**（与 `examples/probes/probe_stbtt.c` 一致）：

```c
#define NK_IMPLEMENTATION          /* ← 关键：开实现段 */
#include "nuklear_tcc_compat.h"
#include "nuklear.h"
```

`NK_IMPLEMENTATION` 段里 nuklear 自己会 `#define STB_TRUETYPE_IMPLEMENTATION`
再包含 `stb_truetype.h`，于是 stbtt 的完整 API 与实现一次到位。

### 另一个易错点：量文本宽度不能直接调 `nk_font_text_width`

它的签名是回调形式：

```c
float nk_font_text_width(nk_handle handle, float height, const char *text, int len);
```

它**已经挂在 `font->handle.width` 上**，不是"给我宽度"的便捷函数。
直接 `nk_font_text_width(font, "中")` 会报
`cannot cast 'struct nk_font *' to 'union <anonymous>'`。

正确写法：

```c
float w = font->handle.width(font->handle.userdata,
                             (float)font->handle.height, "中", 3);
```

### 验证

`examples/probes/probe_nk_cjk.c`（**22 项断言**，自检项 69 → 70）：
自动探测 `%WINDIR%\Fonts` 下的中文字体（`simhei.ttf` / `Deng.ttf` /
`simkai.ttf` / `simsun.ttc` / `msyh.ttc`），也可以传路径作第一个参数。
四组：原始 stbtt 解析、全量烘焙、5 种 range 代价对比、中文端到端排版。

### 未验证边界

- **没有真实窗口**。验证的是"图集建起来了、字形拿到了、宽度对了、
  nuklear 文本路径跑通没崩"，**不是**"屏幕上显示出了正确的中文"。
  真实显示需要人工跑一次 GUI 示例。
- 只用 `simhei.ttf` 测过。`.ttc` 集合（`simsun.ttc` / `msyh.ttc`）
  虽然代码里会尝试，但**未实测**其子字体选择是否正确
  （`stbtt_GetFontOffsetForIndex(buf, 0)` 取的是第 0 个）。
- 只测了 16pt / 18pt。更大字号下图集线性增长，未测。
- 未测多个 `nk_font_config` 合并（`merge_mode`）时的表现。
---

## P68. `<dirent.h>` 谎报能力：声明齐全，12 个符号一个都没导出

### 症状与性质

TCC 自带 `toolchain/tcc/include/dirent.h`，它**完整声明了 POSIX 目录接口**：

```c
struct dirent { long d_ino; unsigned short d_reclen, d_namlen; char *d_name; };
typedef struct { struct _finddata_t dd_dta; struct dirent dd_dir;
                 long dd_handle; int dd_stat; char dd_name[1]; } DIR;
DIR* opendir(const char*);   struct dirent* readdir(DIR*);
int  closedir(DIR*);         void rewinddir(DIR*);
long telldir(DIR*);          void seekdir(DIR*, long);
/* 外加 _w 宽字符的 6 个 */
```

但**这些符号 msvcrt.dll 一个都没有导出**（实测：`tcc -impdef
%WINDIR%\System32\msvcrt.dll` 生成导出表后逐个查，`opendir` / `readdir` /
`closedir` / `rewinddir` / `telldir` / `seekdir` 以及 `_wopendir` /
`_wreaddir` / `_wclosedir` / `_wrewinddir` / `_wtelldir` / `_wseekdir`
**全部无导出**）。

于是任何 `#include <dirent.h>` 的移植代码都在**链接期**失败：

```
undefined symbol 'opendir'
```

**这是"头文件谎报能力"，比缺头文件更隐蔽 —— 因为编译能过。**
同类风险：只看"头文件在不在"就下结论，会以为这个能力可用。

### 修法：已经修好了

`helper/dirent_compat.c` 用 msvcrt **真正有**的三个函数补齐了窄字符的 6 个：

```
msvcrt 有导出：  _findfirst   _findnext   _findclose
msvcrt 无导出：  opendir readdir closedir rewinddir telldir seekdir
```

TCC 的 `DIR` 结构体本身就是照着 `_findfirst` 族设计的
（`dd_dta` / `dd_handle` / `dd_stat` 与 mingw 原始实现一一对应），
所以补起来是同构的，**不需要改 dirent.h**（"不改 TCC 头文件"是本项目原则）。

用法：

```c
#include <dirent.h>
...
scripts\tcc.cmd app.c -With dirent        /* tcc.ps1 已登记 */
```

`rewinddir` 用"关掉再重新 `_findfirst`"实现（路径存在 `dd_name` 柔性数组里）；
`seekdir` 用"rewind + 连续 readdir 跳过"，O(n) 但语义正确。

验证：`examples/probes/probe_dirent.c`（**24 项断言**）——
基本遍历、到底后持续 NULL、`telldir`/`seekdir`/`rewinddir` 位置，
以及**与 `FindFirstFile` 独立实现的项数交叉校验**。

### ⚠️ 宽字符版（`_wopendir` 等 6 个）是**真实死路**，故意不实现

不是懒，有三个硬障碍：

1. **类型尺寸对不上**。TCC 的 `_WDIR.dd_dta` 是 `struct _wfinddata_t`
   （32 位时间，约 50 字节）；而 msvcrt **只导出 `_wfindfirst64` /
   `_wfindnext64`**，要求 `struct _wfinddata64_t`（64 位时间，约 560 字节）。

2. **TCC 的宏指向一个不存在的符号**。`io.h:147`：

   ```c
   #define _wfindfirst _wfindfirst64i32
   ```

   而 `_wfindfirst64i32` **在 msvcrt.dll 里没有导出**（`_findfirst` 那一侧
   有 stub 所以能过，`_w` 这一侧没有）。实测报：
   `undefined symbol '_wfindfirst64i32'`。

3. **绕过它的唯一办法是改 `dirent.h` 的 `_WDIR` 定义**（把 `dd_dta` 换成
   `_wfinddata64_t`），那就违反"不改 TCC 头文件"的原则了。

**结论：TCC 的宽字符目录接口在本环境下无法实现。**
需要 Unicode 目录遍历时用 **`tinydir`**（本环境自带，走 `FindFirstFileW`，
原生 Unicode），或直接用 Win32 `FindFirstFileW`。

### 未验证边界（`dirent_compat.c` 文件头也写了）

- 只测过当前目录与普通子目录。**超长路径（>4096）返回 NULL**；
  UNC 路径与 `\\?\` 前缀路径**未测**。
- `d_ino` / `d_reclen` 恒为 0 —— 依赖它们的代码（如按 inode 去重）不可用。
- `seekdir` 是 O(n)，大目录下慢。
- `DIR` 不是线程安全的：同一 `DIR` 不能多线程同时 `readdir`
  （不同 `DIR` 互不影响）。
- 未测"遍历过程中目录被删除/改名"的行为。

---

## P69. `unistd.h` 只要 include 就编译不过（`useconds_t` 从未被定义）

### 症状

    #include <unistd.h>

得到：

    unistd.h:24: error: ',' expected (got "useconds")

**是编译失败，不是链接失败** —— 而且报错没点名 `useconds_t` 这个标识符，
容易误判成头文件损坏。

### 根因：两条错的前提叠在一起

TCC 的 `unistd.h` 第 21-25 行：

    #ifndef __NO_ISOCEXT
    #include <sys/types.h>        /* For useconds_t. */   <-- 这个注释是错的
    int __cdecl __MINGW_NOTHROW usleep(useconds_t useconds);
    #endif

实测两件事：

1. **`sys/types.h` 里根本没有 `useconds_t`**。它只定义 time_t /
   __time32_t / __time64_t / ino_t / dev_t / pid_t / mode_t / off_t /
   off64_t / timespec / itimerspec。
2. **整个头文件树（1012 个头，含 winapi/ 772 个）里都没有
   `useconds_t` 的 typedef。**

所以 `useconds_t` 是未知标识符，声明解析失败。
而 `usleep` 在 `msvcrt.dll` 里**也没有导出**（实测 `tcc -impdef` 查过），
所以光让它解析通过还不够，实现也得自己补。

### 修法：已经修好了

`helper/unistd_compat.h`（声明）+ `helper/unistd_compat.c`（实现），
由 `-With unistd` 用 `-include` 在**用户源码之前**注入头文件，
同时把 `.c` 编进目标文件：

    scripts\tcc.cmd app.c -With unistd

`usleep` 用 `Sleep()` 实现，微秒转毫秒时**向上取整** —— 向下取整会让
`usleep(1000)`（1ms）变成 `Sleep(0)` 提前返回，在"轮询等待"的代码里
会退化成忙等。

验证：`examples/probes/probe_unistd.c`（**11 项断言**）。

### 一个实现细节：声明与实现必须分成两个文件

把两者放同一个文件会得到：

    'usleep' defined twice

因为 `-include` 和普通源文件编译是**两条独立路径**，都会处理那个文件。
必须拆成 `.h`（被 `-include`）+ `.c`（被编译）—— 这也是
`msvcrt_compat.h` / `msvcrt_compat.c` 的既有约定。

### 实测数据：`usleep` 的真实粒度

| 请求 | 实际 | 说明 |
|---|---|---|
| `usleep(0)` | < 1 ms | 走 `Sleep(0)`，让出时间片 |
| `usleep(1000)` | **16 ms** | 小值被系统粒度放大 |
| `usleep(50000)` | 78 ms | 50ms 请求被放大到 78ms |
| `10 x usleep(10000)` | 187 ms | 累积也偏大 |

**Windows `Sleep()` 的最小粒度约 15.6 ms**（取决于系统定时器分辨率），
所以 **`usleep` 不能用于精确延时**。要精确计时用 `GetTickCount64` 轮询
或 `QueryPerformanceCounter`。

### 未验证边界

- 只补了 `usleep`。`unistd.h` 里的 **`ftruncate` 没修** —— 它走
  `__CRT_INLINE` + `_chsize`，而 `_chsize` 在 msvcrt.dll 里**也没有导出**
  （实测）。用到 `ftruncate` 的代码仍会在链接期失败。
  探针里只断言"声明可见"，**没有**断言可链接。
- `usleep` 返回值恒为 0，不模拟被信号打断（EINTR）。
- 未测多线程下 `usleep`（`Sleep` 本身线程安全）。
- 精度数据来自本机（Windows 10 22H2），其它机器可能不同。

---

## P70. **更正**：TCC x64 **确实定义了 `_WIN32`**

### 结论

项目此前的记录里写过"TCC defines _WIN64 but NOT _WIN32 on x64"。
**实测这是错的** —— TCC 0.9.27 x64 **同时定义了 `_WIN32` 与 `_WIN64`**，
与 MSVC / MinGW 在 x64 下的行为一致。

### 为什么这条错误前提值得单独记

它是**方法论级**的隐患：`#ifdef _WIN32` 是移植代码里最常见的平台判断。
如果读者以为"TCC x64 不定义 `_WIN32`"，就会写出

    #if defined(_WIN64) && !defined(_WIN32)     /* 基于错误前提的绕法 */

这类多余的、甚至有害的分支。**错误的前提比缺信息更糟** ——
它会主动引导出错误的修法。

### 残留位置的修正

`include/nuklear_tcc_compat.h` 的注释里曾写"TCC 在 x64 上只定义 _WIN64，
不定义 _WIN32"。**那句话已改正。**

值得注意的是：那个注释的**结论（NK_SIZE_TYPE 落到 4 字节）是对的**，
但**归因错了**。真正的原因是 nuklear 的每一档判定都还额外要求一个 TCC
没有的宏 —— 第 1/3 档要 `_MSC_VER`，第 2/4 档要 `__MINGW64__`/
`__MINGW32__` 或 `__clang__`，第 5 档要 `__GNUC__`/`__clang__`。
`_WIN32` 是定义了的，但同一分支还要 `_MSC_VER`。

**教训：结论对不代表归因对。归因错了，后人照着改就会改错地方。**

### 怎么发现的

在审计 `unistd.h` 失败链时，我怀疑 `sys/types.h` 第 9 行的
`#ifndef _WIN32 #error Only Win32 target is supported!` 被触发了，
于是实测了一下 `_WIN32` —— 结果是**已定义**，我的怀疑不成立，
真正原因是 `useconds_t` 缺失（见 P69）。

**顺带说明**：`sys/types.h` 单独 `#include` 是**能过的**。
看到那个 `#error` 很容易误以为它有问题。

### 验证方式

写一个只含下面内容的 .c，**刻意不加任何 -I**（测的是 TCC 自己的预定义宏），
用 toolchain\tcc\tcc.exe 编译：

    #if defined(_WIN32)
    #error _WIN32_IS_DEFINED
    #endif
    #if defined(_WIN64)
    #error _WIN64_IS_DEFINED
    #endif
    int main(void){return 0;}

`#error` 触发即说明对应宏已定义。

---

## P71. gettimeofday / ftime：声明齐全但底层符号全无导出（含一个签名陷阱）

### 缺口

实测（`tcc -impdef %WINDIR%\System32\msvcrt.dll` 查导出表）：

    gettimeofday        无导出
    mingw_gettimeofday  无导出
    ftime / _ftime      无导出
    _ftime64            无导出

而头文件里**声明是有的**：`sys/time.h:42` 声明 `mingw_gettimeofday`，
`:55-56` 声明 `gettimeofday`；`sys/timeb.h:98` 声明 `_ftime64`。

**后果**：`#include <sys/time.h>` 本身能过，但**调用 `gettimeofday` 的
移植代码在链接期失败**。

### 修法：已经修好了

`helper/time_compat.c`，由 `-With time` 启用。用 Win32 自己算，
不依赖 msvcrt：`GetSystemTimeAsFileTime` 取 UTC，再转 Unix 纪元
（1601 -> 1970 相差 11644473600 秒）。

验证：`examples/probes/probe_timecompat.c`（**15 项断言**），
关键是**与 `time()` 的交叉校验**（差值必须在 ±2 秒内）。

### 陷阱 1：`ftime` 不缺实现，缺的是它调用的 `_ftime64`

`sys/timeb.h:114-124` **已经提供了内联的 `ftime`**：

    __CRT_INLINE void __cdecl ftime(struct timeb *_Tmb) {
      _ftime64((struct __timeb64 *)_Tmb);
    }

所以 `ftime` 只是个转发，真正缺导出的底层是 `_ftime64`。

**自己定义 `ftime` 会得到 `redefinition of 'ftime'`**（实测踩到）。
只需实现 `_ftime64`。

### 陷阱 2：`ftime` 的返回类型是 `void`，不是 `int`

TCC 的 `sys/timeb.h:120` 把它定义成 `__CRT_INLINE void`，
而 **MSDN / 老 mingw 的 `ftime` 返回 `int`（成功 0）**。

照抄 MSDN 写 `if (ftime(&tb) != 0)` 会报：

    cannot cast from/to void

实测踩到。**这里不能照抄 MSDN，必须以本头文件的实际签名为准。**
正确的用法是直接调用，然后检查结构体是否被填好。

### 陷阱 3：`gettimeofday` 的第二参数是 `void *`，不是 `struct timezone *`

`sys/time.h:55-56`：

    int __cdecl gettimeofday(struct timeval *__restrict__,
                             void *__restrict__  );

第二参数是 **`void *`**（注释里管它叫 tzp）。写成 `struct timezone *` 会得到
`incompatible types for redefinition of 'gettimeofday'`（实测踩到）。
只有 `mingw_gettimeofday`（`:42`）用的是 `struct timezone *`。

**注意**：`sys/time.h` 的注释明确说 "The timezone pointer arg is ignored"，
所以实现里**完全忽略**第二参数 —— 依赖它拿时区的移植代码会拿到未修改的
内存。这是与 POSIX 语义的差异。

### 实测数据：精度受系统定时器粒度限制

`probe_timecompat.c` 第 2 组测"两次连续调用相差多少微秒"，实测：

    两次调用相差 0 us

因为 `GetSystemTimeAsFileTime` 的粒度约 15.6 ms，短时间内多次调用会返回
**同一个值**。所以 `gettimeofday` **不能用于测量耗时或精确计时** ——
要精确计时用 `QueryPerformanceCounter`。

### 顺带确认可用的（本轮一起测了，**不需要补**）

这些"看着可疑"的头文件实测都能编译+链接：

    sys/stat.h + stat() / fstat() / mkdir()      可用
    sys/fcntl.h + fcntl.h + open()               可用
    sys/utime.h + utime()                        可用
    conio.h + getch() / kbhit()                  可用
    process.h + getpid()                         可用
    sys/unistd.h / sys/locking.h / sys/file.h    只 include 可用
    string.h 的 strdup / strcasecmp / stricmp /
      strcmpi / memicmp / memccpy                可用
    process.h 的 execl                           可用

**注意 `strdup` / `strcasecmp` 这类**：它们**不在**任何 `.def` 文件里，
但**实际能链接**（TCC 自己的库提供了）。所以

> **不要用"符号在不在 `.def` 里"判断可用性 —— 必须实测编译+链接。**

### 未验证边界

- 只测过"能编译+链接+与 `time()` 一致"。**没有**与系统时间逐秒对照，
  也没有测跨时区/夏令时切换。
- `ftime` 的 `timezone` 用 `GetTimeZoneInformation().Bias`，符号方向按
  POSIX 惯例（西为正）。本机实测 `-480`（即 UTC+8），**未在其它时区验证**。
- `_ftime32` **没实现**（本环境 `time_t` 是 64 位，32 位变体无用途）。
- `gettimeofday` 的 `tv_usec` 低位多为 0（粒度问题，见上）。
- 未测多线程并发调用（实现无共享状态，理论上安全，但未验证）。

---

## P72. 头文件可用性全量审计（结论：剩余覆盖完整）

### 为什么要做这次审计

P68（`<dirent.h>` 12 个符号一个都没导出）与 P69（`<unistd.h>` 只要
include 就编译不过）暴露出一类**系统性风险**：

> **头文件存在 ≠ 能力可用。** 编译能过但链接失败（或更糟，
> 隐式声明导致静默截断）才是这类问题的真实形态。

于是把 TCC 自带 include 目录里**所有对外入口**系统测了一遍。

### 测法

每个用例分两种：

  1. 只 `#include` —— 验证头文件本身能否解析
  2. 真实调用一个典型函数 —— 验证编译**与链接**都过

一律用 `scripts\tcc.cmd`（带齐 `-I` 与 `-L lib\tccdefs`），
以 `$LASTEXITCODE` 判定。

### 结果：55 个用例，53 可用，2 个已修

**第一批（21 个）—— 系统/IO 类**

| 头文件 / 调用 | 结果 |
|---|---|
| `sys/stat.h` 只 include；`stat()` / `fstat()` / `mkdir()` / `S_ISDIR` / `S_IWRITE` | 可用 |
| `sys/fcntl.h`、`fcntl.h` 只 include；`open()` | 可用 |
| `sys/utime.h` 只 include；`utime()` | 可用 |
| `conio.h` 只 include；`getch()` / `kbhit()` | 可用 |
| `process.h` 只 include；`getpid()` / `execl()` | 可用 |
| `sys/unistd.h`、`sys/locking.h`、`sys/file.h`、`sys/timeb.h` 只 include | 可用 |
| `string.h` 的 `strdup` / `strcasecmp` / `strncasecmp` / `stricmp` / `strcmpi` / `memicmp` / `memccpy` | 可用 |
| `sys/time.h` 只 include | 可用 |
| **`sys/time.h` + `gettimeofday()`** | **曾缺 —— 已修（P71）** |
| **`sys/timeb.h` + `ftime()`** | **曾缺 —— 已修（P71）** |

**第二批（31 个）—— 语言/运行时类**

| 头文件 / 调用 | 结果 |
|---|---|
| `locale.h` + `setlocale()` / `localeconv()` | 可用 |
| `signal.h` + `signal()` | 可用 |
| `setjmp.h` + `setjmp()` | 可用 |
| `assert.h` + `assert()` | 可用 |
| `errno.h` + `errno` | 可用 |
| `limits.h` + `INT_MAX` / `float.h` + `DBL_MAX` | 可用 |
| `stdarg.h` + `va_list` | 可用 |
| `stdbool.h` + `bool` / `stdint.h` + `int64_t` / `inttypes.h` + `PRId64` | 可用 |
| `wchar.h` + `wcslen()` / `wctype.h` + `iswalpha()` | 可用 |
| `strings.h` + `strcasecmp()` / `memory.h` | 可用 |
| `malloc.h` + `alloca()` | 可用 |
| `io.h` + `_access()` / `direct.h` + `_getcwd()` | 可用 |
| `time.h` + `strftime()` / `gmtime()` | 可用 |
| `stdio.h` + `snprintf()` / `fopen()`+`fread()` / `remove()` / `rename()` | 可用 |
| `stdlib.h` + `qsort()` / `getenv()` / `system()` | 可用 |
| `string.h` + `strerror()` / `ctype.h` + `toupper()` | 可用 |

### 一条重要的方法论更正

审计中途我一度想用"符号在不在 `.def` 里"**筛选**可疑项，于是把
`strdup` / `strcasecmp` / `stricmp` / `memicmp` / `memccpy` 全列进了
"疑似缺失"。**实测它们全部可用** —— TCC 自己的库提供了实现，
不经过那些 `.def`。

> **不要用 `.def` / 导出表列表判断可用性，必须实测编译+链接。**

（导出表仍有用：查"msvcrt 里到底有没有这个符号"时它是权威的，
如 P69/P71 的判定。但它**不能反推**"这个符号一定不可用"。）

### 结论

**TCC 自带头文件集在常用范围内覆盖完整。** 本轮修掉的两个
（`dirent.h` / `unistd.h`）是仅有的两个"死头文件"；
`gettimeofday` / `ftime` 是仅有的两个"声明了但无实现"的常用函数。

如果后续发现新的同类问题，**按本文的两种测法复现即可**：
先只 include（看解析），再真实调用（看链接）。

### 未验证边界

- 只覆盖**常用**头文件。`ddk/`（138 个驱动开发头）、COM/OLE 系列、
  `winscard` / `winspool` / `mapi` 等专业领域头**未测**。
- 每个头文件只测了 1-2 个代表函数，不是全部声明。
- 只测 64 位。32 位（`-32`）下同一批用例**未跑过** —— 自检里的
  32 位项只覆盖少数几个探针。
- 未测"能链接但运行时行为错误"的情形（本轮只做编译+链接判定，
  行为验证在各专用探针里）。

---

## P73. **32 位下 float 数学函数大面积不可用**（原记录只在 64 位成立）

### 项目原来的记录是错的（至少不完整）

"TCC 上真正不可用的 float 函数只有 4 个（fabsf / truncf / cbrt /
nearbyintf）" —— **这个结论只在 64 位成立**。

实测 32 位（`scripts\tcc.cmd -32`），下面这些**全部 `undefined symbol`**：

    sqrtf sinf cosf tanf powf floorf ceilf fmodf atan2f
    logf log10f expf asinf acosf atanf sinhf coshf tanhf
    copysignf truncf

另外 `fabsf` 报 `unknown constraint 't'`。

**共 22 个函数在 32 位下不可用，而不是 4 个。**

### 根因：一个位宽条件写错，让一半目标不可用

`tccmath.h` 里那层修复当时被这条守卫挡住：

    #if defined(__x86_64) || defined(_WIN64)      <-- 只判 64 位

所以 32 位下整块被跳过。

**为什么 32 位也需要它**（读 `_mingw.h:54` 并用 `-E` 展开确认）：

    _mingw.h:54   #define __CRT_INLINE extern __inline__

`extern` 覆盖了 `inline` 的内部链接语义，所以 `math.h` 里那些
`__CRT_INLINE float powf(...) {...}` **只是声明、不带定义**。
`math.h:247` 的 `#ifndef __x86_64` 在 32 位下确实成立，但那只放行了
**声明**；msvcrt 里没有 `powf` 的实现，链接期一样 undefined。

**结论：这个问题与位宽无关，32/64 位都需要这层转发。**
当时只判 64 位，是因为**只在 64 位下测过就下了结论**。

### 修法

守卫扩宽为 `__x86_64 || _WIN64 || __i386__ || _X86_`，
并补上 32 位额外缺的 5 个（`sinhf` / `coshf` / `tanhf` /
`copysignf` / `modff`，它们的双精度版在 32 位可用）。

实测结果：**32 位 24/24 全部可用，64 位 24/24 无回归**。

### 验证

`examples/probes/probe_mathgap.c`（**42 项断言**，两位宽都跑）：
逐函数真实调用并**检查数值结果**（不是只编译），
包括 `sqrtf(4)==2`、`powf(2,10)==1024`、`sinhf(1)≈1.17520` 等。

### 教训

**"只在一种配置下测过"不能写成"不可用/可用"的结论。**
这个错误比缺信息更糟 —— 它让 32 位用户以为数学库没问题。
本项目的自检当时只有 1 个 32 位用例（`sizeof(void*)` + 两个 API），
覆盖不到库层面。

### 未验证边界

- 只测了 24 个常用 float 函数。`float.h` 的宏、`fenv.h` 的舍入模式
  控制、long double 版本（`sqrtl` / `fabsl`）**未系统测**。
- 数值断言用 1e-4 的相对误差，未做 ULP 级比对。
- 未测极值 / NaN / Inf / 次正规数在各函数上的行为。

---

## P74. `modff` 宏造成**栈越界写**（我自己引入的，已修）

### 症状：一半对、一半错，且静默破坏内存

曾把 `modff` 写成宏（在 `tccmath.h` 里）：

    #define modff(x,ip) ((float)modf((double)(x), (double *)(ip)))

看着像"转发到双精度版"，**实际是错的**：`modf` 会往第二参数写
**8 字节**（double），而调用方按 `modff` 的签名给的是
**4 字节的 `float`**。两个后果：

1. **整数部分的值不对** —— 实测 `modff(3.75f,&f)` 得到 `int=0.00`
   而不是 `3.00`
2. **越界写 4 字节栈内存** —— 静默破坏相邻变量

最阴险的是：**小数部分（返回值）恰好是对的**，所以只看返回值不会发现。

### 修法：宏做不到，必须用真函数

正确实现要声明一个 `double` 中转：

    float modff(float x, float *iptr) {
        double di = 0.0;
        double df = modf((double)x, &di);
        if (iptr) *iptr = (float)di;
        return (float)df;
    }

这需要真正的函数体，**宏做不到**。所以放在 `helper/math_compat.c`，
由 `-With math` 提供；`tccmath.h` 里只留声明。

**刻意不保留宏**：不在 `-With math` 时调用 `modff` 会**链接失败**。
这是有意的 —— **宁可链接失败，也不要静默越界写**。

### 验证

`probe_mathgap.c` 第 6 组：除了断言整数部分正确，还放了一个
**哨兵变量**检测越界写：

    float guard = 12345.0f;
    fr = modff(3.75f, &ip);
    ... 断言 guard 仍是 12345.0f

两位宽实测：`int=3.00` / `int=9.00` / 哨兵未变 / exit=0。

### 教训

**"指针出参"是宏转发的雷区。** 单参数函数转发安全，
但只要函数往指针里写数据，就必须检查**目标类型的宽度**是否一致。
`frexpf` 是反例（出参是 `int *`，与 `frexp` 一致，宏安全）；
`modff` 是正例（`float *` vs `double *`，宽度不同）。

### 未验证边界

- 只测了常规值（3.75f / 9.5f / -2.25f）。**未测** ±0.0 / ±Inf /
  NaN / 次正规数 / |x| > 1.6e7 的收窄行为。
- 原计划同时提供 `frexpf`，但实测它**两边都已有定义**
  （`math.h` 里那份 `__CRT_INLINE` 是带定义的），
  再写一遍会 `redefinition` —— 所以本文件**只提供 `modff`**。
  这也印证了"补之前先确认真的缺"。

---

## P75. **TCC 32 位跑不了 SQLite**（64 位正常；gcc 32 位正常）

### 现象

32 位下 `sqlite3_initialize()` 访问违例（`0xC0000005`）。
崩溃点在这个函数**内部**，`sqlite3_libversion()` 与 `sqlite3_threadsafe()`
都正常返回。

### 证据：编译器交叉验证（这是判定的关键）

| 编译器 | 位宽 | 结果 |
|---|---|---|
| TCC 0.9.27 | 64 | 正常，全套 CRUD 通过 |
| **TCC 0.9.27** | **32** | **访问违例 0xC0000005** |
| MinGW gcc 6.3.0 | 32 | 正常，全套 CRUD 通过 |

**同一份 `sqlite3.c` 源码，gcc 32 位正常而 TCC 32 位崩** ——
所以这是 **TCC 的 32 位缺陷**，不是 SQLite 的问题，也不是本项目包装的问题。

gcc 侧的实测输出（`gcc -m32 -O0`，未加任何垫片）：

    A libversion=3.53.4
    B threadsafe=1
    C os_nt=1
    D initialize rc=0
    E open rc=0
    G create rc=0
    H insert rc=0
    I prepare rc=0
    J value=42
    K done

### 已排除的原因（逐个实测，都不是）

1. **`long long` 运算** —— 32 位下除法/取模/移位/无符号与 64 位
   结果**逐位一致**（`1234567890123/1000000`、`0xFFFFFFFFFFFFFFFF/1000` 等）。
2. **线程原语** —— `CRITICAL_SECTION`（32 位 24 字节）、
   `InterlockedIncrement/Decrement/Exchange/CompareExchange/ExchangeAdd`、
   `TlsAlloc/TlsSetValue/TlsGetValue/TlsFree`、`GetLastError` 全部正常。
3. **`malloc` 的 8 字节对齐** —— 32 位下 20/20 次都返回 8 字节对齐地址，
   含 `long long` / `double` 的结构体读写全部正确。
4. **栈被 `OSVERSIONINFOA` 写坏** —— 插入金丝雀变量，崩溃前后**都未被破坏**。
   （`OSVERSIONINFOA` 本身确实有尺寸问题，见下，但不是崩溃原因。）
5. **宏配置** —— `SQLITE_WIN32_GETVERSIONEX=0`、`SQLITE_THREADSAFE=0`、
   `SQLITE_OMIT_WAL` 各种组合都崩，只是崩溃位置略有移动。

**未定位到具体代码生成点** —— 只确定崩溃发生在 `sqlite3_initialize()` 内。
这是一条**诚实的未完成**：结论（TCC 32 位不可用）有充分证据，
根因（TCC 哪一处代码生成错了）未查明。

### 处理方式：让它编译失败，而不是产出会崩的 exe

`lib/sqlite/sqlite3_tcc.c` 里加了编译期守卫：

    #if defined(__i386__) && !defined(SQLITE_TCC_ALLOW_BROKEN_32BIT)
    #error "SQLite 用 TCC 编 32 位会在 sqlite3_initialize() 崩溃…"
    #endif

**一个会崩的二进制比一个编译错误危险得多** —— 静默崩溃是最糟的失败模式。
确实要绕过（例如自己排查这个 TCC 缺陷）就定义
`SQLITE_TCC_ALLOW_BROKEN_32BIT`。

替代做法：
   - 用 64 位（默认，不加 `-32`）—— 实测完全正常
   - 32 位确实需要时用 MinGW gcc：`C:\\MinGW\\bin\\gcc.exe -m32`

### 验证

`examples/probes/probe_sqlite32.c`（**15 项断言**）：64 位下做完整 CRUD
（含绑定参数、`sum()` 的 i64 路径、`count()`），32 位下会被守卫拦住。
这样"32 位不可用"就成了每次自检都会验证的事实，而不是文档里的一句话。

### 顺带发现的头文件缺陷（独立问题）

`toolchain/tcc/include/winapi/winnt.h:5095`：

    typedef struct _OSVERSIONINFOA {
      ...
      CHAR szCSDVersion[128];      <- 应为 WCHAR[128]
    } OSVERSIONINFOA;

实测 `sizeof(OSVERSIONINFOA) == 148`，而**真实 Windows API 里是 276**
（`szCSDVersion` 是 `WCHAR[128]`，不管结构体是 A 还是 W 版）。
同文件 `:5104` 的 `OSVERSIONINFOW` 用的是 `WCHAR[128]`，尺寸 276，是对的。

**但这不影响可用性**：实测 `GetVersionExA` 返回成功且没有溢出
（`platform=2, major=6`）—— 真实 API 是探测/截断写入的。
后果只是**拿不到完整的 SP 描述字符串**。

`ddk/winddk.h:4098` 用的是同样写法，所以这是上游的固有差异，
不是本项目引入的。**不要改 TCC 头文件**（本项目原则），此条仅作记录。

### 未验证边界

- **未定位 TCC 32 位的具体代码生成缺陷**（见上）。
- 只测过 SQLite 3.53.4 amalgamation。其它版本未测。
- 未测 32 位下"部分可用"的可能性（例如某些宏组合能绕过）——
  只测了 5 类组合，都崩。
- `OSVERSIONINFOA` 的 276/148 差异只在**本机**验证了运行时无溢出，
  未在其它 Windows 版本上验证。
- gcc 侧只用了 `-O0`，未测 `-O2`。

---

### 根因找到了（后续轮次补充）

上面写的"未定位到具体代码生成点"**已经解决**。

#### 一句话

**把 `__stdcall` 函数指针存进 `void (*)(void)` 槽位，取出时再强转回真实签名
调用 —— 这个模式在 TCC 0.9.27 的 32 位模式下会栈溢出；64 位正常。**

SQLite 的 Windows 层正是这么做的（`sqlite3.c` L49294）：

    typedef void (*sqlite3_syscall_ptr)(void);        /* 无参、cdecl */
    static struct win_syscall {
      const char *zName;
      sqlite3_syscall_ptr pCurrent;                   /* <- void(*)(void) 槽位 */
      sqlite3_syscall_ptr pDefault;
    } aSyscall[] = {
      { "GetVersionExA", (SYSCALL)GetVersionExA, 0 }, /* <- __stdcall 函数存进去 */
      ...
    };
    #define osGetVersionExA ((BOOL(WINAPI*)(LPOSVERSIONINFOA))aSyscall[34].pCurrent)
                                                      /* <- 取出时强转回真实签名 */

#### 定位过程（每一步都排除了一个假设）

1. 用 `fprintf(stderr)` **无缓冲**追踪（`stdout` 缓冲会在崩溃时丢输出 ——
   第一版就吃了这个亏），把崩溃点从"`sqlite3_initialize()` 里某处"
   收窄到 **`sqlite3_win32_is_nt()`**（`sqlite3.c` L50188，30 行的小函数）。
2. 逐个测该函数里的每个调用（`GetVersionExA` 直接调 / 经函数指针 /
   经数组、`InterlockedCompareExchange` 直接调 / 经函数指针）——
   **全部正常**。
3. **逐字复刻**那个函数（同样的函数指针宏、同样的 `volatile` 全局、
   同样的 `OSVERSIONINFOA`）—— **32 位下也正常**。
4. 于是怀疑"真实 TU 里的差异"。写了一个最小复现：把 3 个签名不同的
   函数（无参 `__stdcall`、两参 `__stdcall`、两参 `__cdecl`）强转进
   `void (*)(void)` 槽位，再按真实签名取出调用 ——
   **32 位崩（`0xC00000FD` 栈溢出），64 位正常**。

#### ~~关键观察：32 位下取到的不是真入口，而是 thunk~~

> ## ⛔ 本小节结论**已被推翻，不要采信**
>
> 这里据"相邻函数地址差 17 字节"推断是 thunk。**直接 dump 机器码后证明不是** ——
> `__stdcall` 与 `__cdecl` 的起始字节都是 `55 89 E5`（push ebp / mov ebp,esp），
> 都是正常函数序言。17 字节只是极简函数体本身的长度。
>
> 真正的触发条件是**栈清理约定不一致**，与 thunk 无关。
> **完整更正见本条目后面的「⚠️ 更正：不是 thunk」小节。**
>
> 保留这段是为了留下"我怎么错的"这个记录 —— 地址间隔这类间接迹象
> 不足以支撑机制结论。

    32 位:  &f_std_0 = 00401011   &f_std_2 = 00401022   &f_cdecl_2 = 0040103E
           <- 相邻函数只差 0x11 = 17 字节
    64 位:  &f_std_0 = 401094     &f_std_2 = 4010A6     &f_cdecl_2 = 4010C9
           <- 正常函数入口

这解释了 SQLite 那 **80 条 pointer/integer 警告** —— 它们全部出现在
`#define osXxx ((...WINAPI*)aSyscall[n].pCurrent)` 这类宏的**使用处**
（L50157 / 50434 / 50443 / 50461 / 50470 / 50490 / 50499 / 50519 / 50528 / 50620 …）。
而 64 位下**同一条命令 0 条警告** —— 这个对比本身就是强信号。

#### 复现

`examples/probes/probe_stdcall_voidptr.c`（**8 项断言**）：

    scripts\tcc.cmd examples\probes\probe_stdcall_voidptr.c -Run        # 64 位：8/8 通过
    scripts\tcc.cmd examples\probes\probe_stdcall_voidptr.c -Run -32    # 32 位：崩溃（就是缺陷本身）

**探针只在 64 位下注册进自检** —— 32 位崩溃是预期行为。

#### 仍然未查明的部分（诚实标注）

- **TCC 内部为什么这样生成**没有查。只确定了现象与触发条件。
- **未找到绕过办法**。试过在 `#include sqlite3.c` **之后**重定义那些
  `os*` 宏为直接调用 —— **无效**，因为函数体在 `#include` 时就已经
  编译完了。要真正绕过需要在 include 之前介入，而宏是在文件各处定义的，
  暂无干净做法。
- 只测了 `__stdcall`。**未系统测** `__cdecl` / `__fastcall` 经槽位中转，
  以及"槽位类型与真实签名一致"时（即不做签名转换）是否也崩。
  探针里 `f_cdecl_2` 那一项在 32 位下没跑到（前面先崩了）。
- 未测这个缺陷是否影响**其它用函数指针表的库**（有此模式的都应当心）。

#### 精确边界（后续轮次补充实测）

上面写"这个模式会崩"还不够精确。实测把边界收窄到**调用约定**：

| 模式 | TCC 32 位 | TCC 64 位 |
|---|---|---|
| `__cdecl` 经 `void(*)(void)` 槽位（无参） | 正确 | 正确 |
| `__cdecl` 经 `void(*)(void)` 槽位（两参） | 正确 | 正确 |
| **`__stdcall` 经同一模式** | **崩** | 正确 |

所以中招的是**调用约定转换**，不是"函数指针经槽位"本身。

**这缩小了风险面**：只有把 `__stdcall`（Win32 API 的默认约定）函数指针
塞进 `void(*)(void)` 或其它 `__cdecl` 槽位时才会触发。纯 C 代码里
`void(*)(void)` 装 `__cdecl` 函数是安全的。

崩溃码不止一种，观测到两种：
- `0xC00000FD` 栈溢出（探针里无参 `__stdcall` 那一项）
- `0xC0000005` 访问违例（SQLite 的路径、以及探针里另一种调用形态）

#### 绕过尝试（失败，记录以免重复走）

既然知道了触发条件，试过两条绕过路径，**都失败**：

1. **在 `#include sqlite3.c` 之后**重定义那些 `os*` 宏为直接 Win32 调用。
   **无效** —— 函数体在 `#include` 时就编译完了，后定义不影响已编译的代码。
2. **`SQLITE_WIN32_GETVERSIONEX=0`** 绕过第一个崩点。
   **部分有效** —— `sqlite3_win32_is_nt()` 确实不再崩（返回 1），
   但 `sqlite3_initialize()` 里还有别的崩点，仍崩。

   顺带确认了一件事：**`osInterlockedCompareExchange` 在默认配置下
   没有走函数指针表** —— 因为 `sqlite3.c` L49802 是
   `#if defined(InterlockedCompareExchange)`，而 TCC 的 `windows.h` 把它
   定义成了宏，于是走 L49805 的 `#define osInterlockedCompareExchange
   InterlockedCompareExchange`（直接调用）。所以崩的不是它，
   而是相邻的 `osGetVersionExA`（它确实是函数指针宏）。

   这条细节有价值：**崩点易被误判** —— 我第一轮就以为崩在
   `osInterlockedCompareExchange` 上，实际它是直接调用。

#### 为什么最终没有做 32 位绕过补丁

`sqlite3.c` 里有 **70 个 API** 经这张表访问（`osHeapAlloc` / `osCreateFileA` /
`osReadFile` / `osMultiByteToWideChar` …），全部是 `__stdcall`。
要绕过就得**逐个**把这些宏改成直接调用，那是 70 处补丁打在第三方
9.5 MB 源码上 —— 维护成本远超收益，且每次升级 SQLite 都要重打。

**所以维持现状**：32 位编译期报错 + 明确指引用 64 位或 MinGW gcc。
这个决定是有依据的，不是回避。

#### ⚠️ 更正：不是 thunk（后续轮次实测推翻）

上面写"32 位下取到的不是真入口，而是 17 字节间隔的 thunk"。
**那条推断是错的，这里更正。**

错在哪：我只看了**地址间隔**（相邻函数差 0x11 = 17 字节）就推断是 thunk，
没有直接看机器码。后来直接 dump 函数起始字节，结果是：

    32 位:  s_0 @00401011 : 55 89 E5 81 EC 00 00 00
            c_0 @00401038 : 55 89 E5 81 EC 00 00 00
            (s_0/s_2 是 __stdcall，c_0/c_2 是 __cdecl)

    `55` = `push ebp` —— **都是正常函数序言，两种调用约定起始字节相同。**
    17 字节间隔只是这些极简函数体（push ebp / mov ebp,esp / sub esp,0 …）
    本身的长度，与 thunk 无关。

**所以触发条件与 thunk 无关。** 精确表述应当是：

> 32 位下把 `__stdcall` 函数指针存进 `__cdecl` 槽位（如 `void(*)(void)`）
> 再转回真实签名调用，TCC 生成的调用代码与 `__stdcall` 的被调方**在栈清理
> 上不一致**，于是崩溃。

**具体是哪一侧的代码生成错了（调用方还是被调方）没有查明** ——
这是诚实的未完成。判断"栈清理不一致"的依据是：
`__stdcall` 由被调方清栈、`__cdecl` 由调用方清栈，两者混用必然错位，
而 `__cdecl` 经同一槽位完全正常（见上表）。

**方法论教训**（与 P70 那条同类）：
**地址间隔这种间接迹象不足以支撑机制结论。** 我把它写成了"是 thunk"，
读一下机器码就能证伪。下次遇到"看起来像 X"的机制，先直接测 X 本身。

---

## P76. `stbsp_sprintf` 是**无界**的 —— 要安全必须用 `stbsp_snprintf`

### 症状与性质

`stb_sprintf` 提供两个格式化函数，名字只差 `n`，但安全性完全不同：

    stbsp_sprintf(buf, fmt, ...)       <- **无界**，不做任何截断保护
    stbsp_snprintf(buf, n, fmt, ...)   <- 有界，标准 snprintf 语义

这与 CRT 的直觉不同：CRT 里 `sprintf` 也确实危险，但很多人以为
"stb_sprintf 是 sprintf 的可靠替代品"就顺手把 `sprintf` 全换成
`stbsp_sprintf` —— **那就把一个已知的不安全函数换成了另一个不安全的**。

### 怎么发现的

写 `probe_misc.c` 验证 stb_sprintf 时，我用 `stbsp_sprintf` 往 8 字节
缓冲写 16 字符的 `"0123456789ABCDEF"`，本意是"验证截断保护"。
实测：**16 个字符全写进去了**（越界 8 字节），断言失败。

第一反应是"stb_sprintf 有 bug"，但查 API 后确认是**我用错了函数** ——
有界的是 `stbsp_snprintf`。改成它之后：

    stbsp_snprintf(small, 8, "%s", "0123456789ABCDEF")
    -> small = "0123456"（7 字符 + NUL，正确截断）
    -> 返回 16（本应写入的长度，标准 snprintf 语义）

### 修法

**项目里凡是往固定缓冲区格式化，必须用 `stbsp_snprintf`。**
`-With stb_sprintf` 仍然提供两个函数，但**不要用无界的那个**。

### 验证

`examples/probes/probe_misc.c` 第 3 组（**21 项断言**，全部通过）：

- `stbsp_sprintf` 基本格式 / 十六进制 / 浮点 / 宽度左对齐 —— 正确
- `stbsp_snprintf` 截断后有 NUL 结尾 —— 通过
- `stbsp_snprintf` 截断到 7 字符 —— 通过
- `stbsp_snprintf` 返回本应写入的长度 16 —— 通过

### 未验证边界

- 只测了 `%d %s %c %x %.2f` 与宽度/左对齐。**未测** `%b`（二进制）、
  千分位分隔、`%e`/`%g`、`%p`、宽字符等 stb_sprintf 的扩展功能。
- **未测** `stbsp_sprintf` 在"目标缓冲足够大"之外的行为（它本来就没有
  边界可测 —— 这正是问题）。
- 未测 `stbsp_vsnprintf` / `stbsp_vsprintf` 两个变体。
- 结论基于 stb_sprintf.h 当前版本（`include\stb_sprintf.h`，60129 B）。

### 顺带：本条目来自「无探针库」的补验证

用 `scripts\check-with.ps1` 核对时发现 30 个登记库里只有 17 个有探针，
另 13 个（`stb_sprintf` / `jsmn` / `utf8` / `xxhash` / `sha256` / `log.c` /
`inih` / `tinyexpr` / `lua` / `stb_image` / `stb_image_write` /
`stb_rect_pack` / `stb_c_lexer`）**功能从未被验证过**。

新增 `probe_misc.c` 补上其中三个（`jsmn` / `sha256` / `stb_sprintf`），
**真实调用并检查行为**：

- `jsmn`：解析一个 4 键 JSON，验证 token 数、对象/数组/字面量类型、
  数组元素个数，以及**截断 JSON 返回错误**
- `sha256`：与 **FIPS 180-4 的公认向量**比对 —— `SHA256("abc")` 与
  `SHA256("")` 两条（不是自己跟自己比），另加确定性与雪崩检查
- `stb_sprintf`：见上

**剩 10 个库仍无探针**：`utf8` / `xxhash` / `log.c` / `inih` / `tinyexpr` /
`lua` / `stb_image` / `stb_image_write` / `stb_rect_pack` / `stb_c_lexer`。
其中 `xxhash` 实际被 `probe_hash` 覆盖、`stb_image` 被 `probe_image` 覆盖
（只是名字没出现在 verify.ps1 的 $map 映射里），所以真实缺口比 13 小。

---

## P77. 只用探针覆盖的库都要验证 **API 语义**，不只是"名字对"

### 背景：一类"能编译、能链接、但用不对"的陷阱

本项目已经四次因为**猜 API** 而写错代码。前三次是"名字不对"（编译报错，
很快发现）；第四次是"**语义不对**" —— 名字猜对了、编译过了、链接过了，
但结果全错，编译器不会给任何提示。

| 库 | 猜的 | 真相 | 表现 |
|---|---|---|---|
| `utf8.h` | `utf8_sz` / `utf8_valid` | `utf8len` / `utf8valid` | 编译失败 |
| `stb_c_lexer` | `CLEX_qliteral` | `CLEX_dqstring` | 编译失败 |
| `stb_rect_pack` | `STBRP_IMPLEMENTATION` | `STB_RECT_PACK_IMPLEMENTATION` | 链接失败 |
| **`stb_c_lexer`** | **`get_token()` 的返回值是 token ID** | **ID 在 `lexer->token`** | **静默给全 0** |

### 第四次（最危险的那种）：`stb_c_lexer_get_token`

`stb_c_lexer.h` L153-156 的注释写得很清楚：

    extern int stb_c_lexer_get_token(stb_lexer *lexer);
    // this function returns non-zero if a token is parsed, or 0 if at EOF
    //   - lexer->token is the token ID, which is unicode code point for a
    //     single-char token, < 0 for a multichar or eof or error

**返回值只是"是否还有 token"（0/非 0）；真正的 token ID 在 `lexer->token`。**

我第一版写 `tok = stb_c_lexer_get_token(&lex);` 然后 `switch (tok)`，
于是所有分类计数**全是 0** —— 而编译无错误、无警告、链接正常。
如果不是探针里有断言（"识别出 2 个标识符"），这个错会一路带进
真实代码。

### 教训：读库 API 的正确姿势

1. **先读头文件**，不要凭记忆或常识猜名字
2. **不只看函数名与签名，还要看紧邻的文档注释** ——
   尤其是**返回值语义**与**输出参数**（`lexer->token` 这类）
3. 探针里**必须有行为断言**，不能只验证"能编译+能链接"
   （不然就是 P78 里说的那种"假通过"）

### 相关的检测强度问题（P78）

本轮还发现 `check-with.ps1` 用 `int main(void){return 0;}` 编每个 `-With`
名字，**从不调用任何库函数** —— 所以它连"4 个库缺实现宏导致完全链接不了"
都抓不到（详见 P78）。**"能编译"离"能用"还有很远。**

### 未验证边界

- 这条是"方法论条目"而非"具体缺陷"，所以**没有对应的探针**。
  它的落实方式是在每个探针里写行为断言（已有 29 个探针这样做）。
- 四个例子里只有第四个是静默错误；前三个都会被编译器/链接器挡住。
  但**不能因此假设静默错误罕见** —— 只验证过这 4 个库。

---

## P78. `check-with.ps1` 检测强度不足：4 个库"假通过"了很久

### 问题

`scripts\check-with.ps1` 用来核对 `-With` 库登记表，做法是**对每个登记名**
编译这样一个程序：

    int main(void){return 0;}

**它从不调用任何库函数。** 所以"登记存在 + 路径正确 + 能编译"三条全满足，
但库可能**完全用不了**。

### 后果：4 个库一直是"假通过"

`stb_c_lexer` / `stb_rect_pack` / `stb_image` / `stb_image_write` 这 4 个
头文件型库的 `defs=` 里缺 `*_IMPLEMENTATION` 宏，**链接期全部 undefined
symbol**，但 check-with.ps1 一直报 "all 30 -With names compile+link"。

（这 4 个已在后续轮次修好，见提交 "修复 4 个头文件型库缺 *_IMPLEMENTATION 宏"。）

### 为什么探针没抓到 `stb_image`

`probe_image.c` **自己在源码里 `#define STB_IMAGE_IMPLEMENTATION`**
（L4-5、L11-12），所以探针能过 —— 但 `-With stb_image` 这个**入口本身**
从没被真正验证过。用户按文档写 `-With stb_image` 就会撞上 undefined symbol。

**教训：探针自己定义了实现宏，就测不到 `-With` 的登记是否正确。**
两者是**不同的东西**：探针测"库的功能"，`-With` 测"登记表让人能用上库"。

### 改进方向（**已完成**，后续轮次）

把 check-with.ps1 从「空 main 能否编译」升级为
「**调用该库至少一个真实符号**能否链接」。

**已完成**：30 个库里 **29 个**有真实符号检查。

    OK   29 libraries link a REAL symbol
    WEAK no real-symbol case (empty-main only): lua

过程中学到两个非显然的点（都写进 check-with.ps1 的注释）：

1. **`-With` 只加 include 路径和宏定义，不加 `#include` 本身。**
   生成的测试源码必须自己 `#include` 库头，否则报 `'sqlite3' undeclared`
   这类错 —— 看着像库的问题，其实是测试程序的问题。（这就是 `$hdrs`
   映射存在的原因。）
2. **有些库需要「库头之前的源码」**：nuklear 要
   `#include "nuklear_tcc_compat.h"` + `#define NK_IMPLEMENTATION`
   **在** `#include <nuklear.h>` **之前**。（这就是 `$preHdr` 映射。）

**反证（证明这个检查不是摆设）**：把 `stb_c_lexer` 的 `defs` 临时改回
`defs=@()`，新检查立刻报

    FAIL [stb_c_lexer] real-symbol link failed: undefined symbol 'stb_c_lexer_get_token'

—— 正是它要抓的那类缺陷（旧版会报 "all 30 compile+link"）。
恢复后回到 `RESULT: consistent`。

#### `lua` 为什么仍是 WEAK —— 设计差异，不是遗漏

`-With lua` 的登记是 `src=@()`，**只提供头文件**（给嵌入 Lua 的 C 代码用）。
真正的 Lua 实现对象由 `scripts\build-lua.cmd` 链接（33 个 Lua 源文件），
**不经过 `-With`**。

所以「用 `-With lua` 链接一个真实符号」**在设计上就不成立** ——
没有可链接的实现。

同类但**能**进强检查的：`utf8io.h` 全是 `static __inline`（无外部符号），
但它的 inline 函数可以被调用，所以能覆盖。

**这类"设计上没有外部符号"的库，强检查覆盖不到是正常的。**

### check-with.ps1 现在证明什么

    1. LIBDEF 里登记了这个名字
    2. inc= / src= 指向的路径真实存在（Test-Path 实测）
    3. 用这个名字编译一个空程序能过
    4. **29 个库还能链接一个真实符号**（新增，这是关键补强）

仍然**不证明**这 29 个库的功能正确 —— 那是探针的职责（`examples\probes\`，
30 个探针）。`lua` 只覆盖 1~3（设计差异，见上）。

### 未验证边界

- 只对 30 个库里的 16 个「头文件型」（`src=@()`）做了实现宏检查。
  其余 14 个有独立 `.c` 源文件（不需要实现宏），但**"有 .c"不等于
  "功能正确"** —— 那是各探针的事。
- **未逐个验证**这 30 个库是否还有其它"能编译但不能用"的情形
  （例如缺某个必需的可选宏、或 API 语义陷阱见 P77）。
- 检测强度的改进**未实施**。

---

## P79. 「名字没出现」不等于「没有覆盖」—— 一次错误清单的教训

### 我做了什么错事

我用一个启发式产出了一份"缺口清单"：

> 「30 个 `-With` 登记库里只有 17 个有探针，另 13 个功能从未被验证过」

依据是「库名是否作为字符串出现在 `verify.ps1` 里」。然后**基于这份清单
做了四轮工作**（补 `probe_misc` / `probe_utf8lib` / `probe_clexer` /
`probe_misc3`）。

**那份清单是错的。**

### 错在哪

`verify.ps1` 的 `$map` 是「**探针文件名 → -With 库名**」的映射：

    $map = @{
        'probe_hash.c'     = 'xxhash,sha256'
        'probe_image.c'    = 'stb_image,stb_image_write'
        'probe_encoding.c' = 'utf8io'
        ...
    }

库名出现在**映射的值**里，不会作为独立字符串出现在别处。
所以「库名没出现」这个判据，把**已覆盖**的库误判成未覆盖。

### 澄清后的真实情况

| 我以为"无探针"的库 | 真实探针 | `-With` |
|---|---|---|
| `xxhash` | `probe_hash.c` | `xxhash,sha256` |
| `stb_image` | `probe_image.c` | `stb_image,stb_image_write` |
| `stb_image_write` | `probe_image.c` | 同上 |
| `utf8io` | `probe_encoding.c` | `utf8io` |

`probe_encoding.c` **真的** `#include "utf8io.h"` 并调用 `u8_wide_to_cp()`
（L19、L70）—— 不是名义覆盖。

**所以 30 个库全部有功能探针。** 之前那份清单里的条目，逐个核实后
都是名字匹配假象。

### 顺带澄清 lua

`verify.ps1` L255-266 已对 lua 做**端到端验证**：

    & build-lua.ps1 -Interpreter -Out $vslua      # 真实构建静态解释器
    $o = & $vslua -e "io.write(tostring(0xF0|0x0F)..' '.._VERSION)"
    Report 'build-lua' ("$o" -match '255')

把 `-With lua` 的弱检查说成"lua 验证不足"**不准确** ——
lua 走的是 build-lua 这条完整路径。

### 已做的修正

`check-with.ps1` 的报告措辞改了：原来 "WEAK no real-symbol case" 容易被
读成"这个库没被验证"。现在它**解析 `verify.ps1` 的 `$map`**（而不是
硬编码，避免漂移），把 WEAK 分成两类：

    WEAK no call expr here, but verify.ps1 covers it: <lib -> probe>
    WEAK no call expr AND no functional probe in verify.ps1: <lib>

当前输出只剩 `lua` 一个 —— 而它由 build-lua 端到端覆盖。

### 方法论教训

**间接指标不足以判定"有无覆盖"。** 正确做法是**逐个实测**：
打开探针文件看它 `#include` 了什么、`$map` 里登记了什么 `-With`。
我用字符串匹配推断，产出了一份错误清单，并基于它做了四轮工作 ——
其中三轮是**白做**的（那些库本来就有探针）。

这与项目已记录的另两条同类错误一致：

| 条目 | 错误的类型 |
|---|---|
| P70 | 结论对但**归因错**（nuklear 的 `NK_SIZE_TYPE`）|
| P75 更正 | 结论**根本错**（把地址间隔推断成 thunk）|
| **P79（本条）** | **缺口清单错**（把名字匹配当成覆盖判定）|

三次都是**用一个间接迹象代替直接测量**。

### 未验证边界

- 只**逐个核实了 4 个**库（`xxhash` / `stb_image` / `stb_image_write` /
  `utf8io`）的探针确实调用了该库。其余 26 个按 `$map` 推定。
  **所以"30 个库全部有功能探针"这个结论的强度是：抽查 4 个都对，
  其余未逐一打开探针确认。**
- `$probeMap` 的解析用行正则，若 `verify.ps1` 的 `$map` 改成多行写法
  会漏解析。影响有限：漏解析只会让报告退回更保守的 "no call expr"，
  不会漏报问题。
- 未统计"探针的断言是否真的覆盖了该库的关键行为"（只确认了有调用）。

---

## P80. 「宽度不一致」陷阱在本项目出现了**第二次**（`ARGPARSE_OPT_FLOAT`）

### 与 P74 同类

P74 记的是 `modff` 宏：把 `float *` 当 `double *` 传给 `modf`，于是
`modf` 往 4 字节变量里**写 8 字节**。

P80（本条）是**同一个陷阱的另一个实例**，而且这次发生在**库的 API 约定**上：

写 `probe_argparse.c` 时我写了

    double ratio = 0.0;
    RAW_OPT(ARGPARSE_OPT_FLOAT, 'r', "ratio", &ratio, "ratio")

结果 `ratio` 是垃圾值 `5.31535e-315`（连数量级都不对）。

查 `argparse.c:104`：

    *(float *)opt->value = strtof(self->optvalue, (char **)&s);

**argparse 内部按 `float *` 写值，只写 4 字节。** 用 `double` 接就是
4 字节写进 8 字节 —— 高位留着未初始化（或栈上残留），于是得到垃圾。

改成 `float ratio = 0.0f;` 后 `ratio == 2.5` 正确。

### 为什么这类陷阱反复出现

两个实例的共同点：**函数/宏的名字里没有类型信息，编译器和链接器也不报错。**

| 实例 | 表面写法 | 实际写入 | 后果 |
|---|---|---|---|
| P74 `modff` 宏 | `modff(x, &float_var)` | 8 字节 | 越界写 4 字节 + 整数部分错 |
| P80 `ARGPARSE_OPT_FLOAT` | `&double_var` | 4 字节 | 高 4 字节未初始化，值成垃圾 |

两者都是**静默的**：编译通过、链接通过、不崩（P80 这种甚至不越界），
只是值不对。

### 可操作的判据

**当把一个"输出参数指针"交给库时，必须确认库往里面写几个字节。**

具体做法（本项目已用过的）：

1. **读实现源码**，找 `*(类型 *)opt->value = ...` 这类赋值，看 `类型`
   （P80 就是这么找到的）
2. **读头文件**的字段声明与文档注释（P74 是靠这一条）
3. **探针里加"值合理性"断言**，不只断言"调用成功"。
   P80 如果只断言 `argparse_parse` 返回 0，就完全发现不了 ——
   是 `ck("ratio ≈ 2.5", ...)` 这条把它暴露出来的

### 未验证边界

- 本条是**方法论条目**，没有对应的探针（落实方式是在探针里写值断言）。
- 只统计了本项目已知的**两个**实例。同类陷阱在其它库里的存在情况
  **未系统排查** —— 30 个库里凡是接受"输出参数指针"的 API 都应当心。
- P80 的具体现象（垃圾值 `5.31535e-315`）是**本机一次观测**，
  不同编译/栈布局下具体数值会不同（但"值不对"这个结论稳定）。

#### 实测补充（后续轮次）：TCC **不给任何警告**，且守护变量未受损

用一段最小复现（`void *` 形参模拟库的输出参数）在 TCC 下实测：

    static void lib_write_float(void *out) { *(float *)out = 2.5f; }

    double d = 0.0;
    lib_write_float(&d);      /* 错：库只写 4 字节 */

结果：

| 场景 | 结果 | TCC 警告 |
|---|---|---|
| `double` 接 float 写入 | `d = 5.31535e-315`（**垃圾**）| **无** |
| `float` 接 float 写入（对照）| `f = 2.5`（正确）| 无 |
| `float` 接 double 写入 | 值错；**相邻守护变量 `12345` 完好** | **无** |

**两点值得记：**

1. **不加 `-Quiet` 也完全没有警告。** 原因是库的形参是 `void *` ——
   类型信息在那一刻就被抹掉了，编译器**无从**警告。所以不能指望"看警告"
   来防这类陷阱，**必须读实现源码或写值断言**。

2. ⚠️ **更正 P74 里"越界写 4 字节栈内存"这个说法。** 本实测里相邻守护
   变量**未被破坏**（仍是 12345）。所以这类错误的后果是
   **值错误（静默的）**，而不是必然的栈越界。P74 原文把后果说重了。
   （P74 的原始观测——`modff(3.75f,&f)` 得到 int=0.00——仍然成立，
   那确实是值错误；但"越界写"这个机制描述未被本实测支持。）

   注意本条只测了**一种栈布局**，不能断言所有情形都不越界。
   保守的做法仍然是：**宽度不匹配就是错的，别赌它不越界**。

**修正后的准确表述：** 这类陷阱是**静默的值错误**，编译器不可能警告
（形参是 `void *`），所以唯一的防线是读源码 + 写值合理性断言。
