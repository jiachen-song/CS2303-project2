# C Agent

**一个基于 C 语言的 AI 编程智能体，支持工具调用、并行执行与上下文管理。**

---

## 项目简介

C Agent 是一个终端编程助手，通过连接远程 LLM 实现智能化的代码编辑、文件操作和任务自动化。它运行在终端中，用户输入自然语言指令，代理执行真实命令并返回结果。

**解决的问题**：
- 将 LLM 的推理能力与本地文件系统操作结合
- 通过上下文管理支持长对话，避免令牌溢出
- 通过并行执行加速多文件操作

---

## 主要特性

- **自然语言交互**：用中文或英文描述任务，代理理解并执行
- **多工具支持**：bash 命令、文件读写编辑、代码审查
- **并行执行**：多个只读操作（如读文件）并行执行，加速批量处理
- **上下文管理**：自动卸载长输出、摘要旧对话，保持令牌预算可控
- **会话持久化**：对话记录保存到磁盘，重启后可恢复
- **项目记忆**：跨会话存储项目知识（构建方式、编码规范等）
- **技能系统**：按需加载专业提示模板（如代码审查流程）
- **实时 TUI**：显示工具执行状态、令牌使用量

---

## 快速开始

### 前置条件

- GCC 支持 C11 (`-std=c11`)
- POSIX 系统（Linux/macOS）
- LLM API 访问（通过 Caddy 反向代理或直接 API）

### 安装

```bash
# 克隆项目
git clone <repository-url>
cd project2

# 编译
make
```

### 配置

```bash
# caddy 代理
caddy reverse-proxy --from :18080 --to https://models.sjtu.edu.cn --change-host-header
```

新建终端

```bash
# 设置 API 密钥
export API_KEY=your_api_key

# 可选：指定 LLM 地址（默认 127.0.0.1:18080）
export LLM_HOST=127.0.0.1
export LLM_PORT=18080
```

### 运行

```bash
./build/c-agent
```

### 示例会话

```
> 帮我创建一个 hello.c 文件，包含 main 函数输出 "Hello, World"
[工具调用: write_file]
文件已创建。

> 编译并运行
[工具调用: bash]
hello world
```

---

## 使用指南

### 核心概念

**工具调用**：代理通过 LLM 决定调用哪些工具。工具有：
- `bash` — 执行 shell 命令
- `read_file` — 读取文件（可选限制行数）
- `write_file` — 创建/覆盖文件
- `edit_file` — 替换文件中首匹配的文本

**上下文窗口**：每次 LLM 调用都会消耗令牌。当对话过长时，系统自动：
1. **卸载**：将长输出保存到磁盘，替换为占位符
2. **摘要**：将旧对话压缩成简短摘要

### 配置项

| 环境变量 | 说明 | 默认值 |
|---------|------|--------|
| `CONTEXT_WINDOW` | 模型令牌预算 | 100000 |
| `OFFLOAD_THRESHOLD` | 触发卸载的令牌比例 | 0.7 |
| `SUMMARY_THRESHOLD` | 触发摘要的令牌比例 | 0.85 |
| `API_KEY` | LLM API 密钥 | 必须设置 |
| `LLM_HOST` | LLM 服务地址 | 127.0.0.1 |
| `LLM_PORT` | LLM 服务端口 | 18080 |

### 扩展工具

| 工具 | 说明 |
|------|------|
| `eval_start / eval_end / eval_report` | 评估任务表现（成功率、令牌消耗） |
| `subagent_spawn / subagent_result` | 创建子代理处理独立任务 |
| `session_save / session_load / session_clear` | 会话持久化 |
| `memory_write / memory_read / memory_list` | 跨会话知识存储 |
| `skill_list / skill_load / skill_info` | 技能系统（渐进式提示加载） |

### 代码示例

**基本交互**：
```
> 统计当前目录下所有 .c 文件的行数
```

**使用评估**：
```
> eval_start scenario="批量文件操作"
> ... 执行任务 ...
> eval_end success=true
> eval_report path=".agent/report.txt"
```

**使用记忆**：
```
> 记住这个项目的构建方式是 CMake
> memory_write key="build" value="CMake"

> 下次问起时
> memory_read key="build"
```

### 局限性

1. **API 依赖**：需要稳定的 LLM API 连接，断网时无法工作
2. **沙盒限制**：文件操作限制在工作目录内，无法访问外部路径
3. **令牌预算**：长对话仍可能触发摘要策略，导致细节丢失
4. **并行限制**：只有只读操作并行，写入/编辑操作串行执行

---

## 目录结构

```
project2/
├── agent/              # 代理核心逻辑
│   ├── agent.c         # 主循环、TUI 集成
│   ├── subagent.c      # 子代理
│   └── llm_client.c    # LLM HTTP 客户端
├── context/            # 上下文管理
│   ├── context.c       # 上下文接口
│   ├── policy_offload.c   # 卸载策略
│   └── policy_summary.c   # 摘要策略
├── tools/              # 工具系统
│   ├── registry.c     # 工具注册表
│   ├── executor.c     # 执行器（并行）
│   ├── sandbox.c      # 沙盒隔离
│   ├── bash.c / read.c / write.c / edit.c  # 基础工具
│   └── eval/subagent/session/memory/skill  # 扩展工具
├── ui/                 # TUI 实现
├── evaluation/         # 评估系统
├── memory/             # 记忆系统
├── session/           # 会话系统
├── skills/            # 技能系统
└── .agent/            # 运行时数据
    ├── memory.json     # 记忆存储
    ├── sessions/       # 会话日志
    └── skills/         # 技能文件
```

---

## 许可证

MIT License