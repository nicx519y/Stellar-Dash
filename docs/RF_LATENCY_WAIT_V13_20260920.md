# 延迟等待路径修复 v13

本轮按代码分析修改 STM32、TX、RX，已编译，没有烧录、设备读取或运行回归测试。
实机验证由用户完成。RX build ID 为 `0x1913`。本版本包含 v12 的源记录修复。

## SPI wait 约 52ms

STM32 `rf_read_event_frame()` 原先对较长回复逐字节执行 `HAL_Delay(1)`。
状态回复原为 28 字节，因此一次读取就包含二十多次毫秒级等待；HAL 的毫秒 tick
相位还会影响每次等待的实际长度。`ConnectionManager::onReportReady()` 在发送输入
之前调用事件服务，输入已经 ready，却要等状态回复读完，因此这段时间计入 SPI wait。
52ms 的量级有直接的代码来源，不是 14 字节输入 SPI 传输本身需要这么久。

只删除 Delay 会让 TX 的八字节 FIFO 来不及由主循环补充，造成新的传输错误。
本次 TX 改成持久、四字节对齐的 DMA 回包缓冲；等物理字节计数结束 CNT_END 中断
才恢复 RX DMA 并撤销 ready，不用提前到达的 DMA_END 当成发送完成。
STM32 从有效状态回复的 byte 24 `0xD1` 确认能力后，取消逐字节毫秒等待。
未确认能力的旧 TX 仍使用原节奏，所以首次能力协商可能仍有一次慢读取。

另一条阻塞是周期性 GET_STATUS 的同步事务：TX 延后约 10ms 发 ACK，STM32 同步等候。
新能力下周期查询改为现有 GET_STATUS 的空 payload 只读形式：STM32 发完即返回，TX
只回一份状态，后续由普通事件服务读取。启动及修改配置等事务仍保留原事务流程。
状态扩展仅增加一个 SPI 字节；RF 输入长度和空口诊断调度没有改变。

## TX 7ms 以上

TX 列是同一有效发送尝试的“SPI 结束到 RF 启动”，不是 RF 空口发送时长。
代码确认两个需要处理的路径：

1. ACK 的硬件接收窗通常是 1200us，但原软件兜底统一等待 20ms；回调丢失后，
   `g_demo_ack_rx_active` 会持续使 8K 发送 ISR 跳过输入。现改为该次实际接收窗
   加 250us 回调余量，用 SysTick 周期计时。主循环检查另有最多一个 housekeeping
   调度周期的延后；这不是绝对 1450us 的实测上限。
2. NSS 缓存原只按 8 位 SPI 序号查找，并允许 8ms 内时间戳。现要求事件 tag 和
   SPI 序号同时匹配，避免取另一个事件的边界。已有 v12 的 NSS 主动回填仍保留。

不能仅凭 7.49ms 或 7.24ms 认定上述哪条路径发生了。主循环解析积压、RF 接收窗口、
射频控制事务仍可能带来真实等待；本轮没有声称所有 TX 尖峰已经消失。

## RX / USB 毫秒级等待

`DONGLE_USB_FORCE_FULLSPEED=1` 原先强制全速。FS 的 EP2 描述符间隔为 1ms，
已有 HS 描述符则为一个微帧。现恢复高速协商；物理连接只支持 FS 时仍有全速限制。
RX 必须拔插 USB 重新枚举才会改变总线模式。主机实际调度仍由主机决定。

原 RX 只比较 FIFO 最后一个元素来合并重复输入。一个状态移到 prepared 或 USB
in-flight 后，后续相同 RF 状态又能入队，形成“已提交的重复报告 + 等待提交的重复
报告 + 新按键边沿”。这会让 RX 和 USB 阶段分别累加不必要的排队时间。

现记录最后已接纳的状态，跨 FIFO、prepared、in-flight 去重。不同按下/松开仍按
顺序入队；重复输入继续刷新 50ms 中立保护的活跃时间。无线 generation 改变、
中立复位和 USB 重枚举允许重新提交当前状态，避免去重导致重连后保持旧值。
测量事件 tag 一并参与比较，以支持新测量会话。

界面目前 RX=RF 接收边界到 report-ready；USB=report-ready 到 IN complete，
后者包括等待端点空闲和主机取走数据，不是单独的 USB 线速传输时长。
没有删减测量区间、减去常数或把缺失阶段当作零值。

## 更新

目录 `.hbox/rf-latency-v13-20260920/`：

- STM32：`application-slot-a.bin`，配套 `metadata.bin`，使用原有无锁流程提交。
- TX：原入口 `python tools/hbox.py flash tx`；快照同时包含 `TX.bin` / `TX_app.bin`。
- RX：用户自行烧录 `RX.hex`，然后拔插接收器。
- SHA-256：`SHA256SUMS.txt`。

STM32 原入口为 `python tools/hbox.py flash app A`。这些命令使用仓库当前构建产物。
本轮没有修改烧录脚本、IAP 布局、任何保护位或锁定状态。不是已完成实机验收的稳定版。
