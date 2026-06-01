# Project 2 - C Agent

A C-language implementation of an AI agent system with tool calling, context management, and extensible capabilities.

---

## Extensions

This project includes 5 extension features for enhanced agent capabilities. Each extension is designed for **autonomous agent triggering** - the agent decides when to use these tools based on the conversation context.

### 6.1 Evaluation（评估）

#### 功能介绍

用于跟踪和记录代理执行任务时的表现指标，包括：
- 成功率（success rate）
- 工具调用轮数（tool call rounds）
- 提示令牌消耗（prompt tokens）
- 完成令牌消耗（completion tokens）
- 任务耗时（duration）

#### 使用方法

```
eval_start scenario="任务名称"    # 开始评估场景
eval_end success=true/false      # 结束评估，记录成功/失败状态
eval_report path="报告路径"       # 生成评估报告
```

#### 验证示例

```
用户输入: "帮我递归查找所有 .c 文件并统计数量"
代理行为:
  1. eval_start scenario="查找c文件"2. main.c 在说什么3. eval_end success=true4. eval_report path=".agent/eval_report.txt"
```

**验证结果示例**（`.agent/eval_report.txt`）：
```
Evaluation Suite: agent_eval
Total Scenarios: 1

Scenario 1: 查找c文件
  Success: true
  Tool Call Rounds: 5
  Prompt Tokens: 1200
  Completion Tokens: 800
  Duration: 2.50 seconds

Summary:
  Success Rate: 100.0% (1/1)
  Total Prompt Tokens: 1200
  Total Completion Tokens: 800
```

---

### 6.2 SubAgent（子代理）

#### 功能介绍

当代理需要处理多个独立子任务时，可以创建子代理来分担工作。每个子代理：
- 拥有独立的上下文窗口（使用主代理的一半大小）
- 执行有边界的子任务
- 返回结构化结果（含指标）

这避免了单一代理同时处理多任务时注意力分散的问题。

#### 使用方法

```
subagent_spawn task="子任务描述"    # 创建并执行子代理
subagent_result                     # 获取上一个子代理的执行结果
```

#### 验证示例

```
用户输入: "分析项目结构，包括：1)目录布局 2)主要模块 3)构建方式"
代理行为:
  1. subagent_spawn task="分析目录布局"
  2. subagent_spawn task="分析主要模块"
  3. subagent_result  # 获取第一个结果
  4. subagent_result  # 获取第二个结果
  5. 聚合结果返回给用户
```

**验证结果示例**：
```
Subagent spawned. Result: 项目包含 agent/, context/, tools/, memory/, session/ 等模块
Metrics: rounds=1, prompt_tokens=500
```

---

### 6.3 Session（会话）

#### 功能介绍

支持对话会话的持久化保存和恢复：
- 代理自动记录用户输入和工具执行结果到 `.agent/sessions/{session_id}.log`
- 通过 `session_replay()` 可恢复历史消息到对话上下文
- 支持中断后继续执行

#### 使用方法

```
session_save session_id="会话ID"     # 保存当前会话（创建或打开日志文件）
session_load session_id="会话ID"     # 加载历史会话并重新开启写入
session_clear                         # 清除当前会话
```

#### 验证示例

```
# 第一轮对话
用户输入: "帮我分析 tools 目录的结构"
session_save session_id="work1"      # 保存会话
... 继续对话 ...

# 重新启动后加载会话
用户输入: "session_load session_id="work1""
代理行为: 加载历史日志，显示消息数量（27条），可继续之前的任务
```

**验证结果**：
- 会话保存后在 `.agent/sessions/` 下生成 `{session_id}.log` 文件
- `session_load` 可正确恢复之前的消息记录
- 实际测试：`session_load session_id="work1"` 返回 "会话已加载，包含 27 条消息"

**内部机制**：
- 用户输入时自动调用 `session_save_raw()` 记录到日志
- 工具执行结果也会自动记录到日志
- 日志格式为 `[timestamp] RAW: {json}` 便于解析

---

### 6.4 Memory（记忆）

#### 功能介绍

项目级别的知识存储系统，允许代理在不同会话间保持连续性：
- 持久化存储到 `.agent/memory.json`
- 以键值对形式存储项目信息（构建约定、编码风格、技术栈等）
- 跨会话持久化，重启后信息不丢失

#### 使用方法

```
memory_write key="键名" value="值"    # 写入记忆
memory_read key="键名"                 # 读取记忆
memory_list                           # 列出所有记忆
memory_clear                          # 清除所有记忆
```

#### 验证示例

