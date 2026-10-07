# 能力补齐路线图

本文件跟踪 portable 环境的能力缺口补齐进度。
**每一项都必须通过验证才算完成**，验证标准见下。

---

## 验收标准（缺一不可）

一个能力项只有同时满足以下全部条件，才能从「待办」移到「已完成」：

| # | 要求 | 为什么 |
|---|---|---|
| 1 | TCC 实测编译通过 | 不是"应该能编" |
| 2 | **真实功能验证**，不是只编译 | 编译通过但结果错误是最危险的情形 |
| 3 | 库缺陷要定位到具体行 | 并**用第二个编译器交叉验证**，区分「TCC 限制」与「库自身 bug」 |
| 4 | 集成进 `-With` | 使用者不该手工拼编译参数 |
| 5 | 自检探针加入 `examples/probes/` | 拷到新位置能自动验证 |
| 6 | 更新 `docs/LIBRARIES.md` 与 `docs/PITFALLS.md` | 坑要留档，否则下一轮重踩 |
| 7 | `scripts/verify.cmd` 全项通过 | 包括已有的检查项，不能回归 |
| 8 | 明确标注「未验证边界」 | 没测到的部分要说出来，不能含糊 |

## 风格约束（不得违反）

- 单文件或双文件优先；纯 C，无 C++
- 宽松许可：公有领域 / MIT / BSD / zlib。**GPL 不纳入**（传染性）
- 不引入需要安装的运行时依赖；产物仍是单文件 exe
- 不使用需要 SIMD intrinsic 的库（TCC 没有 `emmintrin.h`）
- 新增不得破坏已有自检项

---

## 进度总览

图例：`[ ]` 待办 · `[~]` 进行中 · `[x]` 已完成 · `[!]` 已验证但有保留 · `[-]` 决定不做

| 优先级 | 能力 | 候选 | 状态 |
|---|---|---|---|
| P1 | 网络 socket | 自建 Winsock 垫片 + `ws2_32` | `[x]` 38/38 真实收发通过 |
| P1 | GUI 窗口组件 | Nuklear v4.13.3（公有领域） | `[x]` 43/43 几何+命中+绘制通过 |
| P2 | 正则表达式 | tiny-regex-c（公有领域） | `[x]` 56/56 + 2 项已知限制 |
| P2 | SQLite | 3.53.4 官方 amalgamation（公有领域） | `[x]` 26/26 真实建库+事务 |
| P2 | 终端 readline | 自研（linenoise 是 POSIX-only） | `[x]` 94/94 分两层可测 |
| P3 | 跨平台线程 | 不引入（环境已有 Win32 原语） | `[-]` 见下方说明 |
| P3 | 字体渲染 | Nuklear 内置 stb_truetype | `[x]` 24/24 含外部 TTF 光栅化 |
| — | 进程管理 | Win32 已够用 | `[-]` |
| — | 调试器 | 无可行方案 | `[-]` |

---

## 当前阶段：补齐声明缺口（由能力审计得出）

对工具链做了一次系统性审计（标准头覆盖、CRT 函数、Windows API 分 12 类
逐个测声明），结论是：**核心覆盖已经很好**（测的 175 个 Win32 API 只缺 6 个，
201 个 CRT 函数缺 21 个），缺口集中在少数几类。

审计方法：对每个符号生成 `void *p = (void*)&name;` 的探针逐个编译，
有声明才能通过。这样测的是**声明是否存在**，比读头文件列表可靠。

### 优先事项

| 优先级 | 项 | 说明 |
|---|---|---|
| ~~**A0**~~ | ~~核实 winapi 头文件是否被裁减过~~ | **已完成** —— 结论见下（**"被裁减"这个措辞是错的，已改正**） |
| ~~**A1**~~ | ~~补 Win32 API 声明~~ | **已完成** —— 由装入真 `winnls.h` / `aclapi.h` 解决；`GetAdaptersAddresses` 需官方 `winsock2.h`（它提供 `_WINSOCK2API_`），待第 2 步整批纳入时一并解决 |
| ~~A2~~ | ~~补 C99 浮点比较宏~~ | **已完成** —— 实测 TCC 的 math.h:668 用 `#if __GNUC__ >= 3` 分两条路，**两条都不可用**（真分支要 `__builtin_isgreater`，TCC 不实现；假分支用约束 `"t"`，TCC 报 `unknown constraint`）。已在 `tccmath.h` 用 `x != x` 判 NaN 实现，语义符合 C99 7.12.14，probe_winapi 有 7 项 NaN 语义断言 |
| ~~A3~~ | ~~补 CRT 声明~~ | **已完成** —— 实测 `_aligned_malloc`/`_aligned_free`/`_controlfp`/`_statusfp`/`strerror_s`/`strnlen` **在 msvcrt.dll 里都有导出**（用 `tcc -impdef` 查过），只需补声明，不需要自己实现。已加进 `tccwin.h` 的 `TCWIN_CRT_COMPAT_DECL` 块全局生效。`_set_se_translator` **没有导出**且依赖 SEH，故意不声明 |
| ~~A4~~ | ~~每补一批就加自检~~ | **已完成** —— 新增 `probe_winapi.c`（**66 项断言**，全部真实调用：编码页往返、Vista API、CRT 缺口、C99 宏与 NaN 语义）。自检项 **65 → 67** |

### A0 结论：**不是"被裁减"** —— TCC 发行版自带一部分，其余可从 winapi-full 取用

> **本段此前写成"winapi 头文件确实被裁减（18 / 755），且这是缺声明的主因"，
> 措辞是错的。** 依据是官方 `winapi-full-for-0.9.27/readme.txt` 原文：
>
> > A part of these files is already included (with minor modifications)
> > with the tcc-0.9.27 release. Original versions of these files can be
> > found in the 'orig' folder.
> > The other files (from the 'include' folder) may be copied to the tcc
> > installation or used with -I or -isystem.
> > **Compatibility with tcc has not been checked.**
>
> 也就是说：`portable/toolchain/tcc/include/winapi/` 里那 18 个是
> **TCC 发行版自带的、带 TCC 修改的版本**（不是被人删剩的），
> `orig/` 存的是它们的上游原版。其余 700 多个是 **TCC 没带的**，
> 官方说"可以复制过去用，但兼容性没验证过"。

**正确的事实陈述**：

| 事实 | 说明 |
|---|---|
| TCC 自带头集**不含** `winsock2.h` / `ws2tcpip.h` / `winnls.h` / `commctrl.h` 等 | 这是 TCC 发行版的原始状态，**不是被裁减** |
| `winapi-full-for-0.9.27/include/` 有 **755** 个 winapi 头 + 138 个 ddk + 少量 GL/sys/顶层 | 官方提供，可按需取用 |
| `windows.h` 引用的 28 个缺失头**实测 28/28 都能从中取到** | 见 `docs/research/TOOL-AUDIT.md` |
| 整批使用是**用户已确认可行**的策略（"winapi-full 没问题，大胆用"） | 不再分批试探 |

**已建立的规则（后续一律照此办理）**：
缺声明时**先查 `tool/tcc/winapi-full-for-0.9.27/include/`** 里有没有对应头文件；
有就把头文件装进来并**删掉 `tccwin.h` 里的重复声明**，而不是写新垫片。
两类真实冲突已经踩过（详见 PITFALLS **P61**）：

- `NLSVERSIONINFO`：垫片 4 字段 20 字节 vs 真 SDK 3 字段 12 字节
- `GetCPInfo`：垫片 `TCWIN_CPINFO*` vs 真头文件 `LPCPINFO`

### 剩余垫片的正确范围（依据 docs/research/TCC-BASELINE.md 的实测矩阵）

> **已完成逐块审计**：见 [`docs/research/SHIM-NECESSITY-AUDIT.md`](docs/research/SHIM-NECESSITY-AUDIT.md)。
> 结论：`tccwin.h` 的 18 个块里 **14 块实测确认必要**（TCC 两位宽都不提供），
> 另 4 块探测通过但复核后保留（代价为零、且让不包含 `windows.h` 的代码也能用）。
> **所以"只保留类别 C 的垫片"这条的实际结果是：现状已符合。**

清理后 `tccwin.h` **只应保留**这几类，其余删除：

| 类别 | 内容 | 为什么必须保留 |
|---|---|---|
| 64 位缺实现 | `InterlockedExchangeAdd`、`InterlockedExchangeAdd64` | `undefined symbol '_InterlockedExchangeAdd'` |
| 32 位缺实现 | `InterlockedOr`、`InterlockedAnd`、`InterlockedXor` | `undefined symbol 'InterlockedOr'` |
| Vista/7 API（真头也没有） | `CONDITION_VARIABLE`、`INIT_ONCE`、`SYNCHRONIZATION_BARRIER`、`GetTickCount64`、`InitializeCriticalSectionEx`、`CreateThreadpoolWork`、`FILE_INFO_BY_HANDLE_CLASS`、`GetActiveProcessorCount`、文件信息结构体、线程池 | 2009 年的头文件集里没有（那个时代还没有这些 API） |
| 非 Win32 的 CRT 缺口 | `strnlen`、`strtoll`、`strtoull`、`strerror_s`、`_aligned_malloc`、`_controlfp`、`isgreater` 系列、`_set_se_translator` | msvcrt.dll 是 C89 时代的，这些确实没有 |
| TCC 自身缺陷 | `math.h` 的 4 个 float 函数（`fabsf`/`truncf`/`cbrt`/`nearbyintf`） | 见 P5，不是头文件问题 |

**必须删除的**（实测 TCC 已提供）：`InterlockedIncrement` / `Decrement` /
`Exchange` / `CompareExchange` / `CompareExchange64` / `CompareExchangePointer` /
`ExchangePointer` / `Increment64` / `Or` / `And` / `Xor` 的**64 位**垫片，
以及 `GlobalMemoryStatusEx` 的声明；`tccmath.h` 里那 20 个冗余 float 宏。

> **注意别误删**：`strtof` **确实缺**（见 P7），`InterlockedExchangeAdd`/
> `ExchangeAdd64`（64 位）与 `Or`/`And`/`Xor`（32 位）**确实缺**。
> 判据一律以 `docs/research/TCC-BASELINE.md` 的矩阵为准，不要凭印象。

### 后续事项

| 优先级 | 项 | 说明 |
|---|---|---|
| ~~B1~~ | ~~UTF-8 **输入**~~ | **已完成** —— 修掉两个真问题：① `tcwin_rl_translate_key` 取 `uChar.AsciiChar` 把"中"静默变成 0x2D（union 低字节），改取 `UnicodeChar` 转 UTF-8；② 插入/退格/删除/左右移原来全按**字节**，改为按**码点**，光标列改用**显示宽度**。纯逻辑抽到 `include/tcwin_rl_utf8.h`，`probe_rl_utf8.c` 98 项 + `probe_readline.c` 新增 19 项行为断言。见 PITFALLS P64/P65 |
| ~~B2~~ | ~~CJK 字体~~ | **已完成** —— 实测 `simhei.ttf` 可加载、汉字字形齐全、中文宽度是 ASCII 两倍（18px vs 9px @18pt）。**关键数据：图集代价取决于 range，差 260 倍** —— 只 ASCII 0.02 MB、25 个界面汉字 0.03 MB、`nk_font_chinese_glyph_ranges()` 8.00 MB。两个硬约束：`range` 每个区间必须 `f <= t`（否则 abort），用 `stbtt_*` 必须先 `#define NK_IMPLEMENTATION`。见 PITFALLS P67 |
| ~~B3~~ | ~~SQLite 并发压测~~ | **已完成** —— 实测为序列化模式（`THREADSAFE=1` + `MUTEX_W32`）。3 个并发场景全过：共享连接计数不丢（4000/4000）、独立连接并发写 800 次 0 BUSY、WAL 下 60 万次读样本从未见到半个事务。**并发现一条真实约束**：序列化模式只保护单次 API 调用，**不保护跨语句事务** —— 共享连接上用 BEGIN/COMMIT 会报 `cannot start a transaction within a transaction`。见 PITFALLS P66 |
| B4 | 音频解码 / XML / YAML / 加密 / HTTP 客户端 | 库类别缺口，尚无候选评估 |

### 暂缓（等设计讨论）

| 项 | 状态 |
|---|---|
| **`.rc` 资源支持** | **等用户讨论设计后再定**。TCC 没有 `rc.exe`/`windres`。可选方向：C 源码内嵌资源（`helper/txt2c.exe` 已有类似思路）、或构建期调用 MinGW `windres`（不违反便携约束，产物仍是单文件） |

### 已确认的边界（不是缺口，不打算补）

- **无 C++** —— TCC 不支持，无法补
- **无 SIMD intrinsics** —— 没有 `emmintrin.h`。注意 **SSE 指令本身可用**
  （TCC 支持内联汇编），只是没有 intrinsics 包装层
- **POSIX 层缺失** —— `unistd.h` / `termios.h` / `pthread.h` / `sys/ioctl.h` /
  `poll.h` 都没有，这是 linenoise 编译不了的原因。移植 POSIX 源码需适配
- **无 make / 增量构建** —— 完整构建 0.86 秒，实际影响很小

---

## P1 · 网络 socket

**为什么优先**：这是唯一"完全无法绕过"的缺口。TCC 的 `include/` 下
**没有任何** winsock 头文件（`winsock2.h` / `ws2tcpip.h` / `winsock.h` 全无）。
任何网络库都会在 `#include <winsock2.h>` 处直接失败。

**难点**：需要手写最小 Winsock 头。风险在于**结构体布局必须与真实 Win32 ABI 一致**，
而错误只在真实收发数据时才暴露。

**验收要求（比其他项更严）**：
- 不能只验证编译 —— 必须**真实建立 TCP 连接、收发数据、正确关闭**
- 建议验证方式：本机起监听 socket，客户端连上去发一段数据再收回，
  内容逐字节比对；再验证 `WSAStartup` / `WSACleanup` 配对、
  错误码路径（连不存在的端口应返回 `WSAECONNREFUSED`）
- UDP 也要覆盖（`sendto` / `recvfrom`）
- 需要交叉验证结构体大小：用 `sizeof` 与文档值比对
  （`sockaddr_in`=16、`sockaddr_in6`=28、`fd_set` 等）

**候选库**（垫片做好后再评估）：mongoose、civetweb、zed_net、enet

---

## P1 · GUI 窗口组件

**为什么优先**：目前**完全没有替代方案**。现有环境只能手写 Win32 窗口过程，
而这对于任何非平凡界面都不现实。

**候选**：

| 库 | 文件数 | 许可 | 备注 |
|---|---|---|---|
| Nuklear | 1 | 公有领域 | 即时模式 GUI，成熟，但需要后端（GDI 或 OpenGL） |
| Clay | 1 | zlib | 布局引擎，不含控件，需自绘 |

Nuklear 本身只做"控件 + 布局"，渲染要后端。最省的路径是 **GDI 后端**
（环境已有 gdi32 导入库），避免引入 OpenGL。

**验收要求**：
- 创建一个真实窗口，画出一个控件（按钮/滑块），
- 用 `WM_LBUTTONDOWN` 消息模拟点击，验证控件状态真的变化
- 窗口必须在**测试后自动关闭**，不能留 UI 在用户桌面
- 若走 GDI 后端，验证渲染出的像素（`GetPixel`）符合预期

---

## P2 · 正则表达式

**可用于**：日志过滤、文本抽取、输入校验

**候选**：Remimu（1 文件, CC0）、SLRE（2 文件, **GPL2 → 排除**）
另外可用 Lua 的 `string.find`/`match` 顶替多数场景，所以优先级为中。

