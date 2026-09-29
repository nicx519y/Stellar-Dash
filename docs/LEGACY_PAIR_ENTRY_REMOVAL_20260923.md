# 旧配对交互入口移除（2026-09-23）

配对操作已迁移至 WebConfig。本次清理设备屏幕和实体按键触发的旧无线配对入口。

## 检查与修改

- STM32 `spi_screen_detail_tournament_mode.cpp` 原来仍有 `Pair 2.4G` 菜单、等待/成功/错误页和返回时停止配对的逻辑，现已删除。Connection 页保留 USB 和 2.4G 1K/2K/4K/8K，继续由物理开关决定模式。
- TX 没有长按配对按键轮询。实际入口位于 RX `RF_main.c`：PB22 防抖后长按 5 秒调用 `RF_StartPairing`。现已移除这部分 GPIO 初始化、计时逻辑和主循环调用。
- TX/RX 的 `RF_StartPairing` 函数及声明已删除。TX 旧 SPI `START_PAIR` 编号保留，普通请求返回错误，计划执行路径拒绝执行，不再启动旧无线配对。
- WebConfig USB 配对记录事务、存储布局、已提交绑定加载及正常 CONNECT/ACK 未改。旧协议内部解析和历史绑定兼容逻辑不在本次清理范围。

## 验证

- `make -C application -j4 HBOX_SECURE_BOOT_REQUIRED=0 all` 通过。
- `make -C RF_PHY_Hop/TX -j4 all` 和 RX 对应构建通过。
- TX/RX ELF 中不存在 `RF_StartPairing` 和按键配对轮询符号，USB 绑定处理接口仍存在；BLE SNV Flash 回调仍不存在。
- STM32 屏幕目标文件不再含旧配对页文案。
- `tools/frozen_flash_contract.json` 中 7 个文件的 SHA-256 均一致；相关差异检查通过。
- 日志位于 `.hbox/remove-legacy-pair-entry-20260923/`。构建仅有原有链接段 RWX 提示。

遵循用户要求，未刷入任何固件，未采样或运行实机回归。需用户更新 STM32、TX、RX 固件后，硬件上的旧入口才会消失。本次未修改 IAP、烧录流程、保护位或锁定状态。
