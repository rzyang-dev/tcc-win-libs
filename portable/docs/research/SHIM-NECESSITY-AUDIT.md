# `tccwin.h` 垫片必要性审计

> **这份文档回答一个问题：`tccwin.h` 里每一块垫片，TCC 自己到底有没有？**

方法严格按项目铁律（见 `AGENTS.md`）：

1. 测 TCC 能力**不加任何项目 `-I`** —— 只用 TCC 自带的
   `toolchain\tcc\include` 与 `...\include\winapi`。
   加了 `-I portable\include` 就测到自己的垫片了。
2. 链接测试**必带 `-L lib\tccdefs`**（32 位用 `lib\tccdefs32`）——
   否则会把"缺导入库"误判成"编译器不支持"。
3. 结论**必须同时过编译与链接**，不是只看编译过。
4. 源文件只 `#include <windows.h>`（+ 该块明确要求的头），
   **不含 `tccwin.h`**。

每个块同时测 **64 位与 32 位**，因为本项目已多次栽在
"只在一种位宽下测过"上（见 PITFALLS P73）。

---

## 结果汇总

18 个块中：

| 判定 | 数量 | 含义 |
|---|---|---|
| **NEITHER**（两位宽都不行） | 14 | **垫片必要** —— TCC 确实缺 |
| BOTH-OK（两位宽都行） | 4 | 需要人工复核（见下） |

## 逐块结果

| 块 | 判定 | 实测报错 |
|---|---|---|
| `TCWIN_INTERLOCKED_HELPERS` | NEITHER | `undefined symbol '_InterlockedExchangeAdd64'` |
| `TCWIN_INIT_ONCE_DEFINED` | NEITHER | `',' expected (got "o")` |
| `TCWIN_CONDITION_VARIABLE_DEFINED` | NEITHER | `'CONDITION_VARIABLE' undeclared` |
| `TCWIN_SYNC_BARRIER_DEFINED` | NEITHER | `'SYNCHRONIZATION_BARRIER' undeclared` |
| `TCWIN_FILE_INFO_CLASS_DEFINED` | NEITHER | `'FILE_INFO_BY_HANDLE_CLASS' undeclared` |
| `TCWIN_FILE_INFO_STRUCTS_DEFINED` | NEITHER | `'FILE_BASIC_INFO' undeclared` |
| `TCWIN_THREADPOOL_DEFINED` | NEITHER | `',' expected (got "i")` |
| `TCWIN_TOKEN_INFO_DEFINED` | NEITHER | `'TOKEN_ELEVATION' undeclared` |
| `TCWIN_CODEPAGE_DEFINED` | NEITHER | `'CP_UTF8' undeclared` |
| `TCWIN_STRTOLL_DECLARED` | NEITHER | `undefined symbol 'strtoll'` |
| `TCWIN_CBRT_DECL_WIN` | NEITHER | `undefined symbol 'cbrt'` |
| `TCWIN_INET_PTON_DECLARED` | NEITHER | `undefined symbol 'inet_pton'` |
| `TCWIN_SOCKLEN_T` | NEITHER | `'socklen_t' undeclared` |
| `TCWIN_LOCALE_DEFINED` | NEITHER | `'_CREATE_LOCALE' undeclared` |
| `TCWIN_PROC_TOPOLOGY_DEFINED` | BOTH-OK * | 见下（垫片其实必要） |
| `TCWIN_ALIGNED_MALLOC_DECLARED` | BOTH-OK * | 见下（保留） |
| `TCWIN_STRERROR_S_DECLARED` | BOTH-OK * | 见下（保留） |
| `TCWIN_FLOAT_CTRL_DECLARED` | BOTH-OK * | 见下（保留） |

## 那 4 个 BOTH-OK 的复核（**不要照着删**）

这 4 个的探测"通过"了，但逐条复核后**都不能直接删** ——
这是一个重要的方法论提醒：**探测器的通过不等于被探测项冗余**。

