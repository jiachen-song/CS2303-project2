# 项目 2 第二部分：工具注册表与并行执行

你的第一部分代理已经可以工作了。第二部分的目标是演进这个系统，同时保持其外部行为不变。

## 系统问题

第一部分存在两个设计压力。

首先，工具知识分散在整个系统中。代理循环知道如何调度一个具体的工具；LLM 客户端知道如何在传输线路上描述该工具；工具头文件暴露了只有某些模块才需要的实现细节。这对于一个工具来说尚可管理，但对于四个或更多工具来说就变得脆弱。

其次，执行器将一批工具调用视为一个平面列表。三个独立的读取操作被串行化，即使一个读取不依赖于另一个读取的任何内容。解决这个问题需要的不仅仅是并行运行所有操作：写入、编辑、未知工具和消息排序仍然很重要。

到本部分结束时，系统应该有一个用于工具发现的路径，一个用于工具调度的路径，以及一个能够利用只读并行性同时保留 LLM 所见内容的执行器契约。

> **提示：稳定的接口，可替换的实现**
>
> 第二部分改变了代理的内部构建方式。外部契约——与 LLM 对话、运行工具、将结果推入历史记录——保持不变。这与操作系统试图在稳定的外部接口和内核内部实现选择之间保持的分离是同一类型。

---

## 1. 起点

从你工作的第一部分代码树开始。第二部分包提供了新的框架部分和测试。将这些文件复制到你的第一部分代码树中。之后，项目可能无法构建。这是预期的。新的框架代码引用了你的第一部分代码尚未了解的接口。

完整的构建和测试目标列在末尾。在阶段 A 期间，预期 `make test-a` 和 `make test-cunit` 会首先变得有意义；在阶段 B 期间，转向 `make test-b` 和 TSan 执行器测试。

该包提供：

- **框架代码**: `tools/registry.c`, `tools/executor.{h,c}`, `tools/sandbox.{h,c}`
- **测试**: `tests/test_part2_a.py`, `tests/test_part2_b.py`, `tests/cunit/{test_registry,test_sandbox,test_executor}.c`, 以及一个更新的测试框架 (`harness.py`, `run_tests.py`)。更新的框架是向后兼容的：你的第一部分测试将继续工作。
- **构建配置**: `Makefile` 包含所有第二部分目标。这是评分用的构建文件。如果你的第一部分 Makefile 有本地修改，请将它们合并到这个文件中。

---

## 2. 阶段 A — 多种工具的统一接口

外部行为保持不变。改变的是所有权。

在此阶段之后，代理循环应该独立于存在哪些工具。LLM 客户端应该从注册表发现工具。一个具体的工具应该拥有其运行时实现和宣传它所需的元数据。

### 接口

你的第一部分 `tools/tools.h` 是必须演进的边界之一。在阶段 A 开始时，框架需要以下通用工具接口：

```c
typedef struct {
    bool ok;
    char *output;        /* 堆分配；由 tool_result_free 释放 */
} ToolResult;

void tool_result_free(ToolResult *r);

#define MAX_TOOL_OUTPUT 50000

typedef ToolResult (*ToolFn)(cJSON *args);

typedef struct {
    const char *name;
    const char *desc;
    const char *param_schema;
    ToolFn exec;
} ToolDef;

#define MAX_REGISTERED_TOOLS 16

void tools_init(void);
void tool_register(ToolDef *def);
ToolDef *tool_find(const char *name);
ToolDef *const *tool_list(int *out_count);
```

在阶段 A 完成之前，你将用一个新的调度字段扩展同一个结构体。

这些类型是你的工具、注册表和执行器之间的契约。`ToolDef` 捆绑了系统需要了解的关于一个工具的所有信息：它的传输名称、LLM 读取的描述、其参数的 JSON 模式以及运行它的函数。`ToolResult` 是一个小的值类型，因此每个工具中的每个返回路径都必须显式地构造一个。

> **提示：分发表**
>
> 注册表就是一个分发表。调用者不需要为每个实现分别设置分支。系统调用表、VFS 操作表和设备驱动程序接口都使用这种形式：在具体操作变化时保持调用者稳定。

### 阅读提供的边界

首先阅读提供的框架文件。它们定义了实现和测试一致同意的契约。

**`tools/registry.c`** 实现了注册表。它将 `ToolDef` 指针存储在一个平面数组中，在启动时一次性填充。阅读它以理解系统如何将注册的定义转变为执行器稍后可以找到的东西。

**`tools/executor.h`** 定义了 `executor_run_tools` —— 代理调用以运行一批工具调用的函数。阅读契约注释：它规定了执行器对排序的承诺以及如何报告执行失败。

**`tools/executor.c`** 实现了该契约。在阶段 A，它按请求顺序串行运行工具。阅读 `ToolTask` 结构体、`run_one` 函数和结果消息构造循环——这些是你的文件工具将要经过的路径。

**`tools/sandbox.h`** 是工作区隔离边界。`resolve_workspace_path` 规范化相对路径并拒绝任何解析到工作区目录之外的路径。

> **提示：保护边界**
>
> 沙盒是 LLM 提供的路径和文件系统之间的边界。来自高层的请求只是一个请求；运行时拥有强制执行权。如果每个工具都重新实现路径验证，边界就已经泄漏了。在实际系统中，沙盒会更复杂和健壮。

### 将 bash 封装为 ToolDef

你的 `tools/bash.c` 有一个可用的 fork/exec 实现。将其封装在一个 `ToolDef` 中，以便注册表可以通过统一接口找到它。

```c
ToolDef bash_def = {
    .name = "bash",
    .desc = "运行一个 shell 命令并返回其合并的 stdout/stderr。",
    .param_schema = "{\"type\":\"object\","
                    "\"properties\":{\"command\":{\"type\":\"string\","
                    "\"description\":\"要执行的 shell 命令\"}},"
                    "\"required\":[\"command\"]}",
    .exec = tool_bash,
};
```

你的文件工具遵循相同的形状：每个工具一个 `ToolDef`，实现函数通过 `.exec` 指针到达。

### 文件工具

创建 `tools/read.c`, `tools/write.c`, 和 `tools/edit.c`。阅读 `tools/registry.c` 以查看注册表期望链接哪些符号。

每个文件工具在打开任何文件之前，都必须通过 `resolve_workspace_path` 验证其 `path` 参数。沙盒检查是防止像 `read_file("../../etc/passwd")` 这样的路径变成真实信息泄露的边界。

三个工具：

- **`read_file`** — 参数 `{"path": "...", "limit"?: N}`。返回相对于工作区的文件内容（UTF-8）。如果指定了 `limit`，则在那么多行之后截断。无论如何都要限制在 `MAX_TOOL_OUTPUT` 内。失败时（文件缺失、沙盒拒绝等），返回 `ok=false` 并附带一条简短的、LLM 可读的消息。这对于工作区状态是只读的。

- **`write_file`** — 参数 `{"path": "...", "content": "..."}`。用 `content` 替换文件的内容。父目录必须已经存在；工具不执行 `mkdir -p`。成功时返回一条消息，指明文件名和字节数。

- **`edit_file`** — 参数 `{"path": "...", "old_text": "...", "new_text": "..."}`。用 `new_text` 替换文件中第一个精确匹配的 `old_text` 出现位置。如果没有匹配项，则失败。契约是"第一个匹配项"。

模式如下所示。

`read_file`:

```json
{"type":"object","properties":{"path":{"type":"string","description":"工作区内的相对路径"},"limit":{"type":"integer","description":"可选的要返回的最大行数"}},"required":["path"]}
```

`write_file`:

```json
{"type":"object","properties":{"path":{"type":"string","description":"工作区内的相对路径"},"content":{"type":"string","description":"要写入的完整文件内容"}},"required":["path","content"]}
```

`edit_file`:

```json
{"type":"object","properties":{"path":{"type":"string","description":"工作区内的相对路径"},"old_text":{"type":"string","description":"要查找的精确子字符串"},"new_text":{"type":"string","description":"替换文本"}},"required":["path","old_text","new_text"]}
```

### 连接系统

一旦你的工具编译完成并导出其 `ToolDef` 符号，仍然需要三件事才能使系统端到端工作。

