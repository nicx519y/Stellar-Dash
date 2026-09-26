# RF 反复断流复查：SDK 时钟重入（2026-09-19）

## 实机证据与边界

用户截图 10:28–10:30 的原始监控日志已备份到
`.hbox/rf-investigation-20260919/monitor-before-restart.jsonl`。
TX 只读 staging 查询显示 APPLIED / COMPLETE；镜像 SHA-256 是
`c2681505f7e5885648a5e23110f29ca2f2e2dd8b230b0f610f3dc378cb4e1fb6`，与上一包一致。
监控进程自 9 月 18 日运行，旧 parser 丢弃新增 RHD1。备份后重启监控，收到 RHD1：

- RF ready 17ms、USB ready 269ms；累计建链 91 次。
- 最近连接耗时 65535ms（字段已饱和，不能用作有效时延样本）。
- ACK late 130、重复 token 抑制 557、ACK TX watchdog 10。
- 输入待处理丢弃 8357，队列最高 15；边沿队列丢弃 0；CRC 累计 49503。
- 截图区间主要在发现频道 16/39 间变化；原始 RHM1 没有 hop start/finish，
  因此不能把图上密集的频道事件全部解释成智能跳频。

以上证明存在真实断流和软件积压，且 RX 支持上一版诊断；不能据此声称 CRC 全由软件引起。
重启监控之后的初始采样段未收到 DATA/CRC 增量，无法进行新固件双端实机对照。

## 已确认的代码缺陷

对实际链接的 WCH SDK 反汇编确认：`TMOS_GetSystemClock()` 调用 `pfnTimerCBs`，
再将返回的增量累加到 `gTmosPara`。回调 `clockGetTickValve()` 还会更新上次硬件时刻；
如果当前值小于上次值，它按硬件计数回绕处理。它是带共享状态的更新操作，不是纯读取。

应用此前在主循环、RF 回调、TMR 和 SPI 路径共调用该 getter 67 次（静态调用点）。
主循环读取硬件值后被 RF ISR 抢占，ISR 更新上次值，主循环恢复后可能把旧值误认成
计数器回绕，破坏 scheduler 时钟。只在外层 getter 的调用者局部加锁不能覆盖 SDK
主循环内部的取时，也不能从根本上消除这个重入点。

这一缺陷可以导致异常的无符号时间差、假超时和反复恢复，且每包调用 SDK 中的
64 位除法增加 8K 回调开销。它与实机时间字段饱和/频繁重连一致，但仍需新版对照
才能确认它对实际 CRC、丢包和吞吐率的贡献，不能预先宣称已消除所有断流。

## v2 修复

- `rf_link_clock.h` 提供 RF/SPI 专用单调时钟；读取已运行的 SysTick CPU 计数，
  短临界区维护本地累积量，以 625µs tick 保持协议现有参数单位。
- TX/RX APP 全部 getter 调用改用 `RF_LinkClockNow()`，不再重入 SDK 时钟。
  时钟状态由每端唯一的实现共享，SPI 事务与 RF 状态机使用相同时间域。
- 不改写 SysTick 配置、不改 SDK、不修改烧录/IAP、配对信息、速率或超时门限。
- RX RHD1 page 0 的 30..31 字节新增 `rfBuildId=0x1902`；0/缺失表示上一版。
  新 RF/USB ready 时间以 SysTick 启动为基准，与旧 SDK epoch 不应直接混比。
- 主循环包含 RF Off 状态持续维护计时；假定单次停顿不超过 SysTick 的约 71 秒
  （60MHz），当前禁止深度 Standby 的运行模式满足该约束。

## 验证

23 项 Python/原生 C 与契约测试通过，包含百万次变步长硬件计数回绕、tick 回绕、
主循环/ISR 交错以及嵌套临界区保存测试；9 项 monitor 测试和类型检查通过。
源码检查禁止应用再次调用 SDK getter。固定/正式配对的 TX/RX 构建分别检查。

下一步必须使用匹配的 v2 TX/RX 做对照：新 RHD1 应显示 `rfBuildId=6402`，
建链耗时不再异常饱和；再测连接次数、ACK late/watchdog、CRC 和队列丢弃的增量。
仍需 8K 两小时、100 次恢复和 P95 指标验收。此前固件不能标记为稳定通过。

## TX 更新记录

已执行唯一日常入口 `python tools/hbox.py flash tx`，QSPI staging 写入/校验完成，
最终 APPLIED / IAP COMPLETE / 81692 字节 Application 进度 100%。完整工具镜像 SHA-256：
`875a6304a8f7a5b3e49a6c062e470a5dca000e3e0d2e154c8576930cc4a7b908`。
TX 4KB IAP 与改动前相同，未修改任何保护位或锁定状态。RX v2 留给用户自行烧录，
交付目录为 `.hbox/rf-clockfix-v2-20260919/`；两端匹配后的实机验证仍未完成。
