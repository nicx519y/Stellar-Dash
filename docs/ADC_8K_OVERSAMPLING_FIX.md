# 8K ADC 采样失步修复（2026-09-19）

## 原因与修复

实机 STM32H750 Rev.V（REV_ID `0x2003`）的 ADC 在 45 MHz kernel clock
之后还有硬件二分频。每路 6 通道、32.5-cycle sampling、16-bit conversion
采用 16 倍过采样时，一轮约 175 us（实测含回调约 180 us），超过 8K 的
125 us 周期。采样组装器失步后 INPUT 进入 Fault，关闭 CH585 与灯电源。

- 1K/2K/4K：16 倍过采样，右移 4 位。
- 8K：8 倍过采样，右移 3 位，实测含回调约 92 us。
- 右移同步调整，保持 ADC 输出尺度，继续使用现有行程映射和校准数据。
- `ReportScheduler::setRate()` 停止 TIM2 后，将目标速率传给
  `ADCManager::rearmInputSampling()`。三路 ADC/DMA 全部停止后才重新配置，
  重置采样组装状态、重新挂载 DMA，最后启动 TIM2。
- 配置失败继续停机，不放宽采样健康检查；不改变 ADC 时钟。

## 验证

- 12 项测试通过：采样预算与全范围数值尺度、ADC 组装器、循环 DMA 合约、
  重配顺序和已验收烧录流程完整性。
- 通过现有入口构建并烧录 A 槽 `unlocked-development` 固件，回读校验通过，
  metadata 最后提交；内部 Flash 镜像相同，跳过内部 Flash 擦写。
- 实机分别以 1K/2K/4K/8K 临时 RAM 配置复位启动，正常 RF 初始化会下发对应
  速率；三路 ADC 的 CFGR2 分别为 `0x000F0081` / `0x00070061`，输入持续运行。
- 实机另以 RAM 中的目标采样率验证 4K→8K→1K→8K→2K→8K 调度切换，未发生
  采样失步。此项仅验证 ADC 调度切换，不等同于无线协议速率切换验收。
- 测试后正常复位，恢复原持久配置 8K；未保存临时测试参数。
- 灯电源、CH585 电源和 RF SPI 数据发送恢复。接收器 HID 遥测仍显示
  `CA / Connecting`（已有 bond，等待握手或首个合法 DATA）、无 DATA，因此无线
  端到端连接尚未验收；该状态不等于 recovery scan。USB 物理档未实测。
- 未修改任何保护位或锁定状态，未修改烧录脚本或启用深度 Standby。

本次构建、烧录及实机日志保存在本机 `.hbox/rf-adc-fix/`（不提交日志）。
