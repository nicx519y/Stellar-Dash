# XORA 日志与诊断的独立开关

Device Logs 只提供两个功能开关，均默认关闭并保存到主控设备配置。旧版总开关的已保存 ON 迁移为两项 ON，OFF 仍为两项 OFF；导入配置与切换配置档不会改变这两个设备级设置。

| 设备日志记录 | 诊断监控采集 | 行为 |
|---|---|---|
| 关 | 关 | 不新增日志、不自动落盘、不采集或发送诊断 |
| 开 | 关 | 记录故障/连接日志，不运行监视器统计、打点和遥测 |
| 关 | 开 | 允许 Connect Monitor 统计、延迟采集及遥测，不新增设备日志 |
| 开 | 开 | 两项各自运行 |

日志开关只控制结构化故障/连接记录及其必要计数、观察和安全窗口持久化。监控开关只控制 profiler、延迟打点、SPI/source trace、诊断统计和 RF AUX / USB 遥测。切换日志不会重置监控会话；切换监控不会清空日志观察状态。历史日志仍可手动读取、导出和清空，电池测试独立控制。

Connect Monitor 移除 HID、Latency、Start/Pause Listening 重复控制入口。连接时同时请求统计与延迟采集，但设备权限关闭时不得采集、自动重试或续租。开启权限后点击“重新连接采集”或重启客户端；此按钮不改变设备权限。频道设置、来源绑定及清空本地历史保留。

## Device Logs 状态展示

开关使用固定网格位置，检查按钮不随请求状态改变文案宽度。初始读取、保存、失败及 RX 确认均复用同一提示区域；RX 检查按钮始终占位，状态未知时禁用。状态文字按当前语言和窗口宽度预留换行空间，页面通知使用固定占位，避免响应返回时推移日志区域。

设备操作锁仍由 `read` / `clear` / `policy` 共用，但日志卡片只在实际 `read` / `clear` 时展示进度。保存开关只锁定操作，不触发日志读取、不替换已读列表、不重置列表滚动位置。Mock 可通过 `mockDiagnosticsDelay=1200` 模拟慢应答（最大 3000ms），与 `mockDiagnostics=save-failed|unsupported` 组合检查失败展示。

2026-10-26 浏览器复现时，旧布局在开关保存期间横移约 12px，首次应答后日志区域下移约 56px。修复后中英文 1280/820/390px 的初始读取与切换坐标保持一致；未读/已读日志、真实读取进度、失败重试、RX 确认、来源切换、导出和清空提示均已用 Mock 检查。页面坐标比较精度为 0.01px。`typecheck` 和 `test:logs` 的 40 项通过；未连接实机。

## 权限同步与兼容性

主控通过 BoardLink/SPI 同步两位权限到 TX，TX 在下一次 RF CONNECT 同步给 RX。TX/RX 上电从关闭开始。RX 断开 RF 时撤销监控权限，但保留日志权限以记录已授权的断链事件；下一次 CONNECT 更新两项权限。未连通的 RX 不代表已应用主控的新设置，页面保留“待确认”，可手动读取 RX 状态核实。

RPC `get_diagnostics_policy` / `set_diagnostics_policy` 回应版本 2，包含 `loggingEnabled`、`monitoringEnabled` 与三端状态；写入必须同时提供两个布尔值，保存失败回滚，不把超时当作成功。BoardLink/SPI 写入使用 `0x80 | flags`，旧版布尔协议会拒绝。状态回应为版本 2 与原始两位 flags。RF CONNECT 在 CH_B 的低六位保留频道、高两位承载权限；OPTIONS 的频道 capability 位不变。旧版 RX 在任一权限开启时不能接受该扩展，必须配套更新三端固件。旧 RPC/状态版本不可显示为已生效。

协议定义见 [diagnostics_policy.h](../common/diagnostics_policy.h)。读取 RX 权限不进入历史日志读取所需的无线暂停流程。

## 关闭边界与验证

必要的输入序号、收发/ACK、队列时效、中立报告、看门狗、基础运行时钟和连续崩溃保护继续运行。两个开关关闭时，诊断工作被运行时门禁跳过；少量判断指令仍存在，不能把它描述为绝对零周期，也不能用停止采样后的空图表宣称零丢包。

定向主机检查覆盖四种组合、两域切换互不重置、日志参数不求值、禁止新增记录和自动写入、历史保留、连续崩溃保护、RPC 保存失败与旧版本拒绝。三端源码和编译检查须覆盖新增协议的生产者/消费者。Mock 页面用于检查组合切换、刷新持久化、错误状态、键盘操作及中英文窄屏布局。

本次不烧录、不部署、不运行暂停中的 RF/监视器自动回归或设备采样；主机检查不替代实机性能验收。主控 RAM 空间紧张，须检查正式无锁槽位构建与 ELF 布局。

## 本次主机验证（2026-10-26）

- `python -m unittest tools.tests.test_diagnostics_policy tools.tests.test_fault_log_rpc tools.tests.test_webhid_command_manifest`：8 项通过；独立策略、保存失败及命令清单检查。
- `python -m unittest tools.tests.test_fault_log_layout`：3 项通过；只读本地 ELF。主控 AXI 剩余 196 字节，D2 剩余 7904 字节；诊断缓冲放入已有 D2 区域，队列先完整写入再发布。
- WebConfig 策略解析测试：2 项通过；类型检查、Hosted 构建和产物隔离检查通过。
- Mock 浏览器：四种组合及逐次刷新持久化、Space 键操作、双关闭后读取历史、保存失败后重新读取、旧版本禁用写入、中英文桌面与 390px 窄屏通过。
- `python tools/formal_application_build.py --slot A --jobs 4`：约 101 秒通过，manifest 为 `unlocked-development`；包含 STM32 与 TX 编译。RX 另行 `python tools/hbox.py build rx` 通过。
- Connect Monitor 类型检查、Electron TypeScript 编译、renderer 构建通过；现有 native helper 与构建源文件 SHA-256 一致，无需覆盖正在占用的文件。未重启正在运行的客户端，未生成安装包。

构建仍有编译器、React Hook 和打包体积警告；通过不表示无警告。上述结果不包含暂停中的 RF/监视器自动回归、实机丢包率或时延验收。