**验收要求**：至少 20 组正则案例，含分组捕获、量词、锚点、字符类、转义；
与已知正确结果逐条比对。

---

## P2 · SQLite

**为什么值得**：官方 amalgamation（`sqlite3.c` + `sqlite3.h`）是**官方推荐的
单文件用法**，不是第三方打包。许可为公有领域。

**已知难点**：
- `sqlite3.c` 约 900 万字节，是环境里最大的单个文件 —— 编译时间会明显增加
- TCC 可能在某些 C99 语法上受阻，需实测
- 可能需要 `-DSQLITE_OMIT_*` 裁剪掉不需要的特性以加速编译

**验收要求**：建表 → 插入 → 查询 → 事务回滚 → 关闭；
验证数据真的落盘并能重新打开读到；测试并发读。

---

## P2 · 终端 readline

**候选**：Linenoise（2 文件, BSD）—— 行编辑 + 历史。

**验收要求**：非交互验证（管道输入）能读到行；历史文件能正确读写。
交互行为无法自动化验证，需明确标注。

---

## P3 · 跨平台线程

**候选**：TinyCThread（2 文件, zlib）—— C11 `<threads.h>` 实现。

**注意**：环境已有 `tccwin.h` 提供完整 Win32 同步原语（事件、互斥、
信号量、条件变量、线程池），所以此项价值在**可移植性**而非能力。

**验收要求**：多线程竞争测试（复用已有的 `atomic_stress.c` 思路），
验证互斥与条件变量在真实竞争下正确。

---

## P3 · 字体渲染

**候选**：stb_truetype（1 文件, 公有领域）。

**为什么 P3**：只有配合 GUI 才有意义。应在 GUI 完成后做。

**验收要求**：加载一个系统字体（如 `C:\Windows\Fonts\consola.ttf`），
取字形轮廓，渲染成位图，验证位图非空且形状合理（例如 'I' 比 'M' 窄）。

---

## 明确不做

| 项 | 原因 |
|---|---|
| C++ 生态 | TCC 不支持 C++ |
| GPL 许可的库 | 传染性，不适合作为通用基础库 |
| 需要 SIMD 的库 | TCC 无 `emmintrin.h`、无 `__m128i` |
| 需要包管理器的依赖 | 违反"可拷贝"原则 |
| 调试器 | 无可行方案；用 `printf` + `-g` 配合外部工具 |
| 进程管理库 | Win32 API 已足够，无需额外抽象层 |

---

## 每轮开始前先做

```cmd
scripts\verify.cmd
```

确认环境完好（应为 44 项通过）。若有失败项，先修复再开始新工作 ——
否则无法区分"新引入的问题"与"原有的损坏"。

## 完成后必做

1. 把新探针放进 `examples/probes/`
2. 在 `verify.ps1` 的 `$map` 里注册探针与其需要的 `-With`
3. 重跑 `verify.cmd`，确认总数增加且无失败
4. 更新 `docs/LIBRARIES.md`（新库条目）与 `docs/PITFALLS.md`（新坑，若有）
5. 更新本文件的进度表
6. 同步 `copy-test/portable` 副本（如有）或删除过期副本


---

## 维护注意（踩过的坑）

### 清理临时文件不要用宽通配

清理本轮诊断产物时我用了 `-Filter '_*'`，**误删了 TCC 自己的
`toolchain\tcc\include\_mingw.h`**，导致副本环境整个编译不了
（报 `include file '_mingw.h' not found`，而错误指向 `windows.h:17`，
很难联想到是清理造成的）。

**规则**：TCC 发行版里有以 `_` 开头的文件（`_mingw.h`）。
清理时用**精确文件名名单**，或用带前缀的私有命名（如 `_probe_*`）。
删完一定要跑一次 `scripts\verify.cmd`。

### 每次改动后同步副本

`copy-test\portable` 用于验证"拷贝到别处仍可用"。
改动 `portable` 后要同步，并**两边各跑一次自检**：

```powershell
# 同步（scripts/、include/、examples/、docs/ 都要）
Copy-Item portable\scripts\* copy-test\portable\scripts\ -Force -Recurse
```

