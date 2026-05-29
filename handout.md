# Project 2 (Part 1): A Basic AI Agent

You already have the three mechanisms that a coding agent depends on: a
shell-style fork/exec pattern for running child programs, an HTTP client for
talking to a remote service, and a thread pool for running work
concurrently. What none of these pieces can do, on its own, is decide what
to do next.

This project changes that. You will wire the pieces together and give the
resulting program a _decider_ — a remote LLM that, turn by turn, chooses
the next tool to run and the next thing to say. When the LLM asks for a
shell command, your program forks a child, runs it, captures the output,
and sends the output back. When the LLM has no more tools to run, your
program prints the final answer.

The end product is a coding assistant that lives in your terminal. It reads
your request, runs real commands against real files, and reports what it found.
It is also the substrate for Parts 2 and 3 (which add a tool registry,
parallel execution, and context management), so the structural decisions
you make here will be lived with.

---

## 1. What You Are Building

| Phase | New capability                          | Core loop shape                                              |
| ----- | --------------------------------------- | ------------------------------------------------------------ |
| A     | One user prompt → one tool call → reply | `read → ask LLM → run tool → ask LLM → print`                |
| B     | Multiple tool calls per prompt (ReAct)  | wrap Phase A in a `while` that breaks when the LLM stops     |
| C     | Multi-turn dialogue + live TUI          | outer REPL over user inputs, spinner + per-tool status lines |

---

## 2. Before You Start

### 2.1 Directory layout

```
project2/
├── handout.md
├── Makefile
├── main.c
├── config.{h,c}
├── http.{h,c}
├── message.{h,c}
├── util.{h,c}
├── agent/
│   ├── agent.{h,c}
│   └── llm_client.{h,c}
├── tools/
│   ├── bash.c
│   └── tools.h
├── ui/
│   ├── ui.c
│   ├── ui.h
│   ├── render.c
│   └── internal.h
├── libs/
│   └── cJSON.{h,c}
└── tests/
    ├── mock_server.py
    ├── run_tests.py
    ├── harness.py
    ├── test_phase_a.py
    ├── test_phase_b.py
    └── test_phase_c.py
```

Working boundaries:

- You primarily edit `agent/`, `tools/bash.c`, and `main.c`.
- `ui/`, `libs/`, and `tests/` are given. Read them; do not rewrite them.
- The `Makefile` auto-discovers `.c` files under `agent/`, `tools/`, and
  `ui/` via wildcard — adding a new source file in a later phase just
  works.

### 2.2 Build and test

```bash
make               # default binary: build/c-agent
make test          # runs all phase tests
make test-a        # just Phase A
make test-b
make test-c
make asan          # AddressSanitizer build (build/c-agent-asan), no tests
make test-asan     # run all tests under ASan
make clean
```

Tests always build first, so `make test-a` alone is enough during daily
work.

### 2.3 The mock LLM server

`tests/mock_server.py` is a tiny Python `http.server` that pretends to be
the LLM API. You never have to run it yourself — the test harness spawns a
fresh instance per test with a scripted deck of responses.

### 2.4 Running against the real LLM

For manual experimentation you can point the agent at a real proxy: run
`caddy reverse-proxy --from :18080 --to https://models.sjtu.edu.cn`,
export your `API_KEY`, and set `LLM_HOST=127.0.0.1 LLM_PORT=18080`.

### 2.5 Testing your agent by actually using it

The phase tests tell you your wire protocol is right. They do not tell
you whether the agent is _useful_. You can try it:

```bash
mkdir -p /tmp/agent-scratch && cd /tmp/agent-scratch
export API_KEY=...   # see 2.4
/path/to/build/c-agent
> write a C program that prints the first 20 primes to primes.c, then compile and run it
```

A working Phase A agent will `bash` out a few commands — write the file,
invoke `gcc`, run the binary — and report the output. It is the fastest
way to notice problems the mock cannot catch:

- the LLM's tool call reaches your dispatch in a form you can actually
  parse (newlines, nested quotes, heredocs…)
- long tool outputs do not destabilize the next request body
- failed commands (compile errors) come back as signal for the LLM, not
  as silent successes

---

## 3. Phase A — Single-Turn Tool Call