注册表必须知道新工具。`tools/registry.c` 是每个工具进入系统的地方——阅读它并注册你的工具。注册表必须在第一个 LLM 请求询问存在哪些工具之前被填充；这是 `main.c` 中的一个启动顺序依赖。

> **提示：启动顺序**
>
> 子系统有初始化依赖。文件系统在其驱动程序注册之前无法挂载；设备在其设备表知道它之前无法打开。在这里，LLM 客户端在注册表被填充之前无法通告工具。

LLM 客户端应该从注册表发现工具模式。传输格式仍然期望每个工具一个函数条目，模式作为原始 JSON 插入。在你的 `build_tools_json` 中遍历 `tool_list()` 并为每个 `ToolDef` 发出一个条目。

在助手消息被附加到历史记录之后，代理应该将完整的工具调用批次交给执行器，并将返回的工具消息按请求顺序推入历史记录。阅读 `executor.h` 中的 `executor_run_tools` 契约，以了解代理传入什么以及它得到什么返回。

检查你的设计的一个有用方法：

- 稍后添加另一个工具时，哪些现有文件需要更改？
- 如果答案包括代理循环，哪个决定仍然在错误的模块中？
- 如果答案包括 LLM 客户端，工具元数据应该放在哪里？
- 如果一个工具在运行时未知，哪个层应该将其变成一个观察结果而不是崩溃？

避免在代理循环中添加另一个 `if (strcmp(name, ...))` 路径。这可能通过一个工具测试，但它让原始的设计压力依然存在。

### 用 `read_only` 扩展 `ToolDef`

在完成阶段 A 之前，用调度元数据扩展工具接口。阶段 B 需要执行器区分仅观察状态的工具和可能更改状态的工具。向 `ToolDef` 添加一个布尔字段：

```c
bool read_only;
```

在每个 `ToolDef` 字面量中使用指定初始化器（`.read_only = true`），这样字段顺序就不会成为隐藏的依赖。在这个系统中，`read_file` 是唯一的只读工具。`write_file` 和 `edit_file` 更改工作区状态。`bash` 被视为状态更改，因为运行时无法知道任意 shell 命令会做什么。

> **提示：接口元数据**
>
> 接口携带的不仅仅是函数指针。它可以携带改变运行时被允许做什么的元数据。文件模式、页面权限、进程状态和设备能力都影响调度或保护决策。在这里，`read_only` 是一个具有运行时后果的小型元数据。

### 阶段 A 测试检查的内容

`tests/test_part2_a.py`:

- **`tools_advertised`**: 发送给 mock 的传输格式请求包含四个工具 `bash`, `edit_file`, `read_file`, 和 `write_file`。如果 LLM 客户端仍然返回空的工具列表，这会立即捕获它。
- **`bash_through_registry`**: 第一部分的 bash 工具仍然通过新的调度路径工作。
- **`read_file`**, **`write_file`**, **`edit_file`**: 每个针对临时工作区测试一个工具。代理的标准输出、工作区文件以及发送给 mock 的下一个请求都会被检查。
- **`sandbox_rejects_escape`**: 对 `"../etc/passwd"` 的 `read_file` 向 LLM 返回一个沙盒错误。如果主机的密码文件泄漏到工具消息中，边界已经失效。
- **`unknown_tool_via_registry`**: 注册表不认识的工具名称变成一个 LLM 可以阅读和推理的工具观察结果。

`make test-cunit` 在没有 LLM 参与的情况下对注册表和沙盒进行单元级测试。如果注册表返回错误的指针或沙盒接受空路径，这些会在任何端到端测试之前失败。

---

## 3. 阶段 B — 调度工具批次

一批工具调用有两种顺序。

第一种是**请求顺序**：`tool_calls[i]` 必须产生 `out_msgs[i]`。这是与 LLM 协议的一部分，无论工具内部如何执行都必须保持。

第二种是**完成顺序**：当独立的工作并发运行时，第一个完成的工具可能不是请求中的第一个工具。执行器可以在内部利用这一点，但完成顺序必须保留在执行器内部。

> **提示：完成顺序和提交顺序**
>
> 并发工作可能以任何顺序完成，但外部可见的状态通常需要不同的顺序。CPU 可能乱序执行但顺序退休；存储系统可能重新排序内部工作同时保留一致性点。执行器有同样的义务：完成顺序是一种内部优化，消息顺序是外部契约。

### 调度规则

执行器需要少量的语义信息来决定何时重叠是安全的。对于本实验，有用的区分是**只读 vs 状态更改**。

当批次有 N ≥ 2 个工具调用，并且批次中的每个工具都已注册且为 `read_only` 时，执行器并发地调度它们并等待所有完成。总挂钟时间应趋向于最长的单个调用时间，而不是所有调用时间之和，这受调度开销的影响。

当批次中有任何工具缺失、未知或状态更改时，执行器回退到串行执行。这就是整个调度策略。

> **提示：保守的并发**
>
> 更多的并行性不自动代表更好的设计。一个试图证明每对操作独立的调度器可能比它加速的工作更难信任。本实验中的规则可以用一句话概括，是可测试的，也是可审计的。让常见的安全情况变快；保持危险情况简单。

### 实现并发路径

打开 `tools/executor.c` 并阅读现有的串行循环和 `ToolTask` 结构。策略决策很小；正确性工作在于在任务重叠时保持执行器契约。

`ToolTask` 数组已经为每个工具调用准备了一个槽位。每个任务都有一个索引、一个定义指针和一个结果字段。这些槽位是串行路径和并发路径之间的契约：无论任务如何执行，`tasks[i].result` 是工具调用 `i` 的结果所在的位置。

预期的架构是重用你实验 3 风格的线程池作为运行时组件。如果你选择那条路线，请将你的实现所需的源文件和包含标志添加到构建中。直接的 `pthread_create` / `pthread_join` 实现可以满足执行器契约，但不会获得满分；最终分数通过源代码审查来区分这些设计。

一些使这工作的不变量：

1.  **每个任务拥有自己的槽位。** 工作者 `i` 只写入 `tasks[i].result`。主线程仅在所有工作者都完成后才读取所有槽位。

2.  **工具没有共享的可变状态。** `read_file` 打开自己的文件句柄，分配自己的缓冲区，返回一个新的 `ToolResult`。

### 阶段 B 测试检查的内容

`tests/test_part2_b.py`:

- **`three_parallel_reads_in_order`**: 一个响应中的三个 `read_file` 调用。无论完成顺序如何，内容都按请求顺序返回给 LLM。
- **`mixed_read_write_serial_fallback`**: 一个结合了读取和写入的响应。所有操作均成功，写入的文件存在于磁盘上，消息保持请求顺序。
- **`eight_parallel_reads_complete`**: 8 调用批次。捕获"忘记一个工作者"的错误。
- **`parallel_speedup_observed`**: 粗略的时间检查。对一个 32 KB 文件的八次读取应该花费少于单次读取时间的 4 倍。这个界限很宽松；如果调度是静默串行的，这仍然会失败。

`make test-cunit-parallel` 和 `make test-cunit-tsan` 分别在 ASan 和 TSan 下测试执行器。

---

## 4. 构建和测试

```bash
make                  # 默认构建 → build/c-agent
make test-a           # 阶段 A 集成测试
make test-b           # 阶段 B 集成测试（需要先完成阶段 A）
make test             # 所有阶段（包括第一部分测试，如果存在）
make asan             # ASan + UBSan 构建
make test-asan        # ASan 下的所有测试
make tsan             # ThreadSanitizer 构建
make test-tsan        # TSan 下的 C 级执行器测试

make test-cunit            # 纯 C 单元测试（注册表、沙盒）
make test-cunit-parallel   # 并行执行器单元测试的 ASan 运行
make test-cunit-tsan       # 并行执行器单元测试的 TSan 运行
make clean
```

Mock 服务器 (`tests/mock_server.py`) 的工作方式与第一部分相同。

### 手动实验

要针对真实的 LLM 驱动代理（Caddy 代理在 `:18080`，你的 `API_KEY` 已导出）：

```bash
mkdir -p /tmp/agent-scratch && cd /tmp/agent-scratch
/path/to/build/c-agent
> 读取 README.md 并修改第 12 行的错字以修复它
```

如果 LLM 返回一个混合批次，例如 `read_file` 后跟 `edit_file`，执行器应保持该批次串行。如果它在同一个助手消息中返回多个 `read_file` 调用，它们的旋转指示器应同时出现。

