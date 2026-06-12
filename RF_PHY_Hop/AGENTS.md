# AGENTS - RF_PHY_Hop 当前结论

本文件只保留当前 `RF_PHY_Hop/` 调试阶段对后续协议设计有用的结论。旧的跳频、配对、多速率历史设计不要作为当前实现依据。

## 当前测试前提

- 固定频道：`channel 16`
- 固定速率：`8K`
- 周期基准：`125us`
- 当前测试逻辑：TX 作为 master，在正向包里携带 master tick；RX 用该 tick 对齐自己的反向/ACK 发送窗口。
- TX 侧反向窗口必须持续监听：进入窗口 arm RX，收到包或坏包回调后 rearm，直到窗口结束。
- 不要每 `125us` 强制 `RFRole_Stop() + RFIP_SetRx()`；这会打断正在接收的包，明显降低接收率。

## 实测窗口扫描

TX master 同步后，自动扫描反向/ACK 窗口长度。理论包数按 `窗口时间 / 125us * 5s` 估算。

| 反向/ACK 窗口 W | 理论 RX 包数/5s | TX 实收 | 接收率 | 结论 |
|---:|---:|---:|---:|---|
| `100ms` | `4000` | `3264` | `82%` | 可用 |
| `50ms` | `2000` | `1586~1606` | `79~80%` | 可用 |
| `25ms` | `1000` | `737~826` | `74~83%` | 相对可用 |
| `12.5ms` | `500` | `325~329` | `65~66%` | 勉强可用 |
| `10ms` | `400` | `208~211` | `52~53%` | 丢包明显 |
| `8ms` | `320` | `129~135` | `40~42%` | 丢包高 |
| `6ms` | `240` | `75~82` | `31~34%` | 仅部分可用 |
| `4ms` | `160` | `11~37` | `7~23%` | 边缘且波动大 |
| `3ms` | `120` | `0` | `0%` | 基本不可用 |
| `2ms` | `80` | `0~1` | `0~1%` | 基本不可用 |

结论：

- 长窗口证明 RX -> TX 方向硬件和 RF 链路可用；同步后 `100ms` 窗口曾达到约 `3689/4000`，约 `92%`。
- 短 ACK 窗口完全收不到，不是频点、access address 或包格式的根本问题。
- 当前手动 RF API 路径下，反向/ACK 窗口小于 `4ms` 基本不可用。
- `6~10ms` 只能证明能部分收到，不适合作为可靠 ACK 目标。
- `12.5ms` 可用但仍明显丢包。
- 可靠协议建议先用 `20~25ms` ACK 窗口起步，再逐步压缩。

## CH58x TX/RX 切换结论

SDK `wchrf.h` 对 auto 模式的 RX -> TX 切换给出公式：

```text
RX -> TX sendTime = N * 0.5us + 24us
```

当前使用的 `RFH_TX_SEND_TIME_UNITS = 40`，理论 auto RX -> TX 延时：

```text
40 * 0.5us + 24us = 44us
```

注意：

- 这个 `44us` 是 CH58x RF 库 auto mode 的 RX -> TX ACK 延时参数。
- 它不是手动 `RFRole_Stop() -> RFIP_SetRx()/RFIP_SetTx*()` 的完整耗时。
- 当前实测说明：手动 Stop/SetRx/SetTx 路径做 125us 级短 ACK 不稳定，需要毫秒级窗口才可靠。

## Auto ACK 含义

`auto ack` 指 RF 硬件/库在收到包后自动完成 ACK 方向切换和发送，不经过应用层手动 Stop/SetTx。

手动 ACK 路径：

```text
RX 收到 DATA
-> 回调/中断
-> 应用层判断
-> RFRole_Stop()
-> 填 ACK
-> RFIP_SetTxStart()
-> RFIP_SetTxParm()
```

auto ACK 路径：

```text
RX 收到 DATA
-> RF 库按 sendTime 自动切 TX
-> 自动发 ACK
-> 回调 RX_MODE_TX_FINISH / RX_MODE_TX_FAIL
```

TX 侧也应优先研究 auto wait ACK 模式：

```text
TX 发 DATA
-> RF 库自动切 RX 等 ACK
-> 收到 ACK 回调 TX_MODE_RX_DATA
-> 超时回调 TX_MODE_RX_TIMEOUT
```

后续若要做短 ACK，优先切到 CH58x auto ack / auto wait ack 模式，不要继续用应用层手动 Stop/SetRx/SetTx 去硬塞 `125us` slot。

## 官方库实现状态

当前 SDK 默认路径：

```text
E:/Works/CH585EVT/EVT/EXAM/BLE/LIB
```

结论：

- `LLE_MODE_AUTO` / `RF_Tx()` / `RF_Rx()` / `RF_Config()` 依赖 WCH 官方 `CH58xBLE` 库实现。
- SDK 中没有看到这些 RF auto mode 函数的 `.c` 源码；实现封装在预编译静态库中：
  - `libCH58xBLE.a`
  - `libCH58xBLE_PERI.a`
- `CH58xBLE_LIB.h` 中是 `extern` 声明；`CH58xBLE_ROM.h` 中也有跳转表宏形式，说明部分配置/版本可走 ROM 库入口。
- 当前 `RF_PHY_Hop/TX` 与 `RF_PHY_Hop/RX` Makefile 链接的是 `-lCH58xBLE`。

已用 `riscv32-wch-elf-nm` 确认 `libCH58xBLE.a` 中存在可链接实现，不只是头文件声明：

```text
libCH58xBLE.a:rf_fh.o:    T RF_Config
libCH58xBLE.a:rf_fh.o:    T RF_Rx
libCH58xBLE.a:rf_fh.o:    T RF_Tx
libCH58xBLE.a:rf_fh.o:    T RF_Shut
libCH58xBLE.a:rf_basic.o: T RFIP_SetTxParm
libCH58xBLE.a:rf_basic.o: T RFIP_SetTxStart
libCH58xBLE.a:rf_ip.o:    T RFIP_SetRx
```

`T` 表示符号在库目标文件的 text/code 段中有定义。`libCH58xBLE_PERI.a` 也包含同名 RF/RFIP 符号。

因此后续研究 auto ACK 时，应把官方库当作黑盒状态机来实测：

- 能配置和调用 `LLE_MODE_AUTO + RF_Tx/RF_Rx`。
- 能通过 `TX_MODE_RX_DATA` / `TX_MODE_RX_TIMEOUT` / `RX_MODE_TX_FINISH` / `RX_MODE_TX_FAIL` 观测行为。
- 不能直接阅读内部源码确认时序细节，必须通过 GPIO/日志/逻辑分析仪验证。

## Auto ACK 最小 demo

当前 `RF_PHY_Hop/TX/APP/RF_PHY.c` 与 `RF_PHY_Hop/RX/APP/RF_PHY.c` 默认启用：

```text
RF_AUTO_ACK_DEMO_ENABLE = 1
```

demo 目标：只证明 WCH 官方库 `LLE_MODE_AUTO + RF_Tx/RF_Rx` auto wait ACK / auto send ACK 路径可用，不混入现有手动 ACK 窗口状态机。

### 必需调用链

main 初始化顺序必须包含 `RF_RoleInit()`，且顺序与 WCH 官方 `RF_PHY` auto mode 例程一致：

```c
CH58x_BLEInit();
HAL_Init();
RF_RoleInit();
RF_Init();
```

当前 RX 因为还要初始化 USB，顺序为：

```c
CH58x_BLEInit();
HAL_Init();
RF_RoleInit();
RF_USB_CompositeInit();
Main_Circulation();
```

注意：

- `RF_Config()` 返回 `0` 不等于整条 RF auto mode 链路一定可用。
- 如果缺少 `RF_RoleInit()`，实测现象是 RX 可以 `RF_Rx()` arm 成功并保持 active，但完全没有 `RX_MODE_RX_DATA` 回调；TX 则 `TX_MODE_TX_FINISH` 后持续 `TX_MODE_RX_TIMEOUT`。

### 配置与启动

固定参数：

- `channel 39`
- `1M PHY`（`LLE_MODE_AUTO`，先完全贴近官方 `RF_PHY` auto mode 例程）
- `accessAddress = RFH_LINK_ACCESS_ADDRESS_DEFAULT`
- `CRCInit = 0x555555`
- DATA/ACK packet type：`0xFF`（广播/接收全部类型，先绕开官方库 type 匹配规则）
- payload 长度：`12B`
- TX 侧约每 `1ms` 调一次 `RF_Tx(data, 12, 0xFF, 0xFF)`
- RX 侧常驻 `RF_Rx(ack, 12, 0xFF, 0xFF)`，收到 DATA 后由官方库自动回 ACK

TX 初始化：

```c
static void RF_AutoDemoStatusCallBack(uint8_t sta, uint8_t rsr, uint8_t *rxBuf);

rfConfig_t rf_config;
memset(&rf_config, 0, sizeof(rf_config));
rf_config.accessAddress = RFH_LINK_ACCESS_ADDRESS_DEFAULT;
rf_config.CRCInit = 0x555555;
rf_config.Channel = 39;
rf_config.Frequency = 2480000;
rf_config.LLEMode = LLE_MODE_AUTO;
rf_config.rfStatusCB = RF_AutoDemoStatusCallBack;
rf_config.RxMaxlen = 12;
RF_Config(&rf_config);
```

TX 发送：

```c
RF_Tx(tx_buf, 12, 0xFF, 0xFF);
```

参数含义：

- 第 3 个参数是 TX packet type。
- 第 4 个参数是 auto wait ACK 时要接收的 ACK packet type。
- `0xFF` 为广播/接收全部匹配类型；当前先用它绕开 packet type 规则。

RX 初始化与 arm：

```c
static void RF_AutoDemoStatusCallBack(uint8_t sta, uint8_t rsr, uint8_t *rxBuf);

rfConfig_t rf_config;
memset(&rf_config, 0, sizeof(rf_config));
rf_config.accessAddress = RFH_LINK_ACCESS_ADDRESS_DEFAULT;
rf_config.CRCInit = 0x555555;
rf_config.Channel = 39;
rf_config.Frequency = 2480000;
rf_config.LLEMode = LLE_MODE_AUTO;
rf_config.rfStatusCB = RF_AutoDemoStatusCallBack;
rf_config.RxMaxlen = 12;
RF_Config(&rf_config);

RF_Rx(ack_buf, 12, 0xFF, 0xFF);
```

参数含义：

- 第 1 个参数 `ack_buf` 是 RX 收到 DATA 后要自动发出的 ACK payload。
- 第 3 个参数是 RX 要接收的 DATA packet type。
- 第 4 个参数是自动发出的 ACK packet type。
- 当前 demo 在 `RX_MODE_TX_FINISH` / `RX_MODE_TX_FAIL` 后由主循环重新调用 `RF_Rx()` arm 下一次接收。

### 回调处理

预期回调：

- TX：`TX_MODE_TX_FINISH` 后自动 wait ACK
- TX：收到 ACK 走 `TX_MODE_RX_DATA`
- TX：ACK 超时走 `TX_MODE_RX_TIMEOUT`
- RX：收到 DATA 走 `RX_MODE_RX_DATA`
- RX：自动 ACK 发完走 `RX_MODE_TX_FINISH`
- RX：自动 ACK 失败走 `RX_MODE_TX_FAIL`

回调约束：

- 回调在 RF/中断上下文触发，不要在回调内直接重新 `RF_Rx()` 或执行复杂逻辑。
- RX demo 在回调内只置 `g_demo_rearm_pending = 1`，然后在 `RF_Service()` 主循环中重新 arm RX。
- `rsr == 0` 表示收到的包 CRC/type OK。
- `rsr bit0` 表示 CRC error。
- `rsr bit1` 表示 packet type mismatch。

### 日志与实测结果

日志：

- TX 串口每 5s 打印 `TA ...`：
  - `cfg=0` 表示 `RF_Config()` 成功
  - `start` 为发起 `RF_Tx()` 次数
  - `fin` 为 `TX_MODE_TX_FINISH`
  - `ack` 为 `TX_MODE_RX_DATA`
  - `tout` 为 `TX_MODE_RX_TIMEOUT`
- RX CDC/日志每 5s 打印短格式 `RA c0 m01 r0/0 d0 a0/0 e0/0 v1`：
  - `c0` 表示 `RF_Config()` 成功
  - `m01` 表示当前 `LLEMode`
  - `r` 为 `RF_Rx()` arm 次数/失败次数
  - `d` 为 `RX_MODE_RX_DATA`
  - `a` 为 `RX_MODE_TX_FINISH/RX_MODE_TX_FAIL`
  - `e` 为 CRC/type 错误
  - `v` 为当前 RX active

已跑通日志示例：

```text
RA c0 m01 r323/0 d322 a322/0 e0/0 v1
RA c0 m01 r324/0 d324 a324/0 e0/0 v1

TA cfg=0 ch=39 mode=01 start=331 fin=330 ack=311 tout=14 fail=0 crc=5 type=0 busy=1
TA cfg=0 ch=39 mode=01 start=331 fin=330 ack=314 tout=12 fail=0 crc=4 type=0 busy=1
```

解读：

- RX 每 5s 约收到 `319~325` 个 DATA，并自动 ACK 成功 `319~325` 个，ACK fail 为 `0`。
- TX 每 5s 约发起 `331` 次，收到 ACK 约 `310~314` 次，timeout 约 `12~17` 次，ACK CRC 错约 `3~5` 次。
- 这已经证明官方库 `LLE_MODE_AUTO + RF_Tx/RF_Rx` auto wait ACK / auto send ACK 路径可用。

当前成功率粗算：

```text
ack_ok / (ack_ok + timeout + crc_error) ~= 94%
```

构建：

```bash
make -C RF_PHY_Hop/TX
make -C RF_PHY_Hop/RX
```

回到旧手动 ACK 实现：

```bash
make -C RF_PHY_Hop/TX clean
make -C RF_PHY_Hop/TX EXTRA_DEFINES=-DRF_AUTO_ACK_DEMO_ENABLE=0
make -C RF_PHY_Hop/RX clean
make -C RF_PHY_Hop/RX EXTRA_DEFINES=-DRF_AUTO_ACK_DEMO_ENABLE=0
```

### 后续参数回归顺序

已经证明库路径可用后，参数回归不要一次全改，建议按顺序逐项验证：

1. 保持 `channel 39 + 1M + 0xFF/0xFF type`，把 TX 周期从 `1ms` 压到 `500us`、`250us`、`125us`。
2. 切回目标 `channel 16`。
3. 打开 `2M PHY`，即 `LLE_MODE_AUTO | LLE_MODE_PHY_2M`。
4. 测试非 `0xFF` packet type，例如 `0x01/0x02`。
5. 最后再把 ACK payload 设计接回正式协议字段。

## 后续协议建议

- TX 作为 master，同步 tick 必须进入正向 DATA 包。
- RX 的 ACK/反向窗口必须由 TX master tick 推导，避免双方独立本地定时硬碰。
- ACK 窗口先用 `20~25ms`，确认可靠后再压缩。
- 若目标是亚毫秒 ACK，必须研究 CH58x auto ACK 模式。
- 日志中窗口时间要用 `uint32_t` 打印；`100000us` 用 `uint16_t` 会溢出成 `34464us`。

## 常用构建

从仓库根目录执行：

```bash
make -C RF_PHY_Hop/TX
make -C RF_PHY_Hop/RX
```

当前测试产物：

```text
RF_PHY_Hop/TX/build_tx/RF_PHY_Hop_TX.bin
RF_PHY_Hop/RX/build_rx/RF_PHY_Hop_RX.bin
```