### 3.1 The goal

Type one line, the agent uses `bash` to gather information, the agent answers.

```
$ ./build/c-agent
How many C files are in /etc?
14
```

Under the hood, five things had to happen:

```
user line ─┐
           ▼
   +----------------+      +-------------+      +-----------+
   |   agent.c      │ ───▶ │ llm_client  │ ───▶ │   LLM     │
   |  (orchestrate) │      │  build HTTP │      │           │
   +----------------+      +-------------+      +-----------+
           ▲                                            │
           │              (returns: tool_call bash)     │
           │                                            ▼
   +----------------+      +-------------+      +-----------+
   │   print(reply) │ ◀─── │  llm_chat   │ ◀─── │ bash.c    │
   │                │      │  (2nd call) │      │ fork/exec │
   +----------------+      +-------------+      +-----------+
```

### 3.2 What is given

- `http.{h,c}` — HTTP primitives: `tcp_connect`, `send_all`, `recv_all`,
  `http_parse_response`.
- `message.{h,c}` — a dynamic array of JSON-serialized messages and
  constructors for `user` / `tool` messages.
- `tools/tools.h` and `tools/bash.c` — schema globals for the `bash`
  tool, plus the child-side of `bash_tool_exec` (pipe + fork + dup2 +
  execl). You fill in the parent side.
- `main.c` — reads one line, calls `agent_chat`, prints the reply, exits.
- `libs/cJSON` — a minimal JSON library.

### 3.3 What you implement

Three places, each marked with a TODO in the source:

**`tools/bash.c` — the parent side of `bash_tool_exec`.**

After fork you own `pipefd[0]` (the read end) and `pid`. Turn that into a
`ToolResult`: decide how much output to keep, whether the command
succeeded, and what string the LLM sees on the next turn. A failed
command is still useful information — non-zero exit statuses and fatal
signals tell the LLM that its command did not do what it expected, so
include that in `.output` rather than swallowing it. See `waitpid(2)` and
the `WIFEXITED` / `WEXITSTATUS` / `WIFSIGNALED` / `WTERMSIG` macros in
`<sys/wait.h>`. Close `pipefd[0]` and reap the child before returning.

**`agent/llm_client.c` — `llm_chat`.**

1. Build the request body. It is a JSON object with these fields:
   `model`, `messages` (an array containing the system prompt plus the
   given message list), `tools` (an array with one entry describing
   `bash`), and `max_tokens`. Use `cJSON` to construct it — do not hand-
   splice strings.
2. Build the HTTP request, `tcp_connect`, `send_all(header)` then
   `send_all(body)`, `recv_all`, `http_parse_response`.
3. Reject non-200 status codes — write a message into `err` and return -1.
4. Parse the response body: `choices[0].message` is the assistant message.
   Read its `content` (may be empty or absent) and its `tool_calls` array.
   For each tool call, capture `id`, `function.name`, and
   `function.arguments` (a JSON _string_ on the wire; parse it back into
   an object with `cJSON_Parse`).
5. Keep a serialized copy of the whole assistant message (`raw_message`) —
   the agent will push it back into history verbatim so the LLM sees its
   own previous reply on the next call.

**`agent/agent.c` — `agent_chat`.**

1. Push a `user` message onto a local `MessageList`.
2. Call `llm_chat`. On failure, return NULL.
3. If the response has no tool calls, print and cache its `content` and
   return it. Done.
4. Otherwise: push `response.raw_message` onto history; execute the tool
   the LLM asked for; push the tool result as a `tool` message with the
   right `tool_call_id`; call `llm_chat` again; print and return the
   final content.

### 3.4 Edge cases that are yours to handle

- **No tool arguments:** the LLM can emit a tool call with an empty
  `arguments` string. `cJSON_Parse("")` returns NULL; treat that as
  `cJSON_CreateObject()`.
- **Failed shell commands:** a command that exits non-zero or dies to a
  signal is not an error in the agent — it is an observation the LLM
  needs to see so it can recover. Encode the exit status into the tool
  output rather than returning a generic "bash failed" message.
- **Unknown tool name:** the test suite will send a tool call the agent
  has never heard of. Your dispatch must reject it with a `ToolResult`
  whose `.output` names the unknown tool, so the LLM can see its mistake
  and try again, rather than the program aborting.