---

## 5. 接下来的内容

第三部分引入了上下文窗口管理：当消息列表增长超过 LLM 的输入预算时，代理必须决定丢弃什么、总结什么以及保留什么。你在此处构建的注册表将成为第三部分推理的基础。

你集成的并发路径将保持稳定；并行性需求将会增长。当代理运行诸如"读取项目中的每个 C 文件"之类的任务时，它会发出十或二十个调用的批次，"全部并行"和"一次一个"之间的区别将在用户体验中变得可见。同样的问题将再次出现：哪些信息属于模型的上下文，哪些属于运行时的状态？

> 模型可以请求工作。运行时决定什么是安全的、有界限的、有序的和可观察的。


# Project 2 Part 2: Tool Registry & Parallel Execution

Your Part 1 agent already works. The goal of Part 2 is to evolve the system
while keeping its external behavior intact。

## The system problem

Part 1 has two design pressures.

First, tool knowledge is spread across the system. The agent loop knows how
to dispatch a concrete tool; the LLM client knows how to describe that tool
on the wire; the tool header exposes implementation facts that only some
modules should need. This is manageable for one tool and brittle for four
and more.

Second, the executor treats a batch of tool calls as a flat list. Three
independent reads are serialized even though one read depends on nothing
from another. The fix for this requires more than running everything in
parallel: writes, edits, unknown tools, and message ordering all still
matter.

By the end of this part, the system should have one path for tool discovery,
one path for tool dispatch, and one executor contract that can exploit
read-only parallelism while preserving what the LLM sees.

> **Tips: stable interface, replaceable implementation**
>
> Part 2 changes how the agent is built internally. The external contract —
> talk to the LLM, run tools, push results into history — stays the same.
> This is the same kind of separation an OS tries to maintain between a
> stable external interface and kernel-internal implementation choices.

---

## 1. Starting point

Start from your working Part 1 tree. The Part 2 package provides new
framework pieces and tests. Copy the files into your Part 1 tree. After that,
the project may fail to build. That is expected. The new framework code refers
to interfaces that your Part 1 code has not yet learned.

The full build and test targets are listed at the end. During Phase A,
expect `make test-a` and `make test-cunit` to become meaningful first;
during Phase B, move to `make test-b` and the TSan executor tests.

The package provides:

- **Framework code**: `tools/registry.c`, `tools/executor.{h,c}`,
  `tools/sandbox.{h,c}`
- **Tests**: `tests/test_part2_a.py`, `tests/test_part2_b.py`,
  `tests/cunit/{test_registry,test_sandbox,test_executor}.c`, and an
  updated test harness (`harness.py`, `run_tests.py`). The updated harness
  is backward compatible: your Part 1 tests continue to work.
- **Build configuration**: `Makefile` with all Part 2 targets. This is the
  grading build file. If your Part 1 Makefile has local fixes, merge them
  into this one.

---

## 2. Phase A — One Interface for Many Tools

The external behavior stays the same. What changes is ownership.

After this phase, the agent loop should be independent of which tools exist.
The LLM client should discover tools from the registry. A concrete tool
should own its runtime implementation and the metadata needed to advertise
it.

### The interface

Your Part 1 `tools/tools.h` is one of the boundaries that must evolve. At
the start of Phase A, the framework needs the following common tool
interface:

```c
typedef struct {
    bool ok;
    char *output;        /* heap-allocated; freed by tool_result_free */
} ToolResult;

void tool_result_free(ToolResult *r);

#define MAX_TOOL_OUTPUT 50000

typedef ToolResult (*ToolFn)(cJSON *args);

typedef struct {
    const char *name;
    const char *desc;
    const char *param_schema;
    ToolFn exec;
} ToolDef;

#define MAX_REGISTERED_TOOLS 16

void tools_init(void);
void tool_register(ToolDef *def);
ToolDef *tool_find(const char *name);
ToolDef *const *tool_list(int *out_count);
```

Before Phase A is complete, you will extend this same struct with one scheduling field.

These types are the contract between your tools, the registry, and the
executor. `ToolDef` bundles everything the system needs to know about a
tool: its wire name, the description the LLM reads, the JSON schema for its
arguments, and the function that runs it. `ToolResult` is a small value type
so every return path in every tool must construct one explicitly.

