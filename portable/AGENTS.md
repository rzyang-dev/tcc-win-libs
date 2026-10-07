# 给 Agent 的使用说明

本目录是一个**自包含的 C / Lua 开发环境**。拷贝到任何位置都能用，
不需要安装任何东西，不需要联网。

---

## 第一件事：用封装好的入口，不要直接调 tcc.exe

```cmd
scripts\tcc.cmd <源文件.c> [选项]
```

直接调 `toolchain\tcc\tcc.exe` 会缺少重新生成的导入库路径
（`-L lib\tccdefs`），导致 `GlobalMemoryStatusEx` 这类**头里有声明、
但旧导入库里没有导出**的 API 无法链接。

> **注意区分两种不同的失败**（实测，方法见
> [`docs/research/TCC-BASELINE.md`](docs/research/TCC-BASELINE.md)）：
>
> - `GlobalMemoryStatusEx` —— 头里有声明，**加 `-L lib\tccdefs` 即可解决**
> - `GetTickCount64` / `InitializeCriticalSectionEx` —— Vista API，
>   2009 年的头文件集里**没有声明**，**加 `-L` 也不管用**，
>   需要 `tccwin.h` 补声明
>
> 此前 AGENTS.md 把这两个并列为"无法链接"，掩盖了机制差异。

| 选项 | 作用 |
|---|---|
| `-o <路径>` | 指定输出 |
| `-With a,b,c` | 自动加 include 路径、实现宏、依赖源文件 |
| `-Run` | 编译后立即运行 |
| `-32` | 编译 32 位 |
| `-List` | 列出可用库 |
| `-ShowFlags` | 只打印实际会用的编译参数（排查用） |
| `-- <参数>` | 之后的参数原样传给 TCC |

```cmd
scripts\tcc.cmd -List
scripts\tcc.cmd app.c -With cjson,tinydir,miniz -Run
scripts\tcc.cmd app.c -ShowFlags -With miniz
```

---

## 写作规范（违反会得到诡异错误）

### 1. `tccwin.h` 必须第一个 include

```c
#include "tccwin.h"     /* 必须第一行 */
#include <stdio.h>
```

它靠抢在 `<windows.h>` 之前定义 `NOWINBASEINTERLOCK` 来工作。
顺序错了不一定报错，可能只是行为不对。

### 2. 用到 float 数学函数就加 `tccmath.h` —— **32 位下必须加**

```c
#include "tccwin.h"
#include "tccmath.h"
```

按位宽分开说（实测，见 [`docs/PITFALLS.md`](docs/PITFALLS.md) P73）：

| 位宽 | 不可用的 float 函数 |
|---|---|
| **64 位** | 只有 4 个：`fabsf` / `truncf` / `cbrt` / `nearbyintf` |
| **32 位** | **22 个**，含 `sqrtf` `sinf` `cosf` `tanf` `powf` `floorf` `ceilf` `fmodf` `atan2f` `logf` `log10f` `expf` `asinf` `acosf` `atanf` `sinhf` `coshf` `tanhf` `copysignf` `truncf` |

所以"TCC 的数学支持其实不差"这句话**只在 64 位成立**。

> **本条曾被写错两次**：
> 1. 最早把这 16 个函数列成"都不可用"，实测 64 位下其中 12 个可用；
> 2. 改成"只有 4 个不可用"后，**那个结论只在 64 位对** ——
>    32 位实测有 22 个不可用（根因是 `tccmath.h` 的守卫只判了 64 位，
>    现已修正为 `__x86_64 || _WIN64 || __i386__ || _X86_`）。
>
> **教训：只在一种配置下测过，不能写成"可用/不可用"的结论。**

加了不会有副作用（宏只在函数真的缺失时才生效），
所以**需要 float 数学时一律加 `tccmath.h` 是最省事的做法**。

**`modff` 例外**：它是真函数不是宏，实现在 `helper/math_compat.c`，
需要 `-With math`。**不要自己写 `modff` 宏** ——
把 `float *` 当 `double *` 传给 `modf` 会**越界写 4 字节栈内存**（见 P74）。

### 3. 不要加 `-lm`

Windows 上数学函数在 CRT 里，加了报 `library 'm' not found`。

### 4. 生成源码文件必须用显式 UTF-8

```powershell
# 正确
[System.IO.File]::WriteAllText($path, $text, (New-Object System.Text.UTF8Encoding($false)))

# 错误：写 UTF-16，源码彻底损坏
Set-Content $path $text
... > $path
```

如果 C 测试文件里出现中文并伴随诡异行为（例如 printf 输出未初始化内存、
每次运行值不同），**先把文件改成纯 ASCII** 再排查。

### 5. 测试断言不要在同一表达式里二次调用 `GetLastError()`

```c
/* 错误：第二次调用覆盖了第一次的语义 */
if (GetTokenInformation(...) && GetLastError() == ERROR_INSUFFICIENT_BUFFER)

/* 正确 */
GetTokenInformation(...);
DWORD err = GetLastError();
if (err == ERROR_INSUFFICIENT_BUFFER) { ... }
```

---

## 可用库速查

用 `-With` 名字调用。完整清单见 [`docs/LIBRARIES.md`](docs/LIBRARIES.md)。