---

## 4. Phase B — The ReAct Loop

### 4.1 The goal

Phase A only lets the LLM take one action. Real tasks ("find the file that
defines `foo`, then count the lines in it") need a chain: read, think,
read again, think, answer. The pattern is called **ReAct** (Reason +
Act): the LLM reasons out loud, takes an action (tool call), observes the
result, reasons some more, acts again, and eventually returns a final
answer.

```
          ┌────────────────────┐
  user ──▶│     agent_chat     │
          │                    │
          │   ┌────────────┐   │
          │   │ llm_chat   │◀──┼──────────┐
          │   └────┬───────┘   │          │
          │        │           │          │
          │  n_tool_calls?     │          │
          │   ┌────┴────┐      │          │
          │  =0         >0     │          │
          │   │         │      │          │
          │   ▼         ▼      │          │
          │ print    run each ─┼─► push ──┘
          │          tool      │    tool
          │                    │    result
          └────────────────────┘
```

### 4.2 What changes

You edit the two files you already own:

- `agent/llm_client.{h,c}`: in Phase A you only had to parse one tool
  call per response. Now you have to parse all of them. Revisit how
  `LLMResponse` stores its `tool_calls` — a fixed-size array is fine if
  you pick a cap you can defend; a growing buffer is fine too. Either
  way, update `llm_chat` to parse every entry in request order.
- `agent/agent.c`: wrap the body of `agent_chat` in a loop. Terminate
  when the response has zero tool calls. Within one iteration, execute
  every tool call in the response and push their results in request
  order. This is also a good moment to look at the `struct Agent` you
  wrote in Phase A — the multi-iteration loop may want state you did
  not previously need.

### 4.3 Why "in request order"

If the LLM asks for calls `t1` and `t2` in a single response, the tool
messages must appear in history in that order even if the _execution_
order is different. In Phase B execution is sequential so this is
automatic; Part 2 will execute them in parallel and the ordering
invariant will become a real design constraint. Write the code now in a
way that does not assume serial execution, so you do not have to revisit
it later.

### 4.4 Termination as an invariant

What guarantees the loop ends? The LLM can, in theory, keep asking for
tools forever. Two lines of defense are normal:

1. A hard iteration cap (`MAX_TURNS`, say 20). If you exceed it, report
   an error and stop. This is how every production agent protects itself.
2. A sane system prompt that nudges the LLM toward finishing. You do not
   need to tune this now — the mock tests terminate by ending their
   scripts.

A failed tool execution is _still_ a valid tool result. Push the failure
message back into history and let the LLM react; do not abort the loop.

### 4.5 What the tests check

- Three-hop chain: mock scripts three successive `bash` calls, then a
  final text. Your agent must produce four requests to the mock and
  preserve every tool result in the growing history.
- Two tool calls in a _single_ LLM response: both execute, both come
  back in order.

---

## 5. Phase C — Multi-Turn Dialogue and the TUI

### 5.1 The goal

One shot is fine for a demo; a real agent holds a conversation. A user
asks something, gets an answer, asks a follow-up, gets a better answer
because the agent still remembers the first turn. At the same time we
finally start showing the user _what the agent is doing_ while it runs —
the spinner, the per-tool status line, the timing.

### 5.2 What you implement

**`main.c` — turn the single-shot driver into a REPL.**

- Call `ui_init()` at startup.
- Print a banner.
- Loop: read a line from the user, pass it to `agent_chat()`, print.
- Exit on `exit`, `quit`, `q`, or EOF.

**`agent/agent.c`**

1. Persist history across calls. This is the third time you are
   revisiting `struct Agent`; the history you allocated per-call in
   Phases A and B now needs to live on the agent itself and be freed in
   `agent_free`.
2. **Emit UI events at the right moments.** The public contract is in
   `ui/ui.h`. Roughly:
   - Before every `llm_chat` call, emit `ui_begin_thinking()`. The render
     thread draws a spinner until you signal otherwise.
   - When you are about to run tools, build a small stack array of
     `ToolCallView` values (name + short argument display) and call
     `ui_begin_tools(n, views)`.
   - After each tool completes, call `ui_tool_done(index, ok, output)`.
   - Before `printf`'ing the assistant's final text from the main
     thread, call `ui_idle()`. It is a _barrier_: it blocks until the
     render thread has released the dynamic region.

### 5.3 The synchronization question

You now have two threads that both want to write to the terminal: your
main thread (agent logic, LLM calls, tool dispatch) and the render thread
(spinner animation, per-tool status frames). Without discipline they
would interleave mid-line and the display would corrupt.

The `ui/` framework solves this by restricting the main thread to a
small event-posting API and letting the render thread own stdout for the
"dynamic region" (currently-running work). `ui_idle()` is the barrier
between the two regimes: the main thread waits on a condition variable,
the render thread acknowledges, and _then_ the main thread is free to
`printf`.

### 5.4 Tests

- **Multi-turn history**: two user prompts in one session. The mock
  records two distinct requests; the second one must contain both user
  messages in order, so the LLM sees what came before.
- **Clean exit**: `exit` terminates the program within a couple of
  seconds. The render thread must be joined (see `ui_stop`) — a
  lingering thread would block `main` from returning and the test would
  time out.


# 项目 2（第 1 部分）：一个基础的 AI 智能体

你已经拥有了一个编程智能体所依赖的三种机制：用于运行子程序的类 shell 的 fork/exec 模式、用于与远程服务通信的 HTTP 客户端，以及用于并发执行工作的线程池。然而，这些组件本身都无法决定接下来该做什么。

这个项目将改变这一点。你将把这些组件连接起来，并赋予最终的程序一个**决策器**——一个远程的大语言模型（LLM），它会逐轮选择下一个要运行的工具以及下一个要说的内容。当 LLM 请求执行一个 shell 命令时，你的程序会 fork 一个子进程，运行它，捕获输出，并将输出发回。当 LLM 没有更多工具要运行时，你的程序会打印出最终答案。

最终产品是一个运行在你终端中的编程助手。它会读取你的请求，针对真实的文件运行真实的命令，并报告它所发现的内容。它也是第 2 部分和第 3 部分（将添加工具注册表、并行执行和上下文管理）的基础，因此你在此处做出的结构决策将伴随整个项目。

---

## 1. 你要构建的内容

| 阶段 | 新能力                                  | 核心循环形态                                              |
| ---- | --------------------------------------- | --------------------------------------------------------- |
| A    | 一个用户提示 → 一次工具调用 → 回复      | `读取 → 询问 LLM → 运行工具 → 询问 LLM → 打印`                |
| B    | 每个提示允许多次工具调用（ReAct 模式）  | 将阶段 A 包裹在 `while` 循环中，当 LLM 停止调用工具时退出     |
| C    | 多轮对话 + 实时文本用户界面（TUI）      | 基于用户输入的外部 REPL 循环，带有旋转指示器和每工具状态行 |

---

## 2. 开始之前

### 2.1 目录结构

```
project2/
├── handout.md
├── Makefile
├── main.c
├── config.{h,c}
├── http.{h,c}
├── message.{h,c}
├── util.{h,c}
├── agent/
│   ├── agent.{h,c}
│   └── llm_client.{h,c}
├── tools/
│   ├── bash.c
│   └── tools.h
├── ui/
│   ├── ui.c
│   ├── ui.h
│   ├── render.c
│   └── internal.h
├── libs/
│   └── cJSON.{h,c}
└── tests/
    ├── mock_server.py
    ├── run_tests.py
    ├── harness.py
    ├── test_phase_a.py
    ├── test_phase_b.py
    └── test_phase_c.py
```

工作边界：

- 你主要编辑 `agent/`、`tools/bash.c` 和 `main.c`。
- `ui/`、`libs/` 和 `tests/` 是已提供的。请阅读它们，不要重写它们。
- `Makefile` 通过通配符自动发现 `agent/`、`tools/` 和 `ui/` 下的 `.c` 文件——在后续阶段添加新的源文件会自动生效。

### 2.2 构建和测试

```bash
make               # 默认二进制文件：build/c-agent
make test          # 运行所有阶段测试
make test-a        # 仅运行阶段 A 测试
make test-b
make test-c
make asan          # AddressSanitizer 构建 (build/c-agent-asan)，不运行测试
make test-asan     # 在 ASan 下运行所有测试
make clean
```

测试总是会先进行构建，因此在日常工作中仅执行 `make test-a` 就足够了。

### 2.3 模拟 LLM 服务器

`tests/mock_server.py` 是一个微型的 Python `http.server`，它伪装成 LLM API。你无需自己运行它——测试工具会为每个测试启动一个新的实例，并使用预设的响应脚本。

### 2.4 对接真实 LLM 运行

进行手动实验时，你可以将智能体指向一个真实的代理：运行 `caddy reverse-proxy --from :18080 --to https://models.sjtu.edu.cn --change-host-header`，导出你的 `API_KEY`，并设置 `LLM_HOST=127.0.0.1 LLM_PORT=18080`。

### 2.5 通过实际使用来测试你的智能体

阶段测试告诉你线路协议是正确的。但它们不会告诉你智能体是否**有用**。你可以这样尝试：

```bash
mkdir -p /tmp/agent-scratch && cd /tmp/agent-scratch
export API_KEY=sk-2Zlb2aNCFO4HrMD8riSLBw   # 参见 2.4
/home/sjc/桌面/week2/project2-part1/project2/build/c-agent
> 编写一个 C 程序，将前 20 个素数打印到 primes.c，然后编译并运行它
```

一个可工作的阶段 A 智能体将执行几个 `bash` 命令——写入文件、调用 `gcc`、运行二进制文件——并报告输出。这是最快发现模拟测试无法捕获问题的方法：

- LLM 的工具调用到达你的分发逻辑时，其格式是否可以被你实际解析（换行符、嵌套引号、heredocs...）
- 较长的工具输出是否会导致下一次请求体不稳定
- 失败的命令（编译错误）是否作为信号反馈给 LLM，而不是被视为静默成功

---

## 3. 阶段 A — 单轮工具调用

### 3.1 目标

输入一行指令，智能体使用 `bash` 收集信息，然后智能体给出答案。

```
$ ./build/c-agent
/etc 中有多少个 C 文件？
14
```

在底层，发生了五件事：

```
用户输入 ─┐
           ▼
   +----------------+      +-------------+      +-----------+
   |   agent.c      │ ───▶ │ llm_client  │ ───▶ │   LLM     │
   |  (编排逻辑)    │      │  构建 HTTP   │      │           │
   +----------------+      +-------------+      +-----------+
           ▲                                            │
           │              (返回: tool_call bash)        │
           │                                            ▼
   +----------------+      +-------------+      +-----------+
   │   打印(回复)   │ ◀─── │  llm_chat   │ ◀─── │ bash.c    │
   │                │      │  (第二次调用)│      │ fork/exec │
   +----------------+      +-------------+      +-----------+
```

### 3.2 已提供的内容

- `http.{h,c}` — HTTP 原语：`tcp_connect`, `send_all`, `recv_all`, `http_parse_response`。
- `message.{h,c}` — 一个用于存储 JSON 序列化消息的动态数组，以及用于构建 `user` / `tool` 消息的构造函数。
- `tools/tools.h` 和 `tools/bash.c` — `bash` 工具的 schema 全局变量，以及 `bash_tool_exec` 的子进程端（pipe + fork + dup2 + execl）。你需要填充父进程端。
- `main.c` — 读取一行，调用 `agent_chat`，打印回复，退出。
- `libs/cJSON` — 一个极简的 JSON 库。

### 3.3 你需要实现的内容

三个地方，每个都在源代码中用 TODO 标记：

**`tools/bash.c` — `bash_tool_exec` 的父进程端。**

在 fork 之后，你拥有 `pipefd[0]`（读取端）和 `pid`。将其转换为一个 `ToolResult`：决定保留多少输出，命令是否成功，以及 LLM 在下一轮看到的字符串内容。失败的命令仍然是重要的信息——非零退出状态和致命信号会告诉 LLM 其命令未按预期执行，因此应将此信息包含在 `.output` 中，而不是忽略它。参见 `waitpid(2)` 以及 `<sys/wait.h>` 中的 `WIFEXITED` / `WEXITSTATUS` / `WIFSIGNALED` / `WTERMSIG` 宏。在返回之前关闭 `pipefd[0]` 并回收子进程。

**`agent/llm_client.c` — `llm_chat`。**

1.  构建请求体。这是一个包含以下字段的 JSON 对象：
    `model`、`messages`（一个数组，包含系统提示符和给定的消息列表）、`tools`（一个数组，其中有一个描述 `bash` 的条目）以及 `max_tokens`。使用 `cJSON` 来构建它——不要手动拼接字符串。
2.  构建 HTTP 请求，`tcp_connect`，先 `send_all(header)` 然后 `send_all(body)`，`recv_all`，`http_parse_response`。
3.  拒绝非 200 状态码——向 `err` 中写入一条消息并返回 -1。
4.  解析响应体：`choices[0].message` 是助手消息。
    读取它的 `content`（可能为空或不存在）及其 `tool_calls` 数组。
    对于每个工具调用，捕获 `id`、`function.name` 和 `function.arguments`（在线路上是一个 JSON **字符串**；使用 `cJSON_Parse` 将其解析回对象）。
5.  保留整个助手消息的序列化副本（`raw_message`）——智能体会将其逐字推回历史记录中，以便 LLM 在下一次调用时能看到它自己之前的回复。

**`agent/agent.c` — `agent_chat`。**

1.  将一条 `user` 消息推送到本地的 `MessageList` 上。
2.  调用 `llm_chat`。失败时返回 NULL。
3.  如果响应中没有工具调用，打印并缓存其 `content`，然后返回它。完成。
4.  否则：将 `response.raw_message` 推入历史记录；执行 LLM 请求的工具；将工具结果作为一条 `tool` 消息推送，并附带正确的 `tool_call_id`；再次调用 `llm_chat`；打印并返回最终内容。

### 3.4 需要你处理的边界情况

- **无工具参数：** LLM 可能会发出一个带有空 `arguments` 字符串的工具调用。`cJSON_Parse("")` 返回 NULL；应将其视为 `cJSON_CreateObject()`。
- **失败的 shell 命令：** 非零退出或因信号终止的命令并非智能体中的错误——它是 LLM 需要看到以便恢复的观察结果。将退出状态编码到工具输出中，而不是返回一个通用的 "bash failed" 消息。
- **未知的工具名称：** 测试套件会发送一个智能体从未听说过的工具调用。你的分发逻辑必须用 `ToolResult` 拒绝它，并在 `.output` 中指出未知工具的名称，这样 LLM 才能看到其错误并重试，而不是让程序中止。

---

## 4. 阶段 B — ReAct 循环

### 4.1 目标

阶段 A 只允许 LLM 执行一个操作。真实任务（"找到定义 `foo` 的文件，然后计算其中的行数"）需要一个操作链：读取、思考、再次读取、思考、回答。这种模式被称为 **ReAct**（Reason + Act，推理+行动）：LLM 大声推理，采取行动（工具调用），观察结果，进一步推理，再次行动，最终返回一个最终答案。

```
          ┌────────────────────┐
  用户 ──▶│     agent_chat     │
          │                    │
          │   ┌────────────┐   │
          │   │ llm_chat   │◀──┼──────────┐
          │   └────┬───────┘   │          │
          │        │           │          │
          │  工具调用数量？     │          │
          │   ┌────┴────┐      │          │
          │  =0         >0     │          │
          │   │         │      │          │
          │   ▼         ▼      │          │
          │ 打印      执行每个 ─┼─► 推送 ──┘
          │           工具     │    工具
          │                    │    结果
          └────────────────────┘
```

### 4.2 变化之处

你需要编辑你已经拥有的两个文件：

- `agent/llm_client.{h,c}`：在阶段 A 中，你只需要解析每个响应的一个工具调用。现在你需要解析所有工具调用。重新审视 `LLMResponse` 如何存储其 `tool_calls`——如果你选择一个可以自圆其说的上限，使用固定大小的数组是可以的；使用可增长的缓冲区也可以。无论哪种方式，更新 `llm_chat` 以按请求顺序解析每个条目。
- `agent/agent.c`：将 `agent_chat` 的主体包裹在一个循环中。当响应中的工具调用数量为零时终止。在一次迭代中，执行响应中的每个工具调用，并按请求顺序推送它们的结果。这也是审视你在阶段 A 中编写的 `struct Agent` 的好时机——多轮循环可能需要你之前不需要的状态。

### 4.3 为什么"按请求顺序"

如果 LLM 在单个响应中要求调用 `t1` 和 `t2`，则工具消息必须按该顺序出现在历史记录中，即使**执行**顺序可能不同。在阶段 B 中，执行是顺序的，因此这是自动的；第 2 部分将并行执行它们，届时顺序不变性将成为一个真正的设计约束。现在以不假定串行执行的方式编写代码，这样你以后就不必重新修改它。

### 4.4 终止作为不变性

什么能保证循环结束？理论上，LLM 可以不停地要求工具调用。通常有两道防线：

1.  硬性的迭代次数上限（`MAX_TURNS`，比如 20）。如果超出，报告错误并停止。这是每个生产级智能体保护自己的方式。
2.  一个合理的系统提示，促使 LLM 完成任务。你现在不需要调整它——模拟测试通过结束其预设脚本来自动终止。

一个失败的工具执行**仍然**是一个有效的工具结果。将失败消息推回历史记录，让 LLM 作出反应；不要中止循环。

### 4.5 测试检查的内容

- 三跳链：模拟脚本连续进行三次 `bash` 调用，然后返回最终文本。你的智能体必须向模拟服务器发出四次请求，并在不断增长的历史记录中保留每个工具结果。
- 单个 LLM 响应中的两个工具调用：两者都执行，并按顺序返回。

---

## 5. 阶段 C — 多轮对话和 TUI

### 5.1 目标

一次性的交互对于演示来说还可以；但真正的智能体应该能进行对话。用户问某事，得到答案，接着问一个后续问题，因为智能体仍然记得第一轮对话的内容，从而得到更好的答案。同时，我们最终开始向用户展示智能体运行时**它正在做什么**——旋转指示器、每个工具的状态行、耗时。

### 5.2 你需要实现的内容

**`main.c` — 将单次驱动转变为 REPL。**

- 在启动时调用 `ui_init()`。
- 打印一个横幅。
- 循环：从用户读取一行，将其传递给 `agent_chat()`，打印。
- 在遇到 `exit`、`quit`、`q` 或 EOF 时退出。

**`agent/agent.c`**

1.  在多次调用间持久化历史记录。这是你第三次重新审视 `struct Agent` 了；你在阶段 A 和 B 中为每次调用分配的历史记录现在需要存在于智能体本身，并在 `agent_free` 中被释放。
2.  **在正确的时刻发出 UI 事件。** 公共契约在 `ui/ui.h` 中定义。大致如下：
    - 在每次 `llm_chat` 调用之前，发出 `ui_begin_thinking()`。渲染线程会绘制一个旋转指示器，直到你发出其他信号。
    - 当你即将运行工具时，构建一个包含 `ToolCallView` 值（名称 + 简短参数显示）的小型栈数组，并调用 `ui_begin_tools(n, views)`。
    - 每个工具完成后，调用 `ui_tool_done(index, ok, output)`。
    - 在主线程 `printf` 助手的最终文本之前，调用 `ui_idle()`。它是一个**屏障**：它会阻塞，直到渲染线程释放了动态区域。

### 5.3 同步问题

你现在有两个线程都想写入终端：你的主线程（智能体逻辑、LLM 调用、工具分发）和渲染线程（旋转动画、每工具状态帧）。没有纪律的话，它们会在行中间交错输出，导致显示混乱。

`ui/` 框架通过将主线程限制为一小组事件发布 API，并让渲染线程拥有"动态区域"（当前正在运行的工作）的标准输出来解决这个问题。`ui_idle()` 是两种模式之间的屏障：主线程等待条件变量，渲染线程确认，**然后**主线程才能自由地 `printf`。

### 5.4 测试

- **多轮历史记录**：一个会话中的两个用户提示。模拟服务器记录了两个不同的请求；第二个请求必须按顺序包含两条用户消息，这样 LLM 才能看到之前的内容。
- **干净退出**：`exit` 应在几秒钟内终止程序。渲染线程必须被 join（参见 `ui_stop`）——一个残留的线程会阻止 `main` 返回，导致测试超时。