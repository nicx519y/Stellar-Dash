# XORA 文档审核记录：2026-10-05

本次检查仓库全部 13 份 `AGENTS.md`（含 TinyUSB 上游文件），以及项目自有 Markdown 的链接、目录锚点、命令名和高风险旧信息。逐项源码核对集中在当前模块导航、构建/烧录、Hosted/WebHID、服务端部署、TX/RX 和监视器入口；日期实验记录按当时版本保留，不将其中“已通过”或“待完成”改写成现状。

## 已确认并修正

| 问题 | 修正与依据 |
|---|---|
| 文档总入口仍推荐低层旧烧录与 release 流程，并复制过时地址表 | [docs/README](README.md) 改为现行任务导航和无锁构建入口，地址改链 [权威定义](../common/firmware_metadata.h)；旧正文完整归档 |
| 架构将 attestation / permit 写成 WebConfig 产品连接前提 | [架构](architecture.md) 改为 `session.open-direct` 加密直连，账号/管理员权限独立；依据 [设备会话客户端](../application/www/lib/device-transport/device-session-client.ts)、[固件 service](../application/Src/webconfig/webhid_service.cpp) |
| 架构将 TX IAP 布局泛化到所有 CH585 | 明确 TX `0x1000` Application 与 RX 独立链接布局；依据各自 Makefile 和 [TX/RX 规则](../RF_PHY_Hop/AGENTS.md) |
| RF README 声称固定 bond 默认开启，带固定盘符链接和编译命令 | [RF README](../RF_PHY_Hop/README.md) 改为默认 0、相对链接与目标构建入口；依据 [协议定义](../RF_PHY_Hop/Common/include/rf_hop_protocol.h) 和 Makefile |
| 旧 RF 配对/实施计划仍像当前待办，包含屏幕及 RX 长按入口 | 明确标为历史方案，链接 [USB 绑定](WEBCONFIG_RX_BINDING_20260923.md) 和 [入口移除](LEGACY_PAIR_ENTRY_REMOVAL_20260923.md)，不恢复旧入口 |
| server README 含不存在的 lint 脚本、start.js/stop.js、JWT 和 PM2 必需项 | 按 [package.json](../server/package.json)、邮箱与管理员实现修正，当前进程管理为 systemd；未声称通用病毒扫描或 DEBUG 开关已经实现 |
| 监视器架构仍描述旧 RFModule/dongle 路径、旧主要协议和同步存储 | [当前架构](../connect-monitor/ARCHITECTURE.md) 按 HID worker、异步存储 worker、renderer patch/ACK 重写；旧实施计划标为参考 |
| 旧本地安全调试文档要求“随后”设置保护位，易被当日常步骤 | 改为旧方案说明，并显式链接根目录禁令；制造、V2 证明与防降级资料限定为专项设计参考 |
| 旧固件管理器教程仍用 2.0625MB 用户图片区和 HTTP 集成 | 图改为 1.5625MB 用户图片 + 512KB CH585 staging；旧接口/HTTP 示例标为历史参考，链接现行实现 |
| 旧 WebSocket、CH584 原型和 PCB bring-up 被当作当前实现说明 | 明确记录范围与日期，不用旧 15B payload、临时禁用项或历史吞吐作为当前依据 |
| 部分当前文档标题与产品介绍仍用旧名称 | 改用 XORA；保留源码、路径、GUID、存储目录、协议 `HBOX` 等兼容标识 |
| server README 的 LICENSE 链接不存在，版本日期与联系方式是占位模板 | 移除失效链接及无依据的模板信息，如实记录许可证正文缺失 |

## 检查结果与限制

- 最终静态扫描覆盖 127 份项目自有 Markdown、全部 13 份 AGENTS；检查 542 个正文内本地链接及锚点，未发现剩余失效目标。归档中 4 份旧 AGENTS 与 2 份新 README 快照的字节数及 SHA-256 均核对通过；修改后 `git diff --check` 通过。
- `AGENTS.md` 的本地链接、现行 npm 脚本名及主要构建命令与源码一致；没有新增 `AGENT.md` 单数文件或 override 文件。
- 仓库内每条 AGENTS 目录链均低于默认 32 KiB：TX 约 22.52 KiB，RX 约 21.84 KiB，WebConfig 约 22.61 KiB；最长为 TinyUSB 链约 31.19 KiB。这是文件字节累计，不包含聊天中额外提供的指令，也不计链接文档的全文；TinyUSB 后续扩写须注意余量。
- 项目自有 Markdown 的正文链接及目录锚点检查；忽略 fenced code 中类似链接的语法，避免将 C++ lambda 当作失效链接。当前导航没有缺失的本地目标或锚点。
- npm 命令名与 server、WebConfig、connect-monitor、Windows UI 的 package.json 核对；AGENTS 中的 npm 命令另外按所在模块核对。没有执行命令中的设备写入或发布动作。
- 替换前的 docs/RF README 保留原始字节与 SHA-256，见 [归档索引](agent-history/README.md)。2026-09-24 的原始 AGENTS 快照和已有冻结契约/哈希均保留。
- 仅做文档、源码和主机静态检查；没有运行 RF 回归、设备采样、烧录、构建、远端部署或邮件发送。没有对每份历史实验记录重新复现实机结果，也未验证所有外部网站是否可访问。
- 第三方库的大量上游文档未逐页重写；TinyUSB AGENTS 的上游命令保留为其独立工作参考，XORA 集成规则继续明确优先。

## 仍需维护者处理的事项

- 服务端 `package.json` 声明 MIT，但仓库缺少对应项目许可证正文；本次没有代替维护者决定或新增法律授权文本。
- IAP 安装/启动、RF 吞吐/延迟和各日期记录中的实机验证缺口仍按各专题报告保留；文档修正不构成验收通过。
- 历史 SPI handoff 中已移除的源路径，以及日期实验中的 `.hbox` 日志路径按原记录保留，不能作为当前源码或共享交付物链接。

后续维护先更新现行入口和相应专题文档；历史快照与实验数据保留版本上下文，不批量替换兼容标识、更新冻结哈希或把历史结论移植为当前事实。
