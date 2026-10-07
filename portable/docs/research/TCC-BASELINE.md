# TCC 0.9.27 真实能力基线（权威测量）

> 本文件是**改正文档错误判断的依据**。
>
> **测量方法（必须遵守，否则结论无效）**：
> - 探测 TCC 自身能力：`tcc.exe`（或 `i386-win32-tcc.exe`）**不加任何 `-I`**
>   —— 加了 `-I <portable>\include` 就会把自己的垫片带进来，测的是垫片不是 TCC
> - 链接测试：**必须带 `-L lib\tccdefs`**（32 位用 `lib\tccdefs32`）
>   —— 漏了会把"缺导入库"误判成"编译器不支持"
> - 结论必须**同时过编译与链接**
> - 清单一律 `Test-Path` 实测，**不读文档里的清单**
>
> 测量日期：本轮。全部命令见 git 提交信息。

---

## 1. Interlocked 全系列 × 32/64 位

| 函数 | 64 位 | 32 位 |
|---|---|---|
| `InterlockedIncrement` | 可用 | **可用** |
| `InterlockedDecrement` | 可用 | **可用** |
| `InterlockedExchange` | 可用 | **可用** |
| `InterlockedExchangeAdd` | **失败** | **可用** |
| `InterlockedCompareExchange` | 可用 | **可用** |
| `InterlockedOr` | 可用 | **失败** |
| `InterlockedAnd` | 可用 | **失败** |
| `InterlockedXor` | 可用 | **失败** |
| `InterlockedIncrement64` | 可用 | **可用** |
| `InterlockedExchangeAdd64` | **失败** | **可用** |
| `InterlockedCompareExchange64` | 可用 | **可用** |
| `InterlockedExchangePointer` | 可用 | **可用** |
| `InterlockedCompareExchangePointer` | 可用 | **可用** |

**真实失败集（共 5 个格）**：
- 64 位缺实现：`InterlockedExchangeAdd`、`InterlockedExchangeAdd64`
  → `undefined symbol '_InterlockedExchangeAdd'`
- 32 位缺实现：`InterlockedOr`、`InterlockedAnd`、`InterlockedXor`
  → `undefined symbol 'InterlockedOr'`（注意没有前导下划线，与 64 位不同）

**被推翻的文档说法**：
- README 说"**32 位下 14 个全废**" → **错**。实测 12 个里 9 个可用。
- PITFALLS P4 表格说 `Increment/Decrement/Exchange/CompareExchange`
  "32 位**全部缺失**" → **错**，实测都可用。

## 2. float 数学函数（30 个逐一测）

**只有 4 个真缺**：

| 函数 | 失败原因 |
|---|---|
| `fabsf` | `unknown constraint 't'` —— TCC 不支持的 x86 约束 |
| `truncf` | `undefined symbol 'truncf'` |
| `cbrt`（float 版） | `undefined symbol 'cbrt'` |
| `nearbyintf` | `undefined symbol 'nearbyintf'` |

**26 个可用**（编译+链接都过）：
`sqrtf` `sinf` `cosf` `tanf` `expf` `logf` `log10f` `powf` `floorf` `ceilf`
`fmodf` `atan2f` `hypotf` `sinhf` `coshf` `tanhf` `asinf` `acosf` `atanf`
`roundf` `copysignf` `fminf` `fmaxf` `ldexpf` `frexpf` `modff`

**被推翻的文档说法**：
- PITFALLS P5 说"**全部 float 数学函数不可用（16 个）**" → **错**。
- AGENTS.md 说 `ceilf` `fmodf` `atan2f` `truncf` `hypotf` `log10f`
  "在 TCC 上**都不可用**" → **错**，其中只有 `truncf` 真的不可用。
- README 说 `math.h` 的 float 包装器被 `#ifndef __x86_64` 挡住导致
  "16 个函数不可用" → 部分错：只有 `fabsf` 是这个原因，
  另 3 个是**缺实现**（undefined symbol），机制不同。

**`tccmath.h` 的必要性**：仍然必要，但**范围要收窄到 4 个**
（`fabsf`/`truncf`/`cbrt`/`nearbyintf`），而不是原来的 16 个。

## 3. 导入库与 CRT