只同步 `scripts/` 而漏了 `include/` 会出现"主目录正常、副本失败"。

### 保持体积

`bin/` 里的编译产物每个可达 600 KB（Nuklear 探针）。
诊断用的临时 exe 用完要删，否则一次排查就能让体积涨 6 MB。
当前规模：**9.4 MB / 293 文件**。

---

## P3 不引入 · 跨平台线程（TinyCThread）

**决策：不纳入。** 理由是它在这个环境里不解决问题，只增加复杂度。

### 环境已有的线程原语（全部已验证）

| 能力 | 来源 | 状态 |
|---|---|---|
| 线程创建/等待 | `CreateThread` / `WaitForSingleObject` | 已在 socket 探针里用于 TCP/UDP 服务端线程 |
| 原子操作 | `InterlockedCompareExchange` / `...64` / `...Pointer` | `tccwin.h` 有完整垫片（TCC 缺 `ExchangeAdd`/`Add` 等） |
| 原子读写 | `InterlockedExchange` / `InterlockedOr` 等 | 同上 |
| 互斥 | `CRITICAL_SECTION` | 已验证，且踩过"复制结构体破坏互斥"的坑（PITFALLS） |
| 条件变量 | `CONDITION_VARIABLE` + `SleepConditionVariableCS` | `tccwin.h` 已补类型与 API |
| 一次性初始化 | `INIT_ONCE` | `tccwin.h` 已补 |
| 线程池 | `CreateThreadpoolWork` 等 | `tccwin.h` 已补 |

也就是说：**在 Windows 上写多线程 C 代码，这个环境现在就能做**，
不需要额外任何东西。

### 为什么不加 TinyCThread

1. **它解决的是"同一份代码在 Windows/Linux 都能编译"** ——
   但本环境的目标平台就是 Windows（TCC 是 Windows 发行版，
   产物是单文件 exe）。跨平台抽象在这里没有消费者。
2. **它是有成本的**：多一层间接、多一处需要验证的代码路径，
   而且它的 `thrd_*` 语义与 Win32 并非一一对应，
   出问题时排查要多穿透一层。
3. **与本环境的风格冲突**：环境的原则是"零依赖、只做必要的事"。
   已经能用的能力不重复引入。

### 如果将来真需要

要在别的平台用同一份源码时，再按需引入即可 ——
当前的 Win32 原语都封装在 `tccwin.h` 里，
替换成 TinyCThread 的 `thrd_*` 是机械工作，不构成锁定。

**结论**：这一项标记为"不引入"而不是"未完成"。

### 编辑 verify.ps1 / tcc.ps1 必须保留 UTF-8 BOM

这两个脚本含中文，**必须带 BOM**。丢了 BOM 的后果很严重且很难联想：

```
Unexpected token '{' in expression or statement.
Missing closing ')' in expression.
The string is missing the terminator: ".
```

看起来像语法被改坏了，实际只是 PowerShell 按 ANSI 读了中文注释。

**本轮踩到了**：用 `[System.IO.File]::WriteAllLines($f, $lines, (New-Object System.Text.UTF8Encoding($true)))`
写入时**丢了 BOM**（而非 `$true` 参数的问题，是 `WriteAllLines` 与 BOM 的交互），
导致整个自检脚本解析失败。

**规则**：
- 改这两个脚本一律用
  `[System.IO.File]::WriteAllText($f, $text, (New-Object System.Text.UTF8Encoding($true)))`
- 改完**立刻确认前三个字节是 `ef bb bf`**，再跑自检：

```powershell
$b = [System.IO.File]::ReadAllBytes($f)[0..2] | ForEach-Object { $_.ToString('x2') }
# 必须是 ef bb bf
```

### examples 编译要用独立临时目录

`verify.ps1` 里探针循环结束后 `$tmp` 就被删除了。
examples 段若复用 `$tmp` 会报
`tcc: error: could not write '...': No such file or directory`。
必须新建 `$exTmp` 并在段末清理。