> **Tips: dispatch tables**
>
> A registry is a dispatch table. The caller does not need one branch per
> implementation. Syscall tables, VFS operation tables, and device driver
> interfaces all use this shape: keep the caller stable while concrete
> operations vary.

### Read the provided boundaries

Start by reading the provided framework files. They define the contracts
that the implementation and tests agree on.

**`tools/registry.c`** implements the registry. It stores `ToolDef` pointers
in a flat array, populated once at startup. Read it to understand how the
system turns a registered definition into something the executor can find
later.

**`tools/executor.h`** defines `executor_run_tools` — the function the agent
calls to run a batch of tool calls. Read the contract comment: it specifies
what the executor promises about ordering and how execution failures are
reported.

**`tools/executor.c`** implements that contract. In Phase A, it runs tools
serially in request order. Read the `ToolTask` struct, the `run_one`
function, and the result-message construction loop — these are the paths
your file tools will travel through.

**`tools/sandbox.h`** is the workspace containment boundary.
`resolve_workspace_path` canonicalizes a relative path and rejects anything
that resolves outside the workspace directory.

> **Tips: protection boundary**
>
> The sandbox is the boundary between LLM-supplied paths and the filesystem.
> A request from a higher layer is a request; the runtime owns enforcement.
> If each tool reimplements path validation, the boundary has leaked.
> In a real system, the sandbox would be more complex and robust.

### Wrapping bash as a ToolDef

Your `tools/bash.c` has a working fork/exec implementation. Wrap it
in a `ToolDef` so the registry can find it through a uniform interface.

```c
ToolDef bash_def = {
    .name = "bash",
    .desc = "Run a shell command and return its combined stdout/stderr.",
    .param_schema = "{\"type\":\"object\","
                    "\"properties\":{\"command\":{\"type\":\"string\","
                    "\"description\":\"The shell command to execute\"}},"
                    "\"required\":[\"command\"]}",
    .exec = tool_bash,
};
```

Your file tools follow the same shape: one `ToolDef` per tool, with the
implementation function reached through the `.exec` pointer.

### File tool

Create `tools/read.c`, `tools/write.c`, and `tools/edit.c`. Read
`tools/registry.c` to see what symbols the registry expects to link against.

Every file tool must validate its `path` argument through
`resolve_workspace_path` before opening anything. The sandbox check is the boundary
that prevents a path like `read_file("../../etc/passwd")` from becoming a real disclosure.

The three tools:

- **`read_file`** — argument `{"path": "...", "limit"?: N}`. Returns the
  workspace-relative file's contents (UTF-8). On a `limit`, truncate
  after that many lines. Cap at `MAX_TOOL_OUTPUT` regardless. On failure
  (missing file, sandbox rejection, …), return `ok=false` with a short,
  LLM-readable message. This is read-only with respect to workspace state.

- **`write_file`** — argument `{"path": "...", "content": "..."}`. Replace
  the file's contents with `content`. The parent directory must already
  exist; tools do not `mkdir -p`. Return a message that names the file
  and the byte count on success.

- **`edit_file`** — argument
  `{"path": "...", "old_text": "...", "new_text": "..."}`. Replace the
  first exact occurrence of `old_text` with `new_text` in the file. If
  there is no match, fail. The contract is "first match".

The schemas are provided below.

`read_file`:

```json
{"type":"object","properties":{"path":{"type":"string","description":"Relative path inside the workspace"},"limit":{"type":"integer","description":"Optional maximum number of lines to return"}},"required":["path"]}
```

`write_file`:

```json
{"type":"object","properties":{"path":{"type":"string","description":"Relative path inside the workspace"},"content":{"type":"string","description":"Full file contents to write"}},"required":["path","content"]}
```

`edit_file`:

```json
{"type":"object","properties":{"path":{"type":"string","description":"Relative path inside the workspace"},"old_text":{"type":"string","description":"Exact substring to find"},"new_text":{"type":"string","description":"Replacement text"}},"required":["path","old_text","new_text"]}
```

### Connecting the system

Once your tools compile and export their `ToolDef` symbols, three things
still need to happen for the system to work end-to-end.

