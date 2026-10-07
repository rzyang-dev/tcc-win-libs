# tcc-win-libs

一个**自包含、可直接拷贝**的 Windows C / Lua 开发环境。

**TCC 和 Lua 本身已经是便携的** —— 这个项目的价值不在打包编译器，
而在**库这一层**：挑选一批能用的库，把它们在 TCC 下**调通**，
并把这过程中撞到的每个坑记录下来。

目标是回答一个具体问题：
**在没有安装任何东西、不联网的 Windows 机器上，把 C 代码变成一个能用的
单文件 exe，需要什么？**

答案是 `portable/` —— 拷过去就能用，不需要安装、不需要联网。

---

## 快速开始

```cmd
cd portable
scripts\verify.cmd                      REM 自检，全部通过说明环境完好
scripts\tcc.cmd examples\01-hello.c -Run
scripts\tcc.cmd examples\05-repl.c -With readline,tinyexpr -Run
```

给 agent 用的说明在 [`portable/AGENTS.md`](portable/AGENTS.md)
（拷贝整个 `portable/` 到别的工作区后会被自动加载）。

---

## 仓库内容

本仓库**只包含交付物本身**：

| 路径 | 作用 |
|---|---|
| [`portable/`](portable/) | **交付物**。自包含环境本体，拷走即用 |

研究与文档在 `portable/docs/` 里：

| 文件 | 内容 |
|---|---|
| [`portable/docs/PITFALLS.md`](portable/docs/PITFALLS.md) | 80 条已知坑，按"会不会静默出错"排序 |
| [`portable/docs/LIBRARIES.md`](portable/docs/LIBRARIES.md) | 30 个可用库的清单与用法 |
| [`portable/docs/research/`](portable/docs/research/README.md) | 实测记录（TCC 能力基线、垫片必要性审计等） |
| [`portable/ROADMAP.md`](portable/ROADMAP.md) | 项目进展与后续方向 |

> **不在本仓库里的东西**（有意排除）：
>
> - **`tool/`** —— TCC 原始发行包与 winapi-full 头文件全集。它是**构建输入**，
>   不是运行时依赖；其中 932 个 winapi 头**已全部并入**
>   `portable/toolchain/tcc/include/`。需要上游对照时另行获取。
> - **`copy-test/`** —— `portable/` 的逐字节验证副本，用于确认
>   "拷到别处仍可用"。它**可由 `portable/` 单向复制重新生成**，不必入库。
> - **`libs/`** —— 各库的源码归档。运行时不依赖它：
>   `portable/lib/` 与 `portable/include/` 已含所需源文件。

---

## portable 里有什么

```
portable/
├── toolchain/     TCC 0.9.27（x64 + x86）与 Lua 5.5 源码
├── lib/           30 个库的源码（含 TCC 导入库 lib\tccdefs）
├── include/       tccwin.h / tccmath.h 等垫片头
├── helper/        TCC 缺口的实现（dirent / unistd / time / math 兼容层）
├── scripts/       tcc.cmd / verify.cmd / build-lua.cmd 等入口
├── examples/      示例程序 + 31 个自检探针
├── bin/           预构建的 Lua 解释器
└── docs/          PITFALLS / LIBRARIES / ROADMAP / research
```

### 能力边界

| 能做 | 不能做 |
|---|---|
| C89/C99 子集、无 C++ | C++ 任何版本 |
| Win32 / 现代 Windows API | SIMD intrinsics（无 `emmintrin.h`） |
| 单文件 exe，零依赖 | 需要第三方运行库的场景 |
| Lua 5.5 + C 扩展绑定 | 可视化资源编辑器（`.rc` 要手写） |
| 32 位与 64 位交叉编译 | 调试器（用 `printf` + `-g` 配合外部工具） |
| 编译速度极快（~1 秒完整构建） | 增量构建系统（无 make，用脚本） |

### 已知限制

- **SQLite 只能用 64 位**。32 位下 TCC 会在 `sqlite3_initialize()` 崩溃，
  根因是 TCC 32 位的一个缺陷（`__stdcall` 函数指针经 `void(*)(void)` 槽位
  中转后调用）。32 位构建被**编译期守卫**拦住，而不是静默产出会崩的 exe。
  详见 [PITFALLS P75](portable/docs/PITFALLS.md)。
- **float 数学函数在 32 位下大面积不可用**（实测 22 个），需加 `tccmath.h`。
  详见 [PITFALLS P73](portable/docs/PITFALLS.md)。

---

## 验证

```cmd
cd portable
scripts\verify.cmd
```

当前 **90 项全部通过**。自检覆盖：工具链完整性、30 个 `-With` 库登记一致性、
31 个功能探针、32 位交叉编译、以及真实运行断言。

---

## 许可

`portable/` 内各第三方组件保留其原始许可（多为 Public Domain / MIT / BSD）。
TCC 本身是 LGPL 2.1 —— 它**不限制被编译程序**的许可。
