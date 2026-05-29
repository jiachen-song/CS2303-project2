# 项目2 第3部分：上下文管理

当代理执行长任务（遍历目录树、读取许多文件、进行长时间对话）时，它积累的扁平消息列表很快就会成为负担。每一轮都会增加用户提示、模型响应、工具调用和工具结果。如果不加以管理，会产生三种压力：

1.  **窗口限制。** 一旦总令牌数超过模型的上下文窗口，下一个请求将被拒绝，对话失败。
2.  **成本。** 令牌是计费的。单调增长的历史记录会强制每一轮重新传输旧内容，导致成本增加。
3.  **质量。** 注意力是有限的资源。冗长、杂乱的历史记录很少能像在紧凑、近期窗口内执行相同任务那样产生好的答案。

本部分引入了一个显式的**上下文**子系统，将代理的消息历史保持在可管理的预算内。

---

## 1. 上下文接口

公共接口位于 `context/context.h` 中。每个策略都有一个名称、一个决定何时应应用它的条件，以及一个回收上下文空间的操作。在任何模型请求之前，代理会调用 `ctx_reclaim`，该函数按注册顺序遍历已注册的策略。

在代理内部实现（`agent/agent.c`）中，原生的消息列表被 `Context` 指针替换。对历史记录的所有添加都通过 `ctx_push` 路由。每次调用模型之前，都会先成功执行 `ctx_reclaim`，并从 `ctx_history(ctx)` 获取最新的历史记录。

三个环境变量配置行为：

- `CONTEXT_WINDOW` – 模型的总令牌预算。
- `OFFLOAD_THRESHOLD` – 触发卸载策略的窗口比例。
- `SUMMARY_THRESHOLD` – 触发摘要策略的窗口比例。

---

## 2. 卸载策略

长的工具输出体积庞大，但可以无损恢复：完整的内容已经存在于磁盘上，或者可以通过重新运行工具来重现。因此，卸载策略将内容体移出对话，并留下一个紧凑的指针，就像一个带有恢复提示的预览。这对于那些工具有大量输出，但代理很少需要完整内容，又必须能够按需获取的工作负载来说是理想的。

**触发条件。** 当当前令牌使用量（通过 `ctx_budget_usage(ctx)` 测量）超过上下文窗口的 `OFFLOAD_THRESHOLD` 比例时，策略被激活。

**操作。** 它检查位于最近 `KEEP_RECENT_MSGS` 条消息之外的工具角色消息。对于每个内容足够长的符合条件的消息，原始内容会被保存到 `.agent/offload/` 目录下的磁盘中。消息体被替换为一个包含子字符串 `"read_file"` 和 `".agent/offload/"` 的简短占位符，这表示可以使用 `read_file` 工具在给定路径检索完整内容。

---

## 3. 摘要策略

当对话的大部分内容来自许多来回的对话轮次而非工具输出时，就需要一种有损策略。摘要策略让模型自己将旧的历史记录压缩成一条单独的交接消息，只保留基本的事实、决策和结果。这种方法适用于需要保留上下文线索但可以丢弃逐字细节的对话。

**触发条件。** 当令牌使用量超过窗口的 `SUMMARY_THRESHOLD` 比例时，策略被激活。但是，如果总消息数不大于 `KEEP_RECENT_MSGS`，策略不执行任何操作，**且不得**调用模型。

**操作。** 否则，该策略会使用 `g_config.model` 精确发起一次 LLM 请求。请求发送应该被折叠的消息前缀，并可选地附带一个用于增量合并的现有摘要。

成功后，所有在最近的 `KEEP_RECENT_MSGS` 条消息之前的消息，会被替换为一条角色为 `user` 的消息，其内容是模型的摘要。在消息体上加一个简短的头部是可以接受的。最近的消息会逐字保留。

---

## 4. 策略协作

这两个策略旨在协同工作，并且顺序很重要。卸载策略首先运行，因为它是一种无损、低成本的操作，可以削减工具结果中的体积，而不改变对话的语义。摘要策略其次运行，处理对话流本身中剩余的冗长部分。

由于每个策略都独立地根据相同的令牌预算检查自己的阈值，一次 `ctx_reclaim` 过程可能会触发两者。系统保证卸载步骤会在摘要评估预算之前减少令牌计数，这有助于避免不必要的压缩。这种分阶段设计保持了框架的开放性：以后只需通过定义良好的优先级注册它们，就可以添加额外的策略，而代理核心保持不变。

---

## 5. 构建与测试

编译项目并运行单元测试：

```bash
make test-cunit-offload
make test-ca
make test-cunit-summary
make test-cb
```

---

## 6. 扩展

你需要选择至少一个扩展（完成更多作为加分项）来完成，并在报告中说明你完成了哪些扩展。

### 6.1 评估

构建代理之后，下一步自然是看看它能做什么。设计几个端到端测试代理的场景并观察其行为。例如，递归查找目录树中的所有 `.c` 文件，探索一个不熟悉的代码库并回答关于它的特定问题，或者执行精确的多文件文本替换而不损坏无关代码。值得跟踪的指标：每个场景的成功率、每个任务所需的工具调用轮数、提示和完成令牌的消耗量。这些数字可以让你具体了解代理的行为。

### 6.2 子代理

当一个代理同时处理几个独立的事情（读取代码、搜索文档、运行构建）时，这些线程会竞争同一个上下文窗口。模型的注意力被分散了，更关键的是，每个子任务的中间输出都会消耗共享预算中的令牌。将独立的工作卸载给子代理可以保持主上下文的精简。设计工具，使主代理能够为有边界的子任务生成子代理。每个子代理都有自己的上下文窗口，执行任务，并返回一个结构化的结果。主代理接收结果并继续执行。

### 6.3 会话

崩溃或关闭终端会清除所有内存中的对话状态。其思路是将对话的仅追加日志写入稳定存储。启动时，代理回放日志以重建历史记录。中断的会话可以从停止的地方恢复。实现此机制需要几个命令。

### 6.4 记忆

上下文管理处理单个会话内的历史记录。但代理产生的知识超出了任何单个会话的生命周期：项目结构、构建约定、编码风格、过去对话中的决策。项目级别的记忆为代理提供了连续性。它了解在这个代码库中什么是重要的，可以保存到 `.agent` 文件夹，并在未来的会话中加载。设计工具，让代理可以向记忆存储写入和读取。

### 6.5 技能

通用代理可以做任何事情，但什么都不精通。当被要求审查代码时，它应该知道检查什么、按什么顺序检查以及如何格式化输出，而无需用户每次都详细说明。技能是一种提示，引导模型完成特定类型的任务，告诉它要遵循哪些步骤，要寻找什么，以及产生什么样的输出形状。挑战在于令牌成本。渐进式披露可以降低此开销：在启动时，只注入技能名称和一行描述。当 LLM 识别出相关技能时，它会调用一个工具，按需加载完整的提示。
# Project 2 Part 3: Context Management

When an agent runs a long task (walking a directory tree, reading many files, engaging in extended conversations) the flat list of messages it accumulates quickly becomes a burden. Every turn adds the user prompt, the model response, tool calls, and tool results. Without management, three pressures accumulate:

1. **Window limits.** Once the total token count exceeds the model’s context window, the next request is rejected and the conversation fails.
2. **Cost.** Tokens are billed. A monotonically growing history forces every turn to retransmit old content at increasing expense.
3. **Quality.** Attention is a finite resource. A long, cluttered history rarely produces answers as good as the same task framed within a tight, recent window.

This part introduces an explicit **Context** subsystem that keeps the agent’s message history within a manageable budget.

---

## 1. The Context Interface

The public surface lives in `context/context.h`. Each policy carries a name, a condition that decides when it should apply, and an action that reclaims context space. Before any model request the agent invokes `ctx_reclaim`, which walks the registered policies in registration order.

Inside the agent implementation (`agent/agent.c`), the native message list is replaced by a `Context` pointer. Every addition to the history routes through `ctx_push`. Every call to the model is preceded by a successful `ctx_reclaim` and receives the up‑to‑date history from `ctx_history(ctx)`.

Three environment variables configure the behaviour:

- `CONTEXT_WINDOW` – the model’s total token budget.
- `OFFLOAD_THRESHOLD` – the fraction of the window that triggers the offload policy.
- `SUMMARY_THRESHOLD` – the fraction of the window that triggers the summary policy.

---

## 2. Offload Policy

Long tool outputs are bulky but losslessly recoverable: the full payload already lives on disk or can be reproduced by rerunning the tool. The offload policy therefore moves the body out of the conversation and leaves behind a compact pointer, much like a preview with a recovery hint. This is ideal for workloads with large tool results where the agent rarely needs the full content but must be able to fetch it on demand.

