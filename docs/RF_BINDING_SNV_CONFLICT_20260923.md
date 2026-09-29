# WebConfig 配对后无线 Connecting：SDK SNV 存储冲突

## 现象与证据

- 用户在 WebConfig 完成配对，界面显示已配对；切到 RF 后 connect-monitor 一直 Connecting。
- 已有监视器日志显示 RX 0x1934 的 USB telemetry 正常，`CA`、输入计数 0、远端 TX 诊断无效。`CA` 仅表示已有绑定但未连接，不能当作已经收到 CONNECT 的证据。
- 用户切回 WebConfig 后，RX 身份 `0x5A29CCF3`、地址 `0x60D8BB95` 仍存在，但变成 “Not paired with this HBox”。
- 配对 journal 固定使用 Data Flash 0x6000/0x7000；首次写入刻意选择 bank B (0x7000)，保留 bank A 的旧记录。
- WCH SDK `BLE/HAL/include/CONFIG.h` 默认 `BLE_SNV=TRUE`，`BLE_SNV_ADDR=0x77000-FLASH_ROM_MAX_SIZE`；本芯片 `FLASH_ROM_MAX_SIZE=0x70000`，结果也是 **0x7000**。
- SDK `MCU.c:CH58x_BLEInit` 向库注册 `Lib_Read_Flash/Lib_Write_Flash`。TX 维护模式不初始化 BLE 库，切 RF 后才初始化，因此网页提交回读成功不能证明该记录能跨 RF 启动保留。

这是明确的 Flash 所有权冲突，与切换模式后丢失绑定的实机现象一致。未读取 CH585 原始 Data Flash 来确定具体被改写字节。

## 修复

这两套固件运行专用 RF 协议，不使用 BLE 配对持久化。通过 `RF_PHY_Hop/Common/include/CONFIG.h` 和 `HAL.h` 的 SDK 包装头关闭 BLE SNV；`HAL.h` 包装覆盖 SDK MCU.c 首先包含 HAL.h、再从 SDK 同目录读取 CONFIG.h 的路径。配置不允许显式重新开启 BLE SNV。

- 不移动 0x6000/0x7000，不改 IAP、Code Flash 地址布局或烧录流程。
- 不修改 SDK 源文件；保留 BLE/RF 库初始化、时钟及高速收发实现。
- RX 诊断 build ID 升为 0x1935。
- 新增包装头会改变头文件搜索结果，首次必须完整重编译（本次 TX/RX 均使用 `make -B`）。

## 验证与产物

- 5 项定向测试通过：SDK CONFIG/HAL 两条包含路径、显式开启 SNV 拒绝、配对存储故障注入、维护模式门禁和冻结烧录契约。
- TX/RX 完整构建通过。检查最终 ELF，均不存在 `Lib_Read_Flash`、`Lib_Write_Flash` 回调；构建前两者均存在。
- TX combined BIN：106836 bytes，SHA256 `b2e5e9e7e82d0d5fc311ea3ffc66154520834422f01ea3f81d4a775d6b919c41`。
- RX BIN：69312 bytes，SHA256 `6a02584d10ba3bd33f6c1daf0c64e4aca738aa876258b9e53516666b67c67645`。
- 固件与日志：`.hbox/rf-binding-snv-fix-20260923/release/`、同级日志。
- 板载 TX 已通过固定入口 `python tools/hbox.py flash tx` 更新，回读 `APPLIED / COMPLETE`，generation `1790104473`、进度 `102740 / 100%`，完整镜像 SHA256 匹配。只写 TX 应用区，未修改任何保护位或锁定状态，IAP 未改动。
- RX 尚未烧录，由用户通过既有 RX 更新方式写入上述 0x1935 固件；无需重刷 STM32 或修改 monitor。

## 恢复与验收

更新 TX 和 RX 后，再在 WebConfig 配对一次。确认两端已提交后切 RF，观察 connect-monitor Connected 与输入计数；再次重启两端、回到 WebConfig，应仍显示同一地址和已配对。被旧 SDK 覆盖的 TX 记录无法仅靠更新固件自动恢复。

本次只读取既有监视器日志，没有启动额外 HID 采样器。完整实机验收仍以更新、重新配对及重启后的用户结果为准。
