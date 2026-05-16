# STM32 -> CH584 High-Speed SPI Implementation

Date: 2026-05-16

## 目标

本文档说明 STM32 application 到 CH584 RF module 的高速 SPI 数据通道实现。

当前目标是支撑 RF24G `INPUT_DATA` 在 8KHz 采样/发送场景下作为无线模块输入通道使用。重点不是通用 SPI RPC，而是让高频输入帧低延迟、不过度排队、不把 DMA/IRQ 实现细节暴露给上层业务代码。

## 分层边界

### STM32 侧

上层只应通过 RF transport 接口发送输入或控制命令：

```text
ConnectionManager
  -> RFTransport
      -> RFBridgePort_Transfer(...)
          -> SPI4 + DMA + GPIO CS/IRQ
```

公开接口：

- `application/Cpp_Core/Inc/rf_transport.hpp`
- `application/Cpp_Core/Inc/rf_bridge_port.hpp`

`rf_bridge_port.hpp` 只暴露稳定端口 API：

```cpp
bool RFBridgePort_Transfer(const uint8_t* tx, uint16_t txLen, uint8_t* rx, uint16_t* rxLen);
```

以下内容属于 STM32 板级 SPI port 内部实现，不应由业务层直接调用：

- SPI4 初始化
- DMA2 Stream5 TX 初始化
- 手动 CS 拉低/拉高
- IRQ 等待与读回
- DMA busy/pending 状态
- HAL SPI/DMA callback 分发
- SPI/RF bridge 诊断计数

内部 IRQ/callback 声明放在：

- `application/Cpp_Core/Inc/rf_bridge_port_internal.h`

它只供中断文件和 HAL callback 分发点使用：

- `application/Core/Src/stm32h7xx_it.c`
- `application/Drivers/SPI-ST7789/spi-st7789.c`
- `application/Cpp_Core/Src/rf_bridge_port.cpp`

### CH584 侧

上层只应启动和轮询 SPI bridge：

```text
main / task loop
  -> rfm_spi_bridge_init()
  -> rfm_spi_bridge_poll()
      -> rfm_spi_port_drain(...)
          -> SPI0 slave + DMA loop/ring
```

公开接口：

- `RF_PHY_Hop/TX/APP/include/rfm_spi_bridge.h`

当前只暴露：

```c
void rfm_spi_bridge_init(void);
void rfm_spi_bridge_poll(void);
```

以下内容属于 bridge/port 内部实现：

- SPI 命令号和事件号
- byte-stream parser 状态机
- SPI0 slave DMA loop/ring
- ring drain/read pointer 维护
- backlog/drop/near/full 诊断
- CH584 IRQ 输出脚控制
- CH584 响应帧写 FIFO

内部 port 接口放在：

- `RF_PHY_Hop/TX/APP/rfm_spi_port_internal.h`

该文件只应被 SPI bridge 和 CH584 port 实现包含。

## SPI 协议帧

主方向是 STM32 -> CH584。

通用帧格式：

```text
sync    1B  0xA5
cmd/evt 1B
len     1B
payload N bytes
sum     1B  checksum8(sync + cmd/evt + len + payload)
```

STM32 -> CH584 命令：

| Command | Value | 用途 |
|---|---:|---|
| `GET_STATUS` | `0x01` | 读取 CH584 状态 |
| `START_PAIR` | `0x02` | 开始配对 |
| `STOP_PAIR` | `0x03` | 停止配对 |
| `UNBIND` | `0x04` | 解绑 |
| `SET_RATE` | `0x05` | 设置无线/输入速率 |
| `INPUT_DATA` | `0x06` | 高频输入数据 |

CH584 -> STM32 事件：

| Event | Value | 用途 |
|---|---:|---|
| `STATUS` | `0x81` | 状态返回 |
| `STATE_CHANGED` | `0x82` | 状态变化 |
| `RATE_APPLIED` | `0x83` | 速率已应用 |
| `LINK_WARN` | `0x84` | 链路警告 |
| `ERROR` | `0x85` | 错误 |

8K 高频路径主要使用固定 `INPUT_DATA` 帧：

```text
0xA5 0x06 0x0F payload[15] checksum
```

总长度为 19 字节。

`payload[15]` 当前由 `RFTransport::sendInput()` 生成：

| Offset | 含义 |
|---:|---|
| 0 | sequence low byte |
| 1 | reserved |
| 2..3 | buttons little-endian |
| 4 | dpad encoded |
| 5 | LT |
| 6 | RT |
| 7..8 | LX signed little-endian |
| 9..10 | LY signed little-endian |
| 11..12 | RX signed little-endian |
| 13..14 | RY signed little-endian |

## STM32 发送实现

实现文件：

- `application/Cpp_Core/Src/rf_transport.cpp`
- `application/Cpp_Core/Src/rf_bridge_port.cpp`

`RFTransport` 负责协议帧封包和事件解析。它不知道 SPI4、DMA stream、CS pin 等硬件细节。

`RFBridgePort_Transfer()` 负责将帧实际送到 CH584。

### INPUT_DATA 快路径

`INPUT_DATA` 是高频路径，使用 SPI4 TX DMA：

1. `RFTransport::sendInput()` 生成 19B 帧。
2. `RFBridgePort_Transfer()` 识别 `0xA5 0x06`。
3. 如果 DMA 空闲，复制到 active DMA buffer，清 DCache，然后启动 `HAL_SPI_Transmit_DMA()`。
4. 如果 DMA 正忙，只保留最新一帧到 pending buffer。
5. DMA 完成 callback 拉高 CS，并释放 busy。

该策略是 latest-only，不无限排队。

这样做的原因：

- 输入数据是状态流，不是可靠消息流。
- 旧输入晚到比丢弃更差，会增加端到端延迟。
- 8K 下每帧约 125us，不能让偶发阻塞形成 backlog。

### 控制命令路径

非 `INPUT_DATA` 命令频率低，当前走阻塞传输/可选读回：

- `GET_STATUS`
- `START_PAIR`
- `STOP_PAIR`
- `UNBIND`
- `SET_RATE`

这些命令可以等待 CH584 IRQ 后再读事件帧，优先保证控制语义清楚，不与高频输入快路径混在一起。

## CH584 接收实现

实现文件：

- `RF_PHY_Hop/TX/APP/rfm_spi_bridge.c`
- `RF_PHY_Hop/TX/APP/rfm_spi_port_ch585.c`
- `RF_PHY_Hop/TX/APP/rfm_input_stream.c`

### SPI0 RX DMA loop/ring

CH584 侧不再按 19B 每帧重启 DMA。当前实现让 SPI0 slave RX DMA 连续写入 ring：

```text
SPI0 RX DMA loop -> s_spi_rx_dma_buf[65536]
                      ^
                      DMA write pointer: R32_SPI0_DMA_NOW

rfm_spi_bridge_poll()
  -> rfm_spi_port_drain()
      -> copy unread bytes from ring
      -> fast_parser_feed_byte()
```

核心目标是消除旧方案中的 per-frame DMA re-arm blind window：

```text
旧方案:
receive 19B -> DMA_END IRQ -> disable/reconfigure/re-enable DMA -> receive next frame

新方案:
DMA continuous loop -> software drain -> byte parser resync
```

当前 ring size：

```c
#define SPI_RX_DMA_RING_SIZE 65536u
```

`R32_SPI0_DMA_NOW` 表示 DMA 当前写位置。软件维护绝对读位置和 wrap count，用于计算当前 backlog。

### Parser

`rfm_spi_bridge.c` 内部使用 fast parser 专门解析 `INPUT_DATA`：

```text
WAIT_SYNC -> CMD -> LEN -> PAYLOAD -> CHECKSUM
```

只有满足以下条件才计入 `ok`：

- sync 是 `0xA5`
- cmd 是 `0x06`
- len 是 `15`
- checksum 正确

校验通过后调用：

```c
RF_SPI_FastWriteInput(payload, 15)
```

再进入 RF 输入流。

### 最新输入策略

CH584 输入流当前也是 latest-only：

- `rfm_input_stream_push()` 写入最新 payload。
- 如果旧 payload 还没被 RF 侧消费，新 payload 覆盖旧 payload。
- 覆盖会增加 `rfm_input_stream_drop_count()`。

这与 STM32 侧 pending-latest 策略一致：系统优先保证输入新鲜度，而不是保证每个历史输入都送达 RF 层。

## Backlog/drop 策略

当前 CH584 SPI ring 主要依靠大 ring 和轮询 drain 消化输入。

诊断中已经保留 backlog/drop 字段：

```text
drop:<count>/<bytes>
max:<max_avail>
near:<near_full_count>
clip:<full_clip_count>
ov:<ring_overrun>
```

解释：

- `max`：统计窗口内 drain 时看到的最大可读 backlog。
- `near`：backlog 接近 ring 尾部的次数。
- `clip`：软件判断 backlog 已超过 ring 能表示的范围，被迫把读指针推进到最新可保留区域的次数。
- `ov`：ring overrun 相关计数。
- `drop`：主动 backlog-drop 的次数/字节数；当前代表是否启用了主动丢旧数据策略。

如果后续确认偶发消费慢导致延迟堆积，可以启用 latest-only / backlog-drop：

```text
if backlog > threshold:
    read pointer jumps near newest data
    parser resets
    old bytes are counted as dropped
```

建议阈值从 4KB 或 8KB 开始，而不是按 ring size 等比例放大。阈值代表允许排队的最大延迟，而 ring size 只是抗瞬时中断的缓冲容量。

## 诊断日志解读

CH584 当前按完成的统计窗口打印最近 4 个窗口：

```text
[4x1s][SPI_BRIDGE] recent completed seconds
[1s-1] dt:1658 raw:169984 ok:8945 rx:8945 bytes:131072 ov:0 drop:0/0 max:65536 near:8 clip:0 irq:2 bad:0/0/0/2 flg:0xCB
```

字段含义：

| 字段 | 含义 |
|---|---|
| `dt` | 该 bucket 实际覆盖时间，单位 ms |
| `raw` | parser 本窗口消费的原始字节数 |
| `ok` | checksum 正确的 `INPUT_DATA` 帧数 |
| `rx` | bridge 接收并写入输入流的帧数 |
| `bytes` | DMA ring 观察到的新增 RX 字节数 |
| `ov` | ring overrun 计数增量 |
| `drop` | 主动丢旧 backlog 的次数/字节数 |
| `max` | 本窗口内最大 backlog |
| `near` | 接近满 ring 的次数 |
| `clip` | backlog 超过 ring 后被裁剪的次数 |
| `irq` | DMA loop wrap IRQ 次数 |
| `bad` | `bad_sync/bad_cmd/bad_len/bad_checksum` |
| `flg` | 最近一次 SPI0 interrupt flags |

`bad:a/b/c/d` 的顺序是：

```text
bad_sync / bad_cmd / bad_len / bad_checksum
```

例如：

```text
bad:285/12/1/82
```

表示：

- `bad_sync=285`
- `bad_cmd=12`
- `bad_len=1`
- `bad_checksum=82`

少量 bad 通常说明 byte-stream parser 在 ring 中遇到了错位、噪声、被截断的旧数据，随后通过 sync 重新对齐。判断是否严重主要看：

- `ok` 是否接近预期输入频率
- `ov/drop/clip` 是否为 0 或可控
- bad 是否持续上升到影响 `ok`

## 当前实测结论

代表性 CH584 日志：

```text
[1s-4] dt:1617 raw:150528 ok:7919 rx:7919 bytes:196608 ov:0 drop:0/0 max:65536 near:12 clip:0 irq:3 bad:4/1/0/2 flg:0x8B
[1s-3] dt:1658 raw:169984 ok:8944 rx:8944 bytes:131072 ov:0 drop:0/0 max:65536 near:8 clip:0 irq:2 bad:15/0/1/1 flg:0x8B
[1s-2] dt:1652 raw:157696 ok:8295 rx:8295 bytes:196608 ov:0 drop:0/0 max:65536 near:12 clip:0 irq:3 bad:7/0/0/3 flg:0x8B
[1s-1] dt:1658 raw:169984 ok:8945 rx:8945 bytes:131072 ov:0 drop:0/0 max:65536 near:8 clip:0 irq:2 bad:0/0/0/2 flg:0x8B
```

按 `ok / dt` 折算，实际解析成功频率约为 4.9KHz 到 5.4KHz。因为 `dt` 仍明显大于 1000ms，这说明 CH584 主循环调度/统计 tick 仍被其它工作拉长；但从 `ov:0`、`drop:0/0`、`clip:0` 看，当前没有出现明确的 ring 数据丢失。

关键判断：

- SPI DMA loop/ring 已经消除了早期明显的 ring overflow。
- 当前日志不能简单按每行当作 1 秒解读，必须使用 `dt` 归一化。
- `max:65536` 表示曾经观察到 backlog 达到 ring 级别，需要继续确认是真实积压、wrap 计算边界，还是 drain 统计取样造成的峰值。
- 在 `ov/drop/clip` 为 0 时，`max` 本身更像风险指标，不等于已经丢包。

## 延迟与瓶颈判断

对于游戏输入链路，SPI 是否成为瓶颈主要看三个维度：

1. 频率：CH584 成功解析 `ok` 是否接近 STM32 `input_ok`。
2. 丢包：`ov/drop/clip/bad_checksum` 是否持续导致 `ok` 明显低于输入。
3. 延迟：backlog 是否持续增大，尤其 `max/near` 是否长期很高。

目前从现有日志看：

- SPI 物理接收没有明显 FIFO overflow。
- ring 没有明确 overrun。
- 主动 drop 为 0。
- 但 CH584 统计窗口仍不是准确 1s，且 `max` 经常触顶，需要继续把测量做准。

因此当前结论应保守表述为：

```text
SPI DMA loop/ring 方案方向正确，已具备作为 8K 输入通道的基础；
但在确认 CH584 1s tick 准确、backlog 峰值含义明确、ok 归一化频率稳定前，
还不能最终断言 SPI 完全不是瓶颈。
```

## 调试建议

优先级从高到低：

1. 让 CH584 统计 bucket 尽量接近真实 1000ms，或者打印时明确用 `ok_hz = ok * 1000 / dt`。
2. 继续观察 `ov/drop/clip`，它们比单独的 `max` 更能说明是否已经丢数据。
3. 如果 `max` 持续触顶但 `ov/clip` 为 0，检查 wrap/write_abs/read_abs 计算是否在 DMA_NOW 边界上误判。
4. 如果确认 backlog 真实堆积，启用 4KB/8KB latest-only backlog-drop，避免旧输入排队造成延迟。
5. 控制日志打印长度和频率，避免串口打印本身拖慢 CH584 主循环。

## 构建验证

最近验证命令：

```powershell
make -C application -j4
make -C RF_PHY_Hop\TX clean
make -C RF_PHY_Hop\TX -j4
```

结果：

- STM32 application 构建通过。
- CH584 `RF_PHY_Hop/TX` 构建通过。
- STM32 linker 的 RWX LOAD segment warning 为既有问题。
- CH58x SDK/既有 unused warning 与 SPI 封装和 DMA ring 实现无关。

