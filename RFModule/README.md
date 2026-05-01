# RFModule Firmware (CH584M)

该目录是 CH584M 射频模块固件实现，功能定位：
- 通过 SPI 从 STM32 接收控制命令与按键数据。
- 按 `dongle` 协议打包并通过 2.4G 发送到接收器。
- 内置配对/连接状态机、绑定持久化、跳频、重传和速率控制（1K/2K/4K/8K）。

## 目录结构

- `include/rfm_config.h`：基础参数与速率/功率定义。
- `include/rfm_protocol.h`：无线帧协议定义。
- `include/rfm_link.h`：链路状态机接口。
- `include/rfm_spi_bridge.h`：SPI 命令与事件接口。
- `src/rfm_link.c`：核心状态机实现。
- `src/rfm_spi_bridge_stub.c`：SPI 桥接与命令分发。
- `src/rfm_protocol.c`：协议编解码。
- `src/platform_ch584_stub.c`：时钟/GPIO/定时器/SPI0 从机初始化。

## 状态机

- `IDLE`：空闲，等待配对命令。
- `PAIRING`：周期发送 `ADV_REQ`，等待 `ADV_RSP`。
- `PAIR_OK`：配对成功，完成 bond 落盘后短暂过渡。
- `CONNECTING`：已绑定连接阶段，监听并处理 `CONN_REQ`。
- `CONNECTED`：连接成功，接收 STM32 输入并发送 `INPUT_DATA`。
- `RECONNECTING`：断链重连，扫描信道等待重新建立连接。

## SPI 协议（模块为从机）

基础帧格式（命令与事件统一）：
- `byte0`: `0xA5` 同步字
- `byte1`: `cmd/evt`
- `byte2`: `payload_len`
- `byte3..`: payload
- `last`: checksum8（前面所有字节求和）

STM32 -> CH584 命令：
- `0x01 GET_STATUS`
- `0x02 START_PAIR`
- `0x03 STOP_PAIR`
- `0x04 UNBIND`
- `0x05 SET_RATE`（payload: rateHz LE16，支持 1000/2000/4000/8000）
- `0x06 INPUT_DATA`（payload: 15B，与 dongle 输入负载一致）

CH584 -> STM32 事件：
- `0x81 STATUS`
- `0x82 STATE_CHANGED`
- `0x83 RATE_APPLIED`
- `0x84 LINK_WARN`
- `0x85 ERROR`

中断通知：
- `PB11` 作为 CH584 -> STM32 事件通知线，发送事件后拉高提示主机读取。

## 硬件配置

SPI 引脚（按当前原理图约定）：
- `CS: PB12`
- `SCK: PB13`
- `MOSI: PB14`
- `MISO: PB15`
- `IRQ: PB11`

电源说明：
- 当前固件按“LDO 供电，不启用 DCDC”策略设计。

## 编译

```powershell
cd e:\Works\STM32\HBox_Git\RFModule
make SDK=E:/Works/CH585EVT/EVT/EXAM all
```

产物：
- `build/rf_module.elf`
- `build/rf_module.hex`
- `build/rf_module.bin`

## 对齐文档

- `..\dongle\CH584_DEVICE_RF_PLAN.md`