```
cjson           JSON 解析/生成
stb_ds          动态数组 + 哈希表
stb_sprintf     可靠 snprintf
jsmn            极简 JSON 分词器
utf8            Unicode 码点操作
tinydir         目录遍历（文件大小用 f._size）
dirent          POSIX opendir/readdir（补 TCC 声明了但链不上的符号）
xxhash          XXH32/XXH64（已自动关 XXH3）
sha256          SHA-256（头文件名 lonesha256.h）
log.c           带级别/颜色的日志
argparse        命令行解析
miniz           DEFLATE/ZIP/CRC32
inih            .ini 配置解析
stb_image       图片解码（已自动关 SIMD）
stb_image_write 图片编码
stb_rect_pack   矩形打包
linmath         向量/矩阵/四元数（配 tccmath.h）
tinyexpr        数学表达式求值
socket          TCP/UDP + DNS（官方 winsock2.h/ws2tcpip.h）
nuklear         GUI 控件库（即时模式，含内置字体）
regex           正则表达式（无分支/捕获组）
sqlite          SQLite 3.53.4 嵌入式数据库（**仅 64 位**，32 位会崩，见 P75）
stb_truetype    字体渲染（含在 nuklear 内）
readline        终端行编辑器（自研，含历史与行编辑）
utf8io          UTF-8 输出常量（GBK 控制台适配，纯头文件）
unistd          补 useconds_t 让 <unistd.h> 能解析，并实现 usleep
time            实现 gettimeofday / mingw_gettimeofday / _ftime64
math            modff 正确实现（宏版本会越界写）+ cbrt/cbrtf
```

---

## 构建 Lua 相关产物

```cmd
REM 静态 Lua 解释器（单文件零依赖；官方 lua55.exe 需要 lua55.dll）
scripts\build-lua.cmd -Interpreter

REM 把 Lua 脚本打包成单文件 exe
scripts\build-lua.cmd -Script app.lua -Out app.exe
scripts\build-lua.cmd -Script app.lua -Out app.exe -Bytecode -With cjson,miniz
```

打包后的 exe 里，Lua 侧可通过全局表 `C` 访问 C 库：

```lua
C.json_encode(t)          -- 需 -With cjson
C.json_decode(s)
C.gzip_compress(s)        -- 需 -With miniz
C.gzip_decompress(s)
C.crc32(s)
```

**什么时候加 `-Bytecode`**：脚本超过 ~1000 行时加。
实测 10000 行脚本：源码嵌入启动 222 ms，字节码嵌入 49 ms。
500 行以下无差别（进程启动本身 ~19 ms）。

**注意**：Lua 的 `arg[1]` 是第一个参数，不是 exe 路径。
打包器已按此约定填充。

---

## 验证环境是否完好

```cmd
scripts\tcc.cmd examples\01-hello.c -Run
scripts\tcc.cmd examples\02-winapi.c -Run
scripts\tcc.cmd examples\03-multilib.c -With tinydir,sha256,xxhash,cjson,stb_ds -Run -- .
scripts\build-lua.cmd -Interpreter
bin\slua.exe -e "print('lua ok', 2^10, 0xF0|0x0F)"
```

四条都通过，环境就是完好的。

---

## 遇到问题的排查顺序

1. 读 [`docs/PITFALLS.md`](docs/PITFALLS.md) —— 80 条已知坑，按"会不会静默出错"排序
   （多处结论的依据在 [`docs/research/`](docs/research/README.md)，那里有索引）
2. `scripts\tcc.cmd -ShowFlags <你的参数>` 看实际编译命令
3. 编译通过但结果不对 → 优先怀疑**缺原型**（坑 P2）而不是逻辑错误
4. 链接失败 → 通常是缺 `-L lib\tccdefs`（用了裸 tcc.exe）或缺 `tccmath.h`

---

## 做诊断测试时的铁律（本项目三次栽在这里）

**要验证某样东西是否必要，必须先把它拿掉再测。**
带着它测出来的 "OK" 只证明它在工作，不证明没有它也行。
详见 [`docs/PITFALLS.md`](docs/PITFALLS.md) P63。

两种测法用途不同，**不要混**：

| 测法 | 用途 | 写法 |
|---|---|---|
| **A** | 判"TCC 自己有没有" | 源文件只 `#include <windows.h>`，**不含 `tccwin.h`**；直接用 `toolchain\tcc\tcc.exe -L lib\tccdefs`（32 位用 `i386-win32-tcc.exe -L lib\tccdefs32`） |
| **B** | 判"用本环境时能不能用" | `scripts\tcc.cmd t.c -Quiet [-32]` |

**两者结果不同是正常的**——差异恰好说明垫片在做实事。

三条具体禁令：

- 测 TCC 能力**不加任何 `-I`**（加了就测到自己的垫片）
- 链接测试**必带 `-L`**（否则把"缺导入库"误判成"编译器不支持"）
- 清单一律 `Test-Path` / `Get-ChildItem` 实测，**不读文档里的清单**
  （`winapi-full/header_doc.txt` 已证明会夸大）

另外：判定单个函数"自带"**不等于**"可删" ——
块内可能另有实现依赖它（如 i386 的 `InterlockedExchangeAdd`
是 `InterlockedIncrement64`/`Decrement64` 的基础）。

---

## 能力边界

| 能做 | 不能做 |
|---|---|
| C89/C99 子集、无 C++ | C++ 任何版本 |
| Win32 / 现代 Windows API | SIMD intrinsics（无 `emmintrin.h`） |
| 单文件 exe，零依赖 | 需要第三方运行库的场景 |
| Lua 5.5 + C 扩展绑定 | 可视化资源编辑器（`.rc` 要手写） |
| 32 位与 64 位交叉编译 | 调试器（用 `printf` + `-g` 配合外部工具） |
| 编译速度极快（~1 秒完成完整构建） | 增量构建系统（无 make；用脚本） |

编译速度是本环境的最大优势：33 个 Lua 源文件 + 9 个库源文件合成一个 exe
只需 **0.86 秒**。改一行重编不会打断思路。
