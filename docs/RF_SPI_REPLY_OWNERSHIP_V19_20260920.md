# SPI 回包 ready 生命周期修复 v19

用户反馈 v18 仍在开启延迟测量后按键停更，但丢包率看起来有所改善。
本轮按用户要求只分析代码、修改及编译，没有设备采样、回归测试或烧录。

## 确认的代码缺陷

`rfm_spi_port_try_write()` 原先装载回包 DMA 并打开 CNT_END 完成中断后返回，
三个调用方才分别调用 `rfm_spi_port_set_irq(true)` 通知 STM32。
而 CNT_END ISR 可立即执行 `spi_tx_finish()`，恢复 RX DMA、清除 tx_pending
并撤销 W_INT。两者之间没有同一个事务边界。

一条允许发生的交错为：

1. TX 检查 NSS 空闲，开始切换 SPI 回包；STM32 刚检查过没有待读事件，准备发输入。
2. STM32 的 SPI 时钟消耗了回包计数；短回包的 CNT_END 抢占 TX 主循环。
3. ISR 恢复 RX，设置 tx_pending=0，撤销 W_INT。
4. 原调用方恢复运行，重新断言 W_INT，此时已没有回包。
5. STM32 的 SendInputLatest 看到 W_INT 后拒绝输入并转而读事件。
   TX 因 tx_pending=0 不会进入 2ms 回包超时恢复，故这条路径本身无法撤销假 ready。
   独立运行的 RF 定时器仍可持续发送旧按键快照，表现为接收率正常而按键停更。

开启测量使用的 SPI_EVT_TIME_SYNC 实际只携带一个 enable 字节，总长 5 字节，
是经过此路径的主动短通知。这是静态代码可确认的竞态，与报告的现象相符；
没有现场状态证据证明它是此次实机问题的唯一原因。v18 的中断策略回退未通过用户验证。

## 修复

- 回包 bytes 先准备，切换前在临界区再次核对 NSS、RX 已消费位置和 pending 状态。
- SPI port 统一拥有 DMA、tx_pending 和 W_INT 生命周期：装载 DMA 后，先断言 W_INT，
  再开启 CNT_END 中断。完成或超时负责恢复 RX 并撤销 W_INT。
- 删除普通事件、命令回执和可靠事件三个调用方在返回后的 ready 断言。
- 删除 process_command 对 W_INT 的无条件清除，命令解析不能代替回包完成管理信号。
- 未改变 RF 调度、CRC 表、包长、速率、频道、保护间隔或丢包计算。

临界区不能阻止外部 SPI 主机开始打时钟，本修改不承诺消除每个总线碰撞；它消除了
“完成以后调用方重新拉起 ready”导致的失效状态。保留既有校验、2ms 超时及状态查询恢复。
STM32 的状态包仍携带捕获开关，单个捕获通知丢失可由后续状态查询补齐。

## 交付

仅 TX 更新，STM32 沿用 v13、RX 沿用 0x1913，monitor 不需更新。
产物在 `.hbox/rf-latency-v19-20260920/`，附 manifest、SHA256SUMS 和编译日志。

使用现有入口 `python tools/hbox.py flash tx`，读取仓库当前 TX 构建产物，
只更新 0x1000 以上 Application。不要用 TX.bin 从零地址覆盖现有 IAP。
本轮没有修改烧录流程、IAP、保护位或锁定状态，没有执行烧录。

构建成功只代表源码能编译，开启测量时的按键响应及丢包表现仍由用户实机验收。