```
# 第一轮：学习项目信息
用户输入: "这个项目使用 CMake 构建，记住这些项目约定"
代理行为:
  memory_write key="build_system" value="CMake"
  memory_write key="project_conventions" value="使用CMake构建系统"

# 第二轮：利用记忆
用户输入: "这个项目的构建系统是什么？"
代理行为:
  memory_read key="build_system"
  返回: "CMake"
```

**验证结果**（`.agent/memory.json`）：
```json
{
    "build_system": {
        "value": "CMake",
        "timestamp": "1780318148"
    },
    "project_conventions": {
        "value": "使用CMake构建系统",
        "timestamp": "1780318149"
    }
}
```

---

### 6.5 Skill（技能）

#### 功能介绍

渐进式披露的技能系统，避免在启动时注入大量提示令牌：
- 启动时只注入技能名称和一行描述
- 当代理识别到相关任务时，按需加载完整提示
- 支持自定义技能（`.skill` 文件格式）

#### 使用方法

```
skill_list          # 列出所有可用技能
skill_info name="技能名"     # 获取技能简要信息
skill_load name="技能名"     # 加载技能完整提示
```

#### 技能文件格式

`.agent/skills/{skill_name}.skill` 文件格式：
```
技能名称
---
一行描述
---
完整提示内容（多行）
```

#### 验证示例

```
# 启动时验证
代理系统提示包含：
  - git_helper: Git操作助手，帮助管理代码版本和分支
  - code_review: 审查代码质量问题，检查安全漏洞和代码风格

# 用户请求使用技能
用户输入: "请对这个项目的代码进行安全审查"
代理行为:
  skill_load name="code_review"
  收到完整的代码审查步骤和输出格式要求
  开始执行代码审查任务
```

**验证结果**：
- `skill_list` 返回技能名称和描述列表
- `skill_load` 返回完整的技能提示内容

---

## 实现细节

### 评估指标统计

`eval_end` 会自动统计以下指标：
- **tool_call_rounds**：通过 `eval_tools_inc_rounds()` 在每次工具调用时递增
- **prompt_tokens**：通过 `eval_tools_set_ctx()` 传入 Context 获取 `ctx_total_tokens()`
- **completion_tokens**：记录完成令牌数

### 会话日志持久化

代理在以下时机自动记录会话日志到 `.agent/sessions/{session_id}.log`：
- 用户输入消息时
- 工具执行结果返回时

日志格式为 JSON，便于后续 `session_replay()` 恢复。

### 编译说明

项目使用 C11 标准编译，主要依赖：
- `libpthread`：多线程支持
- `cJSON`：JSON 解析

如遇编译问题，检查头文件包含顺序和 `extern` 关键字声明。

---

## 已知问题修复

1. **eval_tools.h 多重定义**：ToolDef 使用 `extern` 声明
2. **session_tools.h Session 类型未定义**：添加 `#include "session/session.h"`
3. **session_create 文件模式**：根据文件是否存在选择 "w" 或 "a" 模式
4. **eval_tools_inc_rounds 未声明**：添加函数声明并集成到 agent 主循环
5. **session_load 返回0消息**：
   - 问题：`session_open` 以 "r" 模式打开文件导致 `session_load` 无法正确计数
   - 修复：添加 `session_reopen_for_write()` 在加载后重新以 "a" 模式打开
   - 同时修复 `session_load` 只统计包含 "RAW: " 的行
6. **session_tools.h 多重定义**：ToolDef 添加 `extern` 声明

---

## 运行测试

```bash
# 构建
make

# 运行 agent
./build/c-agent

# 测试特定扩展（直接输入工具调用）
./build/c-agent
> memory_write key="test" value="hello"
> eval_start scenario="test"
> subagent_spawn task="计算 2+2"
> skill_list
> exit
```

---

## 目录结构

```
project2/
├── agent/          # 核心代理逻辑
│   ├── agent.c    # 主代理循环
│   ├── subagent.c # 子代理实现
│   └── llm_client.c
├── context/       # 上下文管理
│   ├── context.c
│   ├── policy_offload.c
│   └── policy_summary.c
├── tools/         # 工具实现
│   ├── eval_tools.c      # 6.1 评估
│   ├── subagent_tools.c # 6.2 子代理
│   ├── session_tools.c  # 6.3 会话
│   ├── memory_tools.c   # 6.4 记忆
│   └── skill_tools.c    # 6.5 技能
├── evaluation/   # 评估系统
├── memory/       # 记忆系统
├── session/      # 会话系统
├── skills/       # 技能系统
└── .agent/       # 运行时数据
    ├── memory.json       # 记忆存储
    ├── sessions/         # 会话日志
    └── skills/           # 技能文件
```