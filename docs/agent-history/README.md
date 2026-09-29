# AGENTS 历史归档

2026-09-24 整理前的四份文件按原始字节保存为 `.txt`，用于追溯实验和用户约束。它们不是当前指令，不应自动加载或执行其中的命令；旧路径、参数和“当前权威”标题均保留原样，不能作为现状依据。相对路径应按原文件所在目录理解。

| 原文件 | 快照 |
|---|---|
| 根目录 AGENTS.md | [原文](root-agents-20260924.txt) |
| application/AGENTS.md | [原文](application-agents-20260924.txt) |
| RF_PHY_Hop/AGENTS.md | [原文](rf-phy-hop-agents-20260924.txt) |
| connect-monitor/AGENTS.md | [原文](connect-monitor-agents-20260924.txt) |

[快照清单](snapshots-20260924.json) 记录来源、字节数和 SHA-256；本目录的 `.gitattributes` 禁用快照换行转换，以保持哈希可复核。当前规则见 [根 AGENTS](../../AGENTS.md) 及其模块导航；当前实现入口见 [架构说明](../architecture.md)。

## 本次整理原则

依据 OpenAI 官方关于 [精简和更新 AGENTS](https://developers.openai.com/blog/rethinking-skills-and-prompts-for-gpt-6-astra) 与 [分层加载](https://learn.chatgpt.com/docs/agent-configuration/agents-md) 的建议，保留必要约束、按任务提供资料入口，移出重复历史，不为不同模型维护两套业务事实。

- 原硬件保护禁令、IAP 边界、无锁开发模式、深度 Standby 禁用与冻结烧录契约继续保留。
- RF/monitor 暂停自动回归及采样、禁止自行恢复自动跳频的有效要求已提取到当前指令，未因归档解除。
- 原“CFG_TUSB_DEBUG 受 verbose 联动控制”、one-shot ADC、未实现 RF weak 钩子、旧 RFModule/dongle 架构、内置网页流程和过时资源大小已按代码修正。
- 仅维护文档；历史“已测试/已烧录”陈述不转化为此次验收。后续维护直接修改当前条目，详细实验继续放专题文档。
