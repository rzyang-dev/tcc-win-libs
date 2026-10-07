# Portable C / Lua Dev Environment (TCC 0.9.27 + Lua 5.5)

一个**自包含、可直接拷贝**的 Windows 开发环境：Tiny C Compiler + Lua 5.5 + 26 个已实测通过的
单文件 C 库。不需要装 Visual Studio、MinGW、Python 或任何运行库。

目标是让另一个 agent（或另一个工作区里的你）**拿到就能编译，编译就能跑**。

- 体积：7.8 MB / 271 文件
- 产物：单文件 exe，零外部依赖（不需要 `msvcrt` 之外的任何 DLL）
- 已验证：x64 与 x86 双架构

---

## 30 秒上手

```cmd
REM 1) 编译一个 C 程序
scripts\tcc.cmd hello.c -Run

REM 2) 用上库（自动处理 include 路径、实现宏、导入库）
scripts\tcc.cmd app.c -With cjson,tinydir,miniz -Run

REM 3) 看有哪些库
scripts\tcc.cmd -List

REM 4) 构建静态 Lua 解释器（官方 lua55.exe 需要 lua55.dll，这个是单文件）
scripts\build-lua.cmd -Interpreter

REM 5) 把 Lua 脚本打包成单文件 exe
scripts\build-lua.cmd -Script app.lua -Bytecode -With cjson,miniz
```

> **用 `.cmd` 而不是 `.ps1`。** PowerShell 默认执行策略会拒绝未签名脚本；
> `.cmd` 包装器用 `-ExecutionPolicy Bypass` 绕开，其他 agent 也会撞上这个限制。

### 先跑一次自检

```
scripts\verify.cmd
```

它会检查全部工具链文件、29 个头文件、导入库导出数，并**实际编译运行 16 个探针程序 + 5 个 examples**
（现代 API / JSON / 数据结构 / 哈希 / 压缩 / 目录 / TOML / 图片 / 矩阵 / 网络 socket / GUI / 正则 / SQLite / 字体 / 行编辑 / UTF-8 编码）
加一次静态 Lua 构建。全部通过会输出：

```
=== 结果: 64 通过, 0 失败 — 环境完好 ===
```

拷贝到新位置后跑一次，就能确认没有文件在传输中损坏。

---

## 目录结构

```
portable/
├── README.md                 本文件
├── AGENTS.md                 给 agent 的使用说明（先读这个）
├── docs/
│   ├── LIBRARIES.md          13 个库的清单、验证状态、用法片段
│   └── PITFALLS.md           工具链全部已知坑与修法（重要）
├── scripts/
│   ├── tcc.cmd / tcc.ps1             编译入口（封装所有坑）
│   └── build-lua.cmd / build-lua.ps1 构建静态 Lua / 打包脚本成 exe
├── include/
│   ├── tccwin.h              ★ Windows 兼容层，必须第一个 include
│   ├── tccmath.h             ★ float 数学函数修复，用到就 include
│   └── stb_ds.h  stb_sprintf.h  jsmn.h  utf8.h
│       xxhash.h  lonesha256.h   tinydir.h          单文件库
├── lib/
│   ├── tccdefs/              25 个重新生成的 .def 导入库（64 位，关键！）
│   ├── tccdefs32/            43 个从 SysWOW64 生成的 32 位导入库
│   │                         （`-32` 必须用这套，见 PITFALLS P60）
│   ├── sqlite/  regex/  readline/   嵌入式库与自研组件
│   ├── cjson/  miniz/  argparse/  inih/  tinyexpr/  log.c/  toml/
├── toolchain/
│   ├── tcc/                  TCC 0.9.27 x64+x86 头文件与库
│   ├── lua-bin/Win64 Win32   Lua 5.5 官方二进制（含 luac55）
│   └── lua-src/              Lua 5.5 全部源码（用于静态链接）
├── helper/                   luac2c.exe txt2c.exe（构建期工具）
├── examples/                 可直接编译运行的示例
└── bin/                      编译产物（可清空）
```

---

## 为什么需要这个环境

裸用 TCC 0.9.27 调 Windows API 会撞上四个**硬性阻塞**，本环境全部预先修好。
下表的数字都是**实测**的（方法：`tcc.exe` 不加任何 `-I`，链接带 `-L`，
编译+链接都过才算可用；完整矩阵见 [`docs/research/TCC-BASELINE.md`](docs/research/TCC-BASELINE.md)）：

| 问题 | 后果 | 本环境的修法 |
|---|---|---|
| 自带 `kernel32.def` 是 2006 年的，只有 **767** 个导出 | 部分现代 API 无法链接（**如 `GlobalMemoryStatusEx`**；加 `-L` 即可解决） | `lib/tccdefs/` 从真实 DLL 重新生成，**1636** 个导出 |
| 头文件缺 Vista/7 时代的类型与原型 | 编译期报未声明；**更糟的是静默出错** | `include/tccwin.h` 补齐（这批是真缺口） |
| `winbase.h` 把 `Interlocked*` 宏改写成编译器内建名 | **64 位缺 2 个**（`ExchangeAdd`/`ExchangeAdd64`）；**32 位缺 3 个**（`Or`/`And`/`Xor`） | `tccwin.h` 补齐这 5 格 |
| `math.h` 的 float 包装器有缺陷 | **只有 4 个不可用**：`fabsf` `truncf` `cbrt` `nearbyintf`（另 26 个可用） | `include/tccmath.h` 只覆盖这 4 个 |

