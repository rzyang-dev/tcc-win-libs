# `tool/` 完整调查报告

> **背景**：本项目的 `portable/` 环境是在**没有先弄清 `tool/` 里有什么**的前提下
> 逐步搭起来的。结果是大量精力花在"重新发明轮子"上 —— 手写 `tccwin.h`
> 里的声明、手写 `winsock2.h` 垫片，而 `tool/` 里其实躺着完整的官方头文件集。
>
> 这份文档记录**基于实测**的完整事实，作为后续清理与重构的依据。
> 每条结论都标注了验证方式。

---

## 1. `tool/` 里到底有什么

```
tool/
├── lua/
│   ├── lua-5.5.0_Sources/          82 个 .c + 5 个 include/  ← Lua 源码
│   ├── lua-5.5.0_Win32_bin/        4 项（lua55/luac55/wlua55 + dll）
│   └── lua-5.5.0_Win64_bin/        4 项
└── tcc/
    ├── tcc-0.9.27-win32-bin/       32 位 TCC 发行版
    │   ├── include/                40 项（TCC 自带头）
    │   ├── lib/                    6 项（libtcc1-32.a / libtcc1-64.a / *.def）
    │   ├── examples/ doc/
    │   └── tcc.exe / i386-win32-tcc.exe / libtcc.dll
    ├── tcc-0.9.27-win64-bin/       64 位 TCC 发行版（结构同上）
    ├── winapi-full-for-0.9.27/     ★ 完整官方头文件集
    │   ├── include/
    │   │   ├── winapi/             755 个 —— Win32 API 主体
    │   │   ├── ddk/                138 个 —— 驱动开发套件
    │   │   ├── GL/                 3 个（gl.h glaux.h glu.h）
    │   │   ├── sdks/               2 个（_mingw_ddk.h _mingw_directx.h）
    │   │   ├── sys/                2 个（cdefs.h param.h）
    │   │   └── 顶层约 30 个         complex.h crtdbg.h dlfcn.h getopt.h
    │   │                            intrin.h libgen.h search.h strings.h
    │   │                            unistd.h utime.h ieeefp.h 等
    │   ├── orig/                   37 项 —— TCC 已含头文件的**上游未修改版**
    │   ├── header_doc.txt          10.6 KB
    │   └── readme.txt
    └── tcc-busybox-for-win32/      busybox + gmake 源码（参考资料）
```

## 2. `readme.txt` 的官方说明（**这份文件应该一开始就读**）

原文：

> This is the full set of winapi headers from the mingw-64 project
> from 2009/12/20 (gcc version 4.5.0 experimental).
>
> **A part of these files is already included (with minor modifications)
> with the tcc-0.9.27 release. Original versions of these files can
> be found in the 'orig' folder.**
>
> **The other files (from the 'include' folder) may be copied to the tcc
> installation or used with -I or -isystem.**
>
> **Compatibility with tcc has not been checked.** Do not expect everything
> to work out of the box under all circumstances.

### 由此得出的三点关键认识

1. **`portable/toolchain/tcc/include/` 里那 18 个 winapi 头不是"被裁减"**，
   而是 **TCC 官方发行版自带的、带 TCC 修改的版本**（其中就包含
   `windows.h:70` 把 `#include <winnls.h>` 注释掉这类改动）。
2. **`winapi-full/include/` 是同一批头文件的上游未修改版 + 大量 TCC 没带的头**。
   `orig/` 则专存 TCC 已含头文件的上游版本，可用于对比 TCC 改了什么。
3. **官方明确说"兼容性未验证，别指望开箱即用"** —— 所以正确用法是
   **按需取用 + 逐个验证**，而不是整批替换（整批替换会丢掉 TCC 的修改，
   并把 2009 年的上游版本换进核心头，风险高）。

### 一个必须纠正的误读

`header_doc.txt` 里那份"File List"**夸大了实际内容**。它列了
`termios.h`、`math.h`、`tgmath.h`、`mm_malloc.h`、`sys/ioctl.h`、`sys/termios.h`
等，但**这些文件在 `winapi-full/include/` 里不存在**（实测逐个 `Test-Path`）。

那份清单混合了"曾经发布过的名字"与正文里的引用，**不能当作实际文件清单**。
必须用 `Get-ChildItem` 实测。