**Trigger.** The policy activates when the current token usage, measured by `ctx_budget_usage(ctx)`, exceeds the `OFFLOAD_THRESHOLD` fraction of the context window.

**Action.** It inspects tool‑role messages that lie outside the most recent `KEEP_RECENT_MSGS` messages. For each qualifying message whose body is sufficiently long, the original content is saved to disk under `.agent/offload/`. The message body is replaced with a short placeholder that contains the substrings `"read_file"` and `".agent/offload/"`, signalling that the full payload can be retrieved with the `read_file` tool at the given path.

---

## 3. Summary Policy

When the bulk of the conversation stems from many back‑and‑forth turns rather than tool output, a lossy strategy becomes necessary. The summary policy asks the model itself to compress old history into a single handoff message, retaining only the essential facts, decisions, and outcomes. This approach works well for dialogues that need to preserve a thread of context but can discard verbatim details.

**Trigger.** The policy activates when the token usage exceeds the `SUMMARY_THRESHOLD` fraction of the window. However, if the total message count is not greater than `KEEP_RECENT_MSGS`, the policy does nothing and **must not** call the model.

**Action.** Otherwise the policy makes exactly one LLM request using `g_config.model`. The request sends the prefix of messages that should be collapsed, optionally together with a running summary for an incremental merge.

On success, all messages before the most recent `KEEP_RECENT_MSGS` are replaced by a single message whose role is `user` and whose content is the model’s summary. A short header on the body is acceptable. The most recent messages are preserved verbatim.

---

## 4. Policy Collaboration

The two policies are intended to work together, and the order matters. The offload policy runs first because it is a lossless, inexpensive operation that trims bulk from tool results without altering the semantics of the conversation. The summary policy runs second, handling the remaining verbosity in the dialogue flow itself.

Because each policy independently checks its own threshold against the same token budget, a single `ctx_reclaim` pass may trigger both. The system guarantees that the offload step reduces the token count before summary evaluates the budget, which helps avoid unnecessary compression. This staged design keeps the framework open: additional policies can be added later simply by registering them with a well‑defined priority, and the agent core remains unchanged.

---

## 5. Build and Test

Compile the project and run the unit tests:

```bash
make test-cunit-offload
make test-ca
make test-cunit-summary
make test-cb
```

---

## 6. Extension

You need to select at least one extension (more as a bonus) to complete, and specify in the report which ones you have completed.

### 6.1 Evaluation

After building an agent, the natural next step is to see what it can do. Design a few scenarios that exercise your agent end-to-end and observe its behavior. For example, recursively find all `.c` files in a directory tree, explore an unfamiliar codebase and answer a specific question about it or perform a precise multi-file text replacement without damaging unrelated code. Metrics worth tracking: success rate per scenario, rounds of tool calls per task, prompt and completion token consumption. The numbers give you a concrete picture of how your agent behaves.

### 6.2 SubAgent

When a single agent handles several independent things at once (reading code, searching docs, running a build), those threads compete for the same context window. The model's attention is divided, and more critically, every subtask's intermediate output consumes tokens from the shared budget. Offloading independent work to child agents keeps the main context lean. Design tools that enable the main agent to spawn child agents for bounded subtasks. Each child gets its own context window, executes, and returns a structured result. The parent receives the result and continues.

### 6.3 Session

A crash or a closed terminal wipes all in-memory conversation state. The idea is an append‑only log of the conversation written to stable storage. On startup the agent replays the log to rebuild history. Interrupted sessions resume where they left off. Several commands are needed to implement this mechanism.

### 6.4 Memory

Context management handles history within a session. But the agent also produces knowledge that outlives any single session: project structure, build conventions, coding style, decisions from past conversations. A project-level memory gives the agent continuity. It learns what matters in this codebase, and can be saved to `.agent` folder and loaded in future sessions. Design tools that let the agent write to and read from the memory store.

### 6.5 Skill

A general-purpose agent can do anything but excels at nothing. When asked to review code, it should know what to check, in what order, and how to format the output, without the user spelling it out each time. A skill is a prompt that guides the model through a specific kind of task, telling it what steps to follow, what to look for, and what output shape to produce. The challenge is token cost. Progressive disclosure reduces this overhead: at startup, inject only skill names and one-line descriptions. When the LLM identifies a relevant skill, it calls a tool to load the full prompt on demand.