The registry must know about the new tools. `tools/registry.c` is where
every tool enters the system — read it and register yours. The registry
must be populated before the first LLM request asks which tools exist; this
is a boot-order dependency in `main.c`.

> **Tips: boot order**
>
> Subsystems have initialization dependencies. A filesystem cannot mount
> before its driver is registered; a device cannot be opened before the
> device table knows about it. Here, the LLM client cannot advertise tools
> before the registry has been populated.

The LLM client should discover tool schemas from the registry. The wire
format still expects one function entry per tool, with the schema inserted
as raw JSON. Walk `tool_list()` in your `build_tools_json` and emit one
entry per `ToolDef`.

After the assistant message has been appended to history, the agent should
hand the complete batch of tool calls to the executor and push the returned
tool messages into history in request order. Read the `executor_run_tools`
contract in `executor.h` to see what the agent passes in and what it gets
back.

A useful way to check your design:

- When adding another tool later, which existing files would need to change?
- If the answer includes the agent loop, which decision is still in the
  wrong module?
- If the answer includes the LLM client, where should tool metadata live
  instead?
- If a tool is unknown at runtime, which layer should turn that into an
  observation rather than a crash?

Avoid adding another `if (strcmp(name, ...))` path in the agent loop. That
may pass one tool test, but it leaves the original design pressure in place.

### Extend `ToolDef` with `read_only`

Before finishing Phase A, extend the tool interface with scheduling
metadata. Phase B needs the executor to distinguish tools that only observe
state from tools that may change state. Add one boolean field to `ToolDef`:

```c
bool read_only;
```

Use designated initializers (`.read_only = true`) in every `ToolDef`
literal so field order does not become a hidden dependency. `read_file` is
the only read-only tool in this system. `write_file` and `edit_file` change
workspace state. `bash` is treated as state-changing because the runtime
cannot know what an arbitrary shell command will do.

> **Tips: interface metadata**
>
> An interface carries more than a function pointer. It can carry metadata
> that changes what the runtime is allowed to do. File modes, page
> permissions, process states, and device capabilities all influence
> scheduling or protection decisions. Here, `read_only` is small metadata
> with runtime consequences.

### What Phase A tests check

`tests/test_part2_a.py`:

- **`tools_advertised`**: the wire-format request to the mock contains the
  four tools `bash`, `edit_file`, `read_file`, and `write_file`. If the LLM
  client still returns an empty tool list, this catches it immediately.
- **`bash_through_registry`**: the Part 1 bash tool still works through the
  new dispatch path.
- **`read_file`**, **`write_file`**, **`edit_file`**: each exercises one
  tool against a temporary workspace. The agent's stdout, the workspace
  file, and the next request to the mock are all checked.
- **`sandbox_rejects_escape`**: a `read_file` for `"../etc/passwd"` returns
  a sandbox error to the LLM. If the host's password file leaks into the
  tool message, the boundary has failed.
- **`unknown_tool_via_registry`**: a tool name the registry does not
  recognize becomes a tool observation that the LLM can read and reason
  about.

`make test-cunit` exercises the registry and sandbox at the unit level with
no LLM involvement. If the registry returns the wrong pointer or the
sandbox accepts an empty path, these fail before any end-to-end test does.

---

## 3. Phase B — Scheduling Tool Batches

A batch of tool calls has two orders.

The first is **request order**: `tool_calls[i]` must produce `out_msgs[i]`.
This is part of the protocol with the LLM and must hold regardless of how
tools execute internally.

The second is **completion order**: when independent work runs concurrently,
the first tool to finish may not be the first tool in the request. The
executor may exploit that internally, but completion order must stay
internal to the executor.

> **Tips: completion order and commit order**
>
> Concurrent work may finish in any order, but externally visible state
> often needs a different order. CPUs may execute out of order but retire
> in order; storage systems may reorder internal work while preserving
> consistency points. The executor has the same obligation: completion
> order is an internal optimization, message order is an external contract.

### The scheduling rule

The executor needs a small amount of semantic information to decide when
overlap is safe. For this lab, the useful distinction is **read-only vs
state-changing**.