（我一度据此得出"linenoise 本来就能编译"的结论，**那是错的** ——
`termios.h` 确实不存在。教训：**清单要实测，不要读文档**。）

## 3. TCC 0.9.27 自带头文件的实测能力

**方法**：`tcc.exe -c -o out.o x.c`，不加任何 `-I`，只用 TCC 自带 include。
链接测试另加 `-L lib\tccdefs`（`portable` 的导入库目录）。

### 已经提供的（**不需要垫片**）

| 项目 | 编译 | 链接 |
|---|---|---|
| `InterlockedIncrement` / `Decrement` | ✅ | ✅ |
| `InterlockedExchange` | ✅ | ✅ |
| `InterlockedCompareExchange` | ✅ | ✅ |
| `InterlockedOr` / `InterlockedAnd` | ✅ | ✅ |
| `InterlockedExchangePointer` | ✅ | ✅ |
| `GlobalMemoryStatusEx` | ✅ | ✅（需 `-L lib\tccdefs`） |
| `strtoll` / `strtof` | ✅ | ✅ |
| Windows API 主体（文件/进程/内存/同步/控制台/注册表/GUI/GDI） | ✅ | ✅ |

### 编译能过但**链接失败**（垫片确实必要）

| 项目 | 失败信息 |
|---|---|
| `InterlockedExchangeAdd` | `undefined symbol '_InterlockedExchangeAdd'` |
| `InterlockedExchangeAdd64` | `undefined symbol '_InterlockedExchangeAdd64'` |

原因：TCC 不为这两个生成内建实现，CRT 里也没有。**这两项垫片是对的。**

### 真的缺声明（垫片必要，且 winapi-full 里也没有）

这些都是 **Vista / Windows 7 时代才引入的 API**，而此头文件集是 **2009 年**的，
所以两头都没有：

| 项目 | 备注 |
|---|---|
| `CONDITION_VARIABLE` + `*ConditionVariable*` | Vista 引入 |
| `INIT_ONCE` + `InitOnceExecuteOnce` | Vista 引入 |
| `SYNCHRONIZATION_BARRIER` | Win8 引入 |
| `GetTickCount64` | Vista 引入 |
| `InitializeCriticalSectionEx` | Vista 引入 |
| `CreateThreadpoolWork` 等线程池 | Vista 引入 |
| `FILE_INFO_BY_HANDLE_CLASS` / `FILE_*_INFO` | Vista 引入 |
| `ConvertSidToStringSidA`（在真 `sddl.h` 里有，可改用真头） | — |
| `RegDeleteTreeA` | XP 后期 |
| `strnlen` | 非 C89，msvcrt 里有 |

**结论：`tccwin.h` 里补 Vista/7 API 的那部分是有价值的，不是白做。**
但补"TCC 已提供"的那部分是白做（见上表第一栏）。

### 真正只能垫片的一件事：float 数学

TCC 的 `math.h` **声明了** `sqrtf`（L183）和 `fabsf`（L188）等，
但**编译不过**：

- `math.h:247` 附近：`#ifndef __x86_64` 守卫导致声明被跳过
- `math.h:217` 附近：`__asm` 内联用了 TCC 不支持的约束 `'t'`

实测错误：`unknown constraint 't'`、`implicit declaration`。

**所以 `tccmath.h` 是必要且正确的** —— 这是 TCC 的真实缺陷，
不是"缺头文件"造成的。**不要删。**

### CRT 头只能来自 TCC

`math.h` / `stdio.h` / `stdlib.h` / `string.h` / `time.h` / `wchar.h` /
`float.h` / `fenv.h` / `stdint.h` **在 `winapi-full` 里不存在**，
只有 TCC 自带那一份。所以这些不能"改用真头"。

## 4. 四类问题的正确处置