### `TCWIN_PROC_TOPOLOGY_DEFINED` —— 我的探测器瞄错了对象

探测器用了 `SYSTEM_LOGICAL_PROCESSOR_INFORMATION` 与
`GetLogicalProcessorInformation`，这两个 TCC **确实自带**。

但那一块垫片提供的**不是**这个类型，而是**处理器组 API**：

```c
TCWIN_PROCESSOR_GROUP_INFO    /* BYTE MaximumProcessorCount; ... KAFFINITY ActiveProcessorMask */
TCWIN_GROUP_RELATIONSHIP      /* WORD MaximumGroupCount; ... GroupInfo[1] */
TCWIN_PROCESSOR_RELATIONSHIP  /* BYTE Flags; BYTE EfficiencyClass; ... */
ALL_PROCESSOR_GROUPS        0xFFFF
```

这些是 Windows 7 引入的，TCC 的 2009 年头文件集里**没有**。
**结论：垫片必要。**

### `TCWIN_ALIGNED_MALLOC_DECLARED` / `TCWIN_STRERROR_S_DECLARED` / `TCWIN_FLOAT_CTRL_DECLARED`

这三个的探测"通过"了，但复核发现两点：

1. **`_controlfp` / `_statusfp` / `_RC_*` 在 TCC 的 include 树里搜不到定义**
   （`Select-String` 全树无命中）。它们能编译是**经由 `<windows.h>`
   传递引入**的 —— 也就是说，可用性依赖用户是否包含了 `windows.h`。
   而 `tccwin.h` 的用途之一就是让**不包含 `windows.h`** 的代码也能用。
2. 这三个块都是**幂等声明**（`#ifndef` 守卫 + 只声明不定义）。
   重复声明同一个原型不会报错，所以"删掉能编过"并不等于"删掉没损失"。

**结论：保留。** 代价为零（重复声明无害），收益是让不包含
`windows.h` 的代码也能用。

> **方法论教训**：判定一侧冗余时，探测器必须覆盖**那块垫片真正提供的东西**，
> 而不是一个相关但不同的 API。我第一版探测器就犯了这个错 ——
> 用 `SYSTEM_LOGICAL_PROCESSOR_INFORMATION` 去测一块提供处理器组 API 的垫片。

## 未验证边界

- 只测了每个块里 **1~2 个代表性符号**，不是块内全部声明。
- `TCWIN_CODEPAGE_DEFINED` 出现两次（两个不同的 `#ifndef` 段），只测了
  `MultiByteToWideChar` + `CP_UTF8` 这一侧。
- 判定依据是"编译+链接是否通过"，**未**验证运行时行为。
- 未测"包含 `windows.h` 的顺序不同"是否改变结果（部分声明是传递引入的）。
- **未做删除验证**：本项目铁律是"要证明某样东西必要，必须先把它拿掉再测"
  （P63）。本次只做了"TCC 有没有"这一侧，**没有**逐个删掉垫片再跑全套自检。
  所以"垫片必要"的结论强度是：TCC 确实不提供 → 垫片在起作用；
  但"删掉之后项目会坏在哪里"没有逐项测。
- 本审计是**写入时**的快照；TCC 头文件集变动后需重测。

## 复现方法

对每个块，写一个只包含 TCC 自带头的最小程序：

```c
/* 注意：**不** include tccwin.h */
#include <windows.h>
int main(void){ /* 调用该块提供的 API */ return 0; }
```

然后两个位宽各编一次（注意 `-I` 只给 TCC 自己的，`-L` 必带）：

```
toolchain\tcc\tcc.exe    -I toolchain\tcc\include -I toolchain\tcc\include\winapi ^
                         -L lib\tccdefs   -D_WIN32_WINNT=0x0601 -o t64.exe t.c
toolchain\tcc\i386-win32-tcc.exe  ...同样的 -I... -L lib\tccdefs32 ... -o t32.exe t.c
```

编不过 → 垫片必要；两边都过 → 进入人工复核（不要直接删，先看上面那节）。