When the batch has N ≥ 2 tool calls and every tool in the batch is
registered and `read_only`, the executor dispatches them concurrently and
waits for all to complete. The total wall time should move toward the
longest single call rather than the sum of all calls, subject to scheduling
overhead.

When any tool in the batch is missing, unknown, or state-changing, the
executor falls back to serial execution. This is the entire scheduling
policy.

> **Tips: conservative concurrency**
>
> More parallelism is not automatically a better design. A scheduler that
> tries to prove every pair of operations independent can become harder to
> trust than the work it speeds up. The rule in this lab fits in one
> sentence, is testable, and is auditable. Make the common safe case fast;
> keep the dangerous case simple.

### Implementing the concurrent path

Open `tools/executor.c` and read the existing serial loop and the `ToolTask`
structure. The policy decision is small; the correctness work is in
preserving the executor contract while tasks overlap.

The `ToolTask` array already has one slot per tool call. Each task has an
index, a definition pointer, and a result field. These slots are the
contract between the serial and concurrent paths: regardless of how tasks
execute, `tasks[i].result` is where the result for tool call `i` lands.

The intended architecture is to reuse your Lab 3-style thread pool as a
runtime component. If you choose that route, add the source and include flags
needed by your implementation to the build. A direct `pthread_create` /
`pthread_join` implementation can satisfy the executor contract, but it will
not earn full scores; the final score distinguishes these designs by source
review.

A few invariants that make this work:

1. **Each task owns its slot.** Worker `i` writes only `tasks[i].result`.
   The main thread reads all slots only after every worker has completed.

2. **Tools have no shared mutable state.** `read_file` opens its own file
   handle, allocates its own buffer, returns a fresh `ToolResult`.

### What Phase B tests check

`tests/test_part2_b.py`:

- **`three_parallel_reads_in_order`**: three `read_file` calls in one
  response. Contents go back to the LLM in request order regardless of
  completion order.
- **`mixed_read_write_serial_fallback`**: a response combining reads and a
  write. All succeed, the written file exists on disk, and messages remain
  in request order.
- **`eight_parallel_reads_complete`**: 8-call batch. Catches "forgot one
  worker" bugs.
- **`parallel_speedup_observed`**: coarse timing check. Eight reads of a
  32 KB file should take less than 4× a single read. The bound is generous;
  if scheduling is silently serial, this still fails.

`make test-cunit-parallel` and `make test-cunit-tsan` exercise the executor
under ASan and TSan respectively.

---

## 4. Build and Test

```bash
make                  # default build → build/c-agent
make test-a           # Phase A integration tests
make test-b           # Phase B integration tests (needs Phase A done first)
make test             # all phases (including Part 1 tests if present)
make asan             # ASan + UBSan build
make test-asan        # all tests under ASan
make tsan             # ThreadSanitizer build
make test-tsan        # C-level executor test under TSan

make test-cunit            # pure-C unit tests (registry, sandbox)
make test-cunit-parallel   # ASan run of the parallel executor unit test
make test-cunit-tsan       # TSan run of the parallel executor unit test
make clean
```

The mock server (`tests/mock_server.py`) works the same as Part 1.

### Manual experimentation

To drive the agent against a real LLM (Caddy proxy on `:18080`, your
`API_KEY` exported):

```bash
mkdir -p /tmp/agent-scratch && cd /tmp/agent-scratch
/path/to/build/c-agent
> read README.md and edit_file the typo on line 12 to fix it
```

If the LLM returns a mixed batch such as `read_file` followed by
`edit_file`, the executor should keep that batch serial. If it returns
several `read_file` calls in the same assistant message, their spinners
should appear simultaneously.

---

## 5. What Comes Next

Part 3 introduces context-window management: when the message list grows
past the LLM's input budget, the agent has to decide what to drop, what to
summarize, and what to keep. The registry you built here will be the
catalogue Part 3 reasons over.

The concurrency path you integrated will stay stable; the parallelism
appetite will grow. As the agent runs tasks like "read every C file in the
project", it issues ten- or twenty-call batches, and the difference between
"all in parallel" and "one at a time" becomes visible in the user
experience. The same question will return: which information belongs in the
model's context, and which belongs in the runtime's state?

> The model may request work. The runtime decides what is safe, bounded,
> ordered, and observable.