| 类别 | 例子 | 处置 |
|---|---|---|
| **A. TCC 已提供，垫片是白做** | `InterlockedIncrement` 等 7 个、`GlobalMemoryStatusEx`、`strtoll`/`strtof` | **删垫片**（删前每个都要编译+链接实测） |
| **B. TCC 缺、但真头文件有** | `MultiByteToWideChar`、`WideCharToMultiByte`、`GetACP`、`GetCPInfo`/`CPINFO`、`CompareStringA`、`GetStringTypeA/W`、`ConvertSidToStringSidA`、`winsock2.h` 全套 | **取真头文件**，删垫片（已部分完成） |
| **C. TCC 缺、真头也没有（时代差）** | `CONDITION_VARIABLE`、`INIT_ONCE`、`GetTickCount64`、`InitializeCriticalSectionEx`、线程池、`FILE_*_INFO`、`strnlen` | **保留垫片** —— 这是 `tccwin.h` 的真正价值 |
| **D. TCC 自身缺陷（非头文件问题）** | float 数学（约束 `'t'`、`#ifndef __x86_64`） | **保留 `tccmath.h`** |

### 尚未处理的

- **`InterlockedExchangeAdd`/`ExchangeAdd64`**：垫片必要（类别 C），已归位。
- **`winapi-full` 里 `windows.h` 引用的 28 个头**（`commctrl.h`、`shlwapi.h`、
  `wincrypt.h`、`winspool.h`、`mmsystem.h`、`shellapi.h`、`commdlg.h`、
  `winsvc.h`、`winscard.h`、`rpc.h`、`winnetwk.h`、`stralign.h` 等）
  **实测 28/28 都可取到**，尚未纳入。纳入需逐个验证能否与 TCC 头的
  `windows.h` 共存。
- **官方 `winsock2.h` 试用结果（已验证可行，未提交）**：
  在临时目录实测通过 —— 结构体尺寸与 MSDN 完全一致
  （`WSADATA=408`、`sockaddr_in6=28`、`sockaddr_storage=128`、`fd_set=520`），
  `_WINSOCK2API_` 定义正确、`GetAdaptersAddresses` 可调、真实回环收发正常。
  但直接替换会在 `typedef INT_PTR SOCKET`（官方）与
  `typedef UINT_PTR SOCKET`（垫片）之间冲突。
  **正确路径是：纳入官方 winsock 头 → 删掉自写 `winsock2.h` 垫片 →
  让 `tccwin.h` 不要重复定义 winsock 相关内容。**

## 5. 方法论教训（比技术结论更重要）

本项目的错误几乎都来自**探测方法不可靠**，而不是判断力：

| 错误做法 | 后果 |
|---|---|
| 探测时用 `tcc.exe -I <portable>\include` | 把自己的 `tccwin.h` 带进来，测不出 TCC 的真实能力 |
| 探测时漏掉 `-L lib\tccdefs` | 把"缺导入库"误判成"TCC 不支持"，两次得出相反结论 |
| 读 `header_doc.txt` 的清单当文件列表 | 得出 `termios.h` 存在等错误结论 |
| 只测编译、不测链接 | 漏掉 `InterlockedExchangeAdd` 这类"编译过但链接失败"的情况 |

**确立的规矩**：

1. **测 TCC 能力必须用 `tcc.exe` 且不加任何 `-I`**，否则测的是自己的头。
2. **链接测试必须带 `-L lib\tccdefs`**（或直接用 `scripts\tcc.cmd`）。
3. **清单一律实测**（`Test-Path` / `Get-ChildItem`），不读文档。
4. **"能用/不能用"必须同时过编译与链接**，并至少真实调用一次。
5. 任何"缺声明"的结论，**先查 `winapi-full/include/`**，再考虑写垫片。

## 6. 下一步建议（待确认）

按风险从低到高：

1. **逐个核实并删除类别 A 的垫片**（7 个 Interlocked + `GlobalMemoryStatusEx`
   + `strtof`/`strtoll`），每删一个都跑全量自检。
2. **完成官方 `winsock2.h` 替换**（已验证可行）：纳入
   `winsock2.h`/`ws2tcpip.h`/`ws2ipdef.h`/`mswsock.h`/`wsipv6ok.h`/`qos.h`，
   删除自写垫片，修掉 `SOCKET` 冲突。
3. **按需纳入 `windows.h` 引用的 28 个头**，逐个验证共存性。
4. **重写 `tccwin.h`**：只保留类别 C 的垫片（Vista/7 API），
   在文件头写明每一项**为什么必要、验证方式、以及 winapi-full 里没有的理由**。

> 第 4 步是最终目标：让 `tccwin.h` 从"一大坨不明来历的补丁"变成
> "每一项都有据可查的最小必要集"。
