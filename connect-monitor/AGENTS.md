# connect-monitor 协作规则

继承 [仓库规则](../AGENTS.md)。本目录是 Electron/React RF 诊断客户端；与 RX HID telemetry 配套，不是 WebConfig 产品页面。

## 仍有效的用户约束

- 2026-09-20 起暂停本轮 RF/监视器自动回归测试和设备采样，由用户验证；源码检查、类型检查和编译可继续。用户明确要求恢复对应范围前，不启动测试、HID 采集或自动参数实验。
- 不自行恢复自动跳频或改变实机固定频道基线。完整 RF 约束见 [RF_PHY_Hop/AGENTS.md](../RF_PHY_Hop/AGENTS.md)。
- 本地已有日志可按任务离线分析；软件构建、历史记录和设备当前运行版本须分别报告。

## 数据与进程边界

- 主进程通过 [hid-telemetry-client.ts](electron/sources/hid-telemetry-client.ts) 调用独立 HID worker；[hid-telemetry-source.ts](electron/sources/hid-telemetry-source.ts) 在 worker/CLI 内使用 `HIDAsync` / `devicesAsync`，不要把同步设备 I/O 放回主进程。
- 历史存储使用 [async-event-store.ts](electron/pipeline/async-event-store.ts) 的 worker；保留批处理、队列上限、清理代次隔离。
- renderer 接收增量 patch，合并后在 React 提交后回 ACK；不要把增量当完整快照覆盖。详见 [线程与队列说明](../docs/CONNECT_MONITOR_UI_THREADS_20260922.md)。
- 默认 RF 观察使用 HID telemetry。避免额外 HID reader 争抢设备，也不要改用高频 CDC 文本推断真实输入性能。
- 设备选择由 [hid-device-selection.ts](electron/sources/hid-device-selection.ts) 统一决定；不能仅凭名称含 HBox 或 `usagePage=0xFF00` 接受设备。始终排除 WebConfig HID `0xCAFE:0x4021`，显式 VID/PID 覆盖也不能绕过此边界。

## 指标与控制语义

- Report Rate 表示 RX 合法 DATA 的窗口速率，不是 HID telemetry 包频率。Packet Loss、软件丢弃、状态合并和 USB 拥塞属于不同统计，不相互代替。
- Link Lost Duration 是固件 DATA 静默判定；Link Recovered Duration 是主机看到恢复事件的时间差，不能当作同一延迟。
- `RHM1` 是主统计；`RHS1`、`RHD1`、`RHF3`、`RHT5`、`RIG5`、`RLS1`、`RXP1` 等诊断不应抬高 DATA 输入计数。版本、页数、量纲和 sentinel 按生产者及解析器核对。
- 分页快照只有收齐且新鲜时才可用；缺页、乱序、重连或旧固件缺能力时显示不可用，不能以零值代替。直方图分位数只代表桶上界。
- 延迟记录按真实按下/松开筛选，不能因阶段不完整丢掉真实事件；同一事件后续分片更新原记录，缺失阶段不能臆造时间或来源。
- Channels 控制依赖新鲜状态和有效频道；关闭 auto 时使用当前工作频道，不用旧列表首项兜底。配置请求成功还需结合回执、两端状态和后续输入确认。
- 延迟测量开关保留用户配置；恢复连接时按已有机制补发/续期，状态一致时只续期，避免持续发送 RF 配置。
- RXP1 和延迟解析入口见 [rx-profile.ts](electron/sources/rx-profile.ts)、[relative-latency.ts](electron/sources/relative-latency.ts)。协议改动核对 RX 生产者和 UI，而非只改显示标签。

## 构建与使用

在本目录执行，命令以 [package.json](package.json) 为准：

| 目的 | 命令 |
|---|---|
| 类型检查 | `npm run typecheck` |
| Electron 编译 | `npm run build:electron` |
| 完整构建 | `npm run build` |
| Mock 启动 | 构建后 `npm run start:mock` |
| 实机启动 | 构建后 `npm start`，仅在用户已要求设备观察时执行 |
| 离线日志报告 | 仓库根目录 `python tools/rf_link_report.py <monitor-events.jsonl> --since-ms <Unix毫秒>` |

修改 worker/主进程后，旧运行进程不会自动使用新代码；需要验证时完整退出再启动。测试脚本按实际文件选择，暂停约束解除前不自动运行；不要把 `test:hid-selection` 当作全套覆盖。

纯 renderer 修改先做相关类型检查，需产物时运行 `npm run build:renderer`；Electron 源码修改使用 `npm run build:electron`；跨进程契约修改检查两端。无对应源码变化不重复完整构建，不自动生成安装包或启动实机观察；上述范围选择不解除本文件的暂停测试要求。

历史协议表与实验记录按需查 [归档索引](../docs/agent-history/README.md)；当前行为以源码及本文件为准。
