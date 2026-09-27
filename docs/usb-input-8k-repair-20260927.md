# XORA USB 输入高速模式修复（2026-09-27）

## 现象与证据

用户确认 USB 1/2/4K 按键能响应，8K 起初无响应；第一轮公平调度修复后，用户也确认 8K 档能操作。设备侧检查发现，仅凭菜单显示 8K 或按键能用，不能判定高速输入已稳定：STM32 请求 8000 Hz，但发生 TX 接收队列溢出后，现有恢复流程会关闭快速输入并回到 SPI /256。

独立、按缓存行发布的 `g_usb_input_fault` 记录首次快速输入溢出。实机捕获 `status=9`、`detail=0x8303`：原因 3 是软件接收环形队列空间不足；DMA 新增数据分别只有 14 或 84 字节，不能归因为 DMA 游标虚假回绕。高速探测阶段为 4，说明握手和最大长度探测已成功，失败发生在后续持续输入。

临时分段耗时统计显示，主要开销在输入帧解析和报告生成。反汇编还确认：只把函数放入 `.highcode` 并不足够，编译器生成的 switch 跳转表、CRC 表及调用的部分函数仍位于 Flash。临时高频耗时采样已从最终版本移除。

## 修改

- USB 输入角色每次最多处理一个 64 字节片段，然后返回主循环，让 USB 上报和控制事件得到处理机会。解析状态跨片段保留；WebConfig 维护角色沿用原有处理边界。
- TX 输入解析、DMA 收包、XInput 报告生成等热路径使用 SRAM。目标对象禁用 Flash 跳转表，输入 CRC 使用 SRAM 中的 16 项半字节表；保留两处校验，算法与线上 CRC-8 完全相同。
- 快速输入不再为每个 14 字节输入帧执行 NSS 上升沿收包中断；主循环与 RX→TX 仲裁前收集 DMA，保留 DMA 回绕和发送完成中断。
- 快速输入准备操作最多尝试三次，仍要求 SPI 切换后的 nonce/CRC 探测通过，不能用重试跳过能力或探测检查。
- TX 首次溢出的原因和 DMA 进度通过 FAULT 事件的附加诊断字段传回 STM32；原有前两字节含义不变。STM32 RAM 记录不包含按键内容或密钥。

不能仅凭这些证据把回归归到某一个历史提交；本次没有做全链路二分烧录。

## 构建与检查

- `python -m unittest tools.tests.test_usb_input_fairness tools.tests.test_webhid_startup tools.tests.test_webhid_fast_link`：9 项通过，约 6 秒。覆盖持续生产者让出执行、片段解析、默认/TX SRAM 编解码、CRC 对照、有限重试与探测门控、WebConfig 启动及高速窗口协议。
- CRC 对照覆盖 1024 组固定种子数据，每组长度 0–255；另检查空指针处理。
- 独立编译执行 `usb_profile_migration_test.cpp`、`usb_board_link_dma_math_test.c`：通过，约 1.5 秒。
- `python tools/hbox.py web local-build --unlocked-development --slot A --skip-web --jobs 4`：通过，约 55 秒；包含 STM32 完整槽位产物与 TX 构建。
- 最终 TX ELF/map 确认解析、CRC、DMA 收包与报告生成位于 `0x2000....` SRAM，CRC 表位于 `0x20006e90`。静态 RAM 占用 125500/131072 字节，`_ebss=0x2001ea40` 至栈顶约剩 5.4 KiB；尚未测量栈高水位，不把链接成功当作完整内存压力验收。
- `usb_webhid_flow_control_test.c` 未通过编译：测试调用的 `usb_device_webhid_credit_ready` 已不在当前头文件中；测试和头文件均与本轮开始的 HEAD 一致。未通过删除断言或恢复旧接口消除失败，因此不宣称全量回归通过。
- 未运行 RF 自动回归或 RF 设备采样；未修改冻结烧录契约及哈希。

## 最终部署与验收边界

STM32 Application 槽 A：371596 字节，SHA-256 `9bbeb1f0bc20c9820fdb9b5e0e68d638e79579638545790a2903cb47f892cdf8`。

TX 包 SHA-256 `1e0e78dd4a75424f1c7b4ec17c8853df6779de08a3057c0b9e7f1419d53e9eb7`，Application 105372 字节，IAP 前缀逐字节一致；既有入口只更新 0x1000 以上 Application。

使用 `python tools/hbox.py flash app A`、`python tools/hbox.py flash tx`，复用已校验产物。STM32 回读与 metadata 最后提交、TX APPLIED/COMPLETE 结果以本地 `.hbox/webhid-hs/usb-input-final-*-flash.log` 为依据。

最终运行快照另记在本目录的 `usb-input-8k-evidence-20260927.json`。最小 Cortex-M 只读 SRAM/外设读取不 halt、不设置断点，不访问保护寄存器。普通复位仅用于启动重复性检查。

最终版三次普通复位启动均得到 `stage=4`、首次快速输入溢出标记为 0、硬件 SPI CFG1 显示 /16（7.5 MHz）。第一轮两个运行快照相隔约 90 秒，第二轮相隔约 54 秒，均保持高速。普通缓存对象中的历史 `last_fault=9` 仍可读到，不能据此宣称所有状态错误已清零；本次判断采用主动清缓存的首次溢出记录和硬件时钟寄存器。

高速保持、无新增溢出记录与 Windows 按键响应是不同证据。此次未测量 Windows 每秒实际收到的 USB 报告数量，未验收完整端到端 8000 reports/s 或输入延迟分布；也未把 USB 输入验证替代 WebConfig 端到端性能验收。

最终三次启动检查后，用户在 USB 8K 输入模式实按按键，明确回复“能正常响应”。这项功能确认针对上述最终烧录版本。

未修改任何保护位或锁定状态。
