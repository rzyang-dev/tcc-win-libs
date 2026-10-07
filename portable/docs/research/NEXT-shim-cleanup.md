# 下一步：垫片清理（**本轮已验证：这 5 个不能删**）

> ⚠️ **本文件此前写"共享块里 5 个 Pointer/64 操作可删"，那是错的，已改正。**
> 错因见文末"方法论"一节 —— 又是同一类方法错误。

## 一、正确的实测矩阵（本轮重测，判据可信）

**必须用两种测法，结果不同，不要混：**

### 测法 A：判"TCC 是否自带"（**不含 `tccwin.h`**）

```powershell
# 源文件只写 #include <windows.h>
toolchain\tcc\tcc.exe          -L lib\tccdefs   -o t.exe t.c
toolchain\tcc\i386-win32-tcc.exe -L lib\tccdefs32 -o t.exe t.c
```

| 函数 | 64 位 | 32 位 |
|---|---|---|
| `InterlockedIncrement` / `Decrement` | 自带 | 自带 |
| `InterlockedExchange` | 自带 | 自带 |
| `InterlockedCompareExchange` | 自带 | 自带 |
| `InterlockedCompareExchange64` | 自带 | 自带 |
| `InterlockedIncrement64` | 自带 | 自带 |
| `InterlockedOr` / `And` / `Xor` | 自带 | **需垫片** |
| `InterlockedExchangeAdd` | **需垫片** | 自带 |
| `InterlockedExchangeAdd64` | **需垫片** | 自带 |
| **`InterlockedExchangePointer`** | **需垫片** | **需垫片** |
| **`InterlockedCompareExchangePointer`** | **需垫片** | **需垫片** |
| **`InterlockedAnd64` / `Or64` / `Xor64`** | **需垫片** | **需垫片** |
| `GlobalMemoryStatusEx` | 自带（带 `-L`） | 自带（带 `-L`） |

### 测法 B：判"用 `tccwin.h` 时能不能用"（**含 `tccwin.h`**）

```powershell
scripts\tcc.cmd t.c -Quiet        # 64 位
scripts\tcc.cmd t.c -Quiet -32    # 32 位
```

**这 5 个（Pointer 两个 + 64 位 And/Or/Xor）在测法 B 下两位宽都 OK，
但在测法 A 下两位宽都需要垫片。**

### ⚠️ 上面这个差异就是本轮踩的坑

我用**测法 B** 得到"5 个都 OK"，据此判定可删 —— **错了**。
删掉之后 `scripts\tcc.cmd ... -32` 立刻报：

```
warning: implicit declaration of function 'InterlockedExchangePointer'
undefined symbol 'InterlockedExchangePointer'
undefined symbol 'InterlockedAnd64'
（5 个全报）
```

**原因**：测法 B 下垫片仍在生效，所以"OK"是垫片自己造成的假象。
**判定"垫片能不能删"必须用测法 A**（不含 `tccwin.h`）。

**结论：这 5 个垫片都必须保留。本轮已还原，未做任何删除。**

## 二、真正可删的（测法 A 判定，但同样要小心连带依赖）

| 函数 | 处置 | 理由 |
|---|---|---|
| `InterlockedIncrement` / `Decrement`（i386 块 L138-149） | 可删 | 测法 A：32 位自带 |
| `InterlockedExchange`（i386 块 L155-159） | 可删 | 测法 A：32 位自带 |
| `InterlockedCompareExchange`（i386 块 L160-168） | 可删 | 测法 A：32 位自带 |

⚠️ **但删之前必须确认没有内部依赖**。已确认的依赖链：

```
i386 块内：
  InterlockedExchangeAdd           <- 基础（LOCK xaddl），必须留
    └─ InterlockedAdd              <- 依赖 ExchangeAdd，必须留
    └─ InterlockedExchangeAdd64
         └─ InterlockedIncrement64 / Decrement64
  InterlockedCompareExchange64     <- 基础（LOCK cmpxchg8b），必须留
    └─ InterlockedExchange64
```

`InterlockedIncrement/Decrement/Exchange/CompareExchange` 不在这条链上，
所以理论上可删；但**收益为零**（它们能工作），风险非零。

## 三、判定"能不能删"的标准流程（本轮血的教训）

对每一个候选：

1. **用测法 A 确认 TCC 自带**（源文件只 `#include <windows.h>`，不含 tccwin.h）
2. 删除该实现
3. `scripts\tcc.cmd t.c -Quiet` **和** `scripts\tcc.cmd t.c -Quiet -32` 都过
4. `portable\scripts\verify.cmd` 与副本自检都 **67 通过 0 失败**
5. 特别看 `[OK] 32-bit sizeof(void*) + 现代 API: 4 1 1`
6. 同步 copy-test，逐字节差异 0

**任何一步不过就还原。** 本轮就是这样发现判断错误的。

## 四、方法论：为什么又错了

这是本项目第三次同类错误：

| 次数 | 错误 | 正确做法 |
|---|---|---|
| 1 | 用 `tcc.exe -I <portable>\include` 测 TCC 能力 | 不加 `-I` |
| 2 | 漏 `-L lib\tccdefs` 测链接 | 链接必带 `-L` |
| 3 | **用含 `tccwin.h` 的测试判"垫片能否删"** | **判垫片要删掉垫片再测** |

**通用规则：要验证"某样东西是否必要"，必须先把它拿掉再测。**
带着它测出的"OK"永远不能作为"可以拿掉"的依据。

这条已值得写进 `PITFALLS.md`。

## 五、还要不要继续清理

**可以停在这里。** 理由：

- 现有垫片无 bug，功能完整，67 项自检全过
- 剩下的"可删项"（i386 块的 4 个）收益为零
- 每次删除都是一次回归风险，而本轮已证明判断容易出错

如果要做，按第三节的流程逐项来，一次只删一个。