| 项目 | 结论 | 验证 |
|---|---|---|
| TCC 自带 `kernel32.def` | **767 个导出** | 与 README 说的一致，**该说法正确** |
| `lib\tccdefs\kernel32.def` | **1636 个导出** | 重新生成版，正确 |
| `GlobalMemoryStatusEx` | **带 `-L` 可用**；不带 `-L` 失败 | README 说"无法链接"**不准确**，正确说法是"需要 `-L lib\tccdefs`" |
| `GetTickCount64` | 带 `-L` **也失败** | 原因是**头文件里没声明**（Vista API，2009 年头文件没有），不是导入库问题 |
| `TryEnterCriticalSection` | **可用**（自带） | 此前未测 |
| `strtof` / `strtoll` / `strtoull` | **缺** | P7 说 `strtof` 缺，**正确**；`strtoll`/`strtoull` 也缺 |
| `strnlen` / `strerror_s` | **缺** | — |
| `_aligned_malloc` | **缺** | — |
| `_controlfp` | **缺** | — |
| `isgreater` | **缺**（C99 比较宏整体缺） | — |
| `_set_se_translator` | **缺** | — |
| `setlocale` | **可用** | — |

## 4. 头文件现状（实测，非文档）

| 项 | TCC 自带 | winapi-full | 说明 |
|---|---|---|---|
| `emmintrin.h` / `xmmintrin.h` / `mmintrin.h` | 无 | **无** | P8 说 SIMD intrinsics 不存在，**正确** |
| `intrin.h` | 无 | **有** | 但那是 MSVC 内建的分发头，不是 SIMD intrinsics |
| `dirent.h` | **有** | 无 | **P/LIBRARIES 说"Windows 上不存在"是错的** |
| `libgen.h` | 无 | **有** | 同上，**"不存在"是错的** |
| `winsock2.h` / `winsock.h` / `ws2tcpip.h` | 无 | **有** | ROADMAP 说 TCC 全无，**正确** |
| `mstcpip.h` | 无 | **有** | — |
| `ws2def.h` | 无 | **无** | 两边都没有（mingw-w64 2009 版把它并入 `winsock2.h`） |
| `termios.h` / `unistd.h` / `sys/ioctl.h` | 无 | **无** | ROADMAP 说 POSIX 层缺失，**正确**（`header_doc.txt` 谎报） |

## 5. 需要改的文档清单

| 文件 | 位置 | 错误 | 正确说法 |
|---|---|---|---|
| `docs/PITFALLS.md` | P4 表格 | "Increment/Decrement/Exchange/CompareExchange 32 位全部缺失" | 实测 12 个里 32 位 9 个可用；只有 `Or`/`And`/`Xor` 在 32 位缺 |
| `docs/PITFALLS.md` | P5 标题与正文 | "全部 float 数学函数不可用（16 个）" | 只有 4 个缺，26 个可用；列明是哪 4 个 |
| `docs/PITFALLS.md` | P5 | 把 `fabsf` 的原因（约束 `'t'`）套到全部 | 只有 `fabsf` 是约束问题，另 3 个是 `undefined symbol` |
| `docs/PITFALLS.md` | P3 | 暗示 `GetTickCount64`/`GlobalMemoryStatusEx` 同类 | 二者机制不同：前者缺声明，后者只需 `-L` |
| `README.md` | L96 | "32 位下 14 个全废" | 改成实测矩阵 |
| `README.md` | L97 | "16 个函数不可用" | 改成 4 个 |
| `README.md` | L94 | `GlobalMemoryStatusEx` "无法链接" | 改成"需要 `-L lib\tccdefs`" |
| `AGENTS.md` | L56 | 列 6 个 float 函数"都不可用" | 只有 `truncf` 不可用；`fabsf` 需 `tccmath.h` |
| `ROADMAP.md` | A0 结论 | "winapi 头文件确实被裁减，这是缺声明的主因" | **错。** 官方 readme 说明：TCC 发行版"已包含其中一部分（带修改）"，不是裁减。正确说法是"TCC 自带头集不含 winsock/winnls 等，这些可从 winapi-full 按需取用" |
| `docs/LIBRARIES.md` | L345 | `dirent.h`/`libgen.h` "Windows 上不存在" | `dirent.h` TCC 自带；`libgen.h` 在 winapi-full 里 |
| `docs/PITFALLS.md` | P61 | 同上，需与 ROADMAP 口径统一 | — |
| `TOOL-AUDIT.md` | 全文 | 需补上本文件的权威矩阵并交叉引用 | — |

## 6. 下一步（第 2 步：改代码）

按 `TOOL-AUDIT.md` 的四类划分：

- **类别 A（TCC 已提供，垫片白做）**：删掉 `tccwin.h` 里覆盖
  `InterlockedIncrement/Decrement/Exchange/CompareExchange/...` 的垫片
  （但**保留** 5 个真实缺口格的实现）
- **类别 B（真头文件有）**：整批纳入 `winapi-full`，删自写 `winsock2.h` 垫片
- **类别 C（Vista/7 API，真头也没有）**：保留垫片
- **类别 D（TCC 缺陷）**：`tccmath.h` 收窄到 4 个函数