**注意"缺导入库"与"缺声明"是两种不同的问题，修法也不同**：

- `GlobalMemoryStatusEx` —— 头里有声明，**只需 `-L lib\tccdefs`**
- `GetTickCount64` —— Vista API，2009 年的头文件集里**没有声明**，
  加 `-L` 也不管用，必须补声明

前两条不修的话，程序可能**编译通过但运行结果错误**——这是最危险的情形。
详情见 [`docs/PITFALLS.md`](docs/PITFALLS.md)。

> **本表曾被写错**：原文说"32 位下 14 个 `Interlocked` 全废"（实测 12 个里 9 个可用）、
> "16 个 float 函数不可用"（实测只有 4 个）。已按实测改正。
> 教训是**探测方法必须正确**，否则会把自己的垫片当成编译器基线来测。

---

## 关键约定

### 1. `tccwin.h` 必须第一个 include

```c
#include "tccwin.h"      /* 必须第一行；它内部会 include <windows.h> */
#include <stdio.h>
```

它靠"抢在 `<windows.h>` 之前定义 `NOWINBASEINTERLOCK`"来工作。
顺序错了会得到链接错误或错误的行为，而不是明确的报错。

### 2. 用到 float 数学函数就加 `tccmath.h`

```c
#include "tccwin.h"
#include "tccmath.h"     /* 修复 sqrtf/sinf/fabsf 等 16 个函数 */
```

### 3. 不要加 `-lm`

Windows 上数学函数在 CRT 里。加了会报 `library 'm' not found`
（LuaRocks 的 MinGW 路径就是栽在这一步）。

### 4. 写文件用显式编码

如果本环境里的脚本要生成 C 源码，**必须**用：

```powershell
[System.IO.File]::WriteAllText($path, $text, (New-Object System.Text.UTF8Encoding($false)))
```

不要用 `>` 重定向（PowerShell 5.1 写 UTF-16，会把源码彻底毁掉），
也不要用 `Set-Content`（默认可能是 ANSI，中文注释会变乱码并导致诡异的编译错误）。

---

## 已验证的库

13 个，全部实测编译 + 功能验证通过。详见 [`docs/LIBRARIES.md`](docs/LIBRARIES.md)。

| 类别 | 库 | 说明 |
|---|---|---|
| 数据 | `stb_ds` | 动态数组 + 哈希表（C 标准库完全没有） |
| 数据 | `cjson` | JSON 解析 / 生成 |
| 数据 | `jsmn` | 极简 JSON 分词器 |
| 序列化 | `inih` | .ini 配置解析 |
| 文本 | `utf8` | Unicode 码点操作与校验 |
| 文本 | `stb_sprintf` | 可靠 snprintf（支持千分位、二进制） |
| 计算 | `tinyexpr` | 数学表达式求值 |
| 哈希 | `xxhash` | XXH32 / XXH64 快速哈希 |
| 哈希 | `sha256` | SHA-256（与 Windows 内置实现逐位一致） |
| 压缩 | `miniz` | DEFLATE / ZIP / CRC32 |
| 文件 | `tinydir` | 目录遍历（已补 Windows 文件大小） |
| 日志 | `log.c` | 带级别 / 时间戳 / 颜色 |
| 参数 | `argparse` | 命令行解析 |
| **网络** | **自带 Winsock 垫片** | **TCP / UDP / DNS / select（实测真实收发）** |

---

## 示例

```cmd
scripts\tcc.cmd examples\01-hello.c -Run
scripts\tcc.cmd examples\02-winapi.c -Run
scripts\tcc.cmd examples\03-multilib.c -With tinydir,sha256,xxhash,cjson,stb_ds -Run -- .
scripts\build-lua.cmd -Script examples\app.lua -Bytecode -With cjson,miniz
bin\app.exe self
```

- `01-hello.c` — 验证工具链
- `02-winapi.c` — 现代 Windows API（内存状态、高精度时钟、原子操作、条件变量）
- `03-multilib.c` — 6 库协作：目录指纹清单 + JSON 输出
- `app.lua` — Lua 脚本打包成单文件 exe，含 C 库绑定

## 已知限制

- **只有 C，没有 C++**。TCC 不支持 C++。
- **没有 SIMD intrinsics**。`__m128i` / `emmintrin.h` 不存在。
  受影响：`XXH3`（已用 `-DXXH_NO_XXH3` 关闭）、`stb_image`（需 `-DSTBI_NO_SIMD`）。
- **原型上的 `__declspec(dllexport)` 无法解析**。cJSON 靠 `CJSON_HIDE_SYMBOLS` 绕开；
  自己写 DLL 时把 `dllexport` 加在**定义**上，不要加在原型上。
- **无 `-lm`、无 C99 的 `strtof`**（已提供垫片）。
- **GUI 需要手写 Win32**。没有可视化资源编辑器。
