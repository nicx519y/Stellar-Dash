# RF24G SPI 8K Bring-up Handoff

Date: 2026-05-16

## Goal

Validate high-speed SPI communication from STM32 application (`./application`) to the `RF_PHY_Hop/TX` CH584/CH585 prototype at 8000 `INPUT_DATA` frames per second.

Current test scope is the SPI bridge only:

- STM32 sends fixed 19-byte `INPUT_DATA` frames: `0xA5 0x06 0x0F payload[15] checksum`.
- CH584/CH585 receives and validates the frames.
- `RF_PHY_Hop/TX` is a standalone RF validation/prototype project, not the production STM32 application code.

## Current Status

STM32 side is considered fixed for this bring-up phase:

- TIM2 scheduler runs at 8K: `REPORT_SCHED irq:40000 irq_hz:8000`.
- RF send path accepts about 39.6K frames per 5s, around 7.92KHz.
- SPI4 TX DMA completion is healthy:
  - `dma_overwrite:0`
  - `spi_err:0`
  - `spi_irq` tracks `dma_done`
  - `dma_irq` is about 2x `dma_done`, consistent with DMA half/full interrupt servicing

Latest representative STM32 log:

```text
[APP] [REPORT_SCHED][5s] irq:40000 consumed:39622 dropped:378 pending:0 irq_hz:8000 consumed_hz:7924 rate:8000
[APP] [RF_SEND][5s] calls:39622 ok:39622 fail:0 hz:7924 total:119138 last_seq:119138 rate:8000
[APP] [RF_BRIDGE][5s] tx:39622 ok:39622 fail:0 input:39622 input_ok:39622 input_fail:0 input_hz:7924 total_input:119139 last_cmd:0x06 last_seq:99 txLen:19
[APP] [RF_BRIDGE][5s][diag] spi_init_fail:0 tx_fail:0 irq_timeout:0 rx_invalid:0 rx_io_fail:0 dma_start_fail:0 dma_overwrite:0 dma_done:119137 dma_irq:238274 spi_irq:119137 spi_err:0
```

CH584/CH585 side has improved but is still below 8K:

- Normal frames bypass queue/parser through ISR direct path.
- Queue pressure is gone:
  - `drop:0`
  - `fifo_ov:0`
  - `qmax:0`
  - `raw:0`
  - `bad_irq:0`
- Physical receive completion is still only about 25.6K frames per 5s, around 5.13KHz.

Latest representative CH584/CH585 log:

```text
[5s][SPI_BRIDGE] raw:0 frame_ok:25494 direct:25662 bad_sync:0 bad_cmd:0 bad_len:0 bad_sum:0 rx_total:143077 tx_total:0 drop:0 dma_end:25662 qmax:0 cnt_end:25662 fifo_ov:0 irq:25662 bad_irq:0 flags:0x00
```

## Current Interpretation

The remaining bottleneck is no longer frame parsing, queue draining, FIFO overflow, or STM32 transmission.

The current fixed-length DMA receive model still requires this per frame:

```text
receive 19B -> DMA_END IRQ -> disable/reconfigure/re-enable RX DMA -> receive next frame
```

At the STM32 send rate, frames arrive about every `126 us`. CH584/CH585 currently completes one accepted frame about every `195 us`. The effective missing window is therefore roughly `69 us` per received frame, including interrupt latency, DMA re-arm, SPI peripheral state changes, and any remaining ISR work.

This points to per-frame RX DMA re-arm blind time or SPI slave transaction spacing as the likely limit.

## Current Code Shape

STM32 application:

- `application/Cpp_Core/Src/rf_bridge_port.cpp`
  - SPI4 TX DMA fast path for `INPUT_DATA`
  - latest-only enqueue
  - diagnostics: `dma_start_fail`, `dma_overwrite`, `dma_done`, `dma_irq`, `spi_irq`, `spi_err`
- `application/Core/Src/stm32h7xx_it.c`
  - `DMA2_Stream5_IRQHandler()`
  - `SPI4_IRQHandler()`
- `application/Drivers/SPI-ST7789/spi-st7789.c`
  - routes HAL SPI completion/error callbacks to RF bridge for non-ST7789 SPI handles

CH584/CH585 prototype:

- `RF_PHY_Hop/TX/APP/rfm_spi_port_ch585.c`
  - current RX path uses fixed 19-byte DMA receive
  - two 19-byte RX buffers are used
  - on `DMA_END`, ISR switches buffers and re-arms DMA before validating the completed frame
  - valid `INPUT_DATA` frames are accepted directly from ISR
- `RF_PHY_Hop/TX/APP/rfm_spi_bridge.c`
  - direct fixed-frame validator for ISR path
  - fallback queue/parser remains for abnormal frames
  - diagnostics include `direct`, `dma_end`, `qmax`, `drop`, `fifo_ov`, `bad_irq`

## Next Plan: SPI DMA Loop / Ring Buffer

Next experiment should replace per-frame 19-byte DMA receive with continuous SPI RX DMA loop/ring buffering.

Target shape:

```text
SPI RX DMA loop writes bytes continuously into ring buffer
software tracks DMA write pointer
parser consumes bytes from ring read pointer
INPUT_DATA frames are validated by sync/cmd/len/checksum
```

Expected advantage:

- Removes the per-frame DMA re-arm blind window.
- Reduces or eliminates `DMA_END` interrupt frequency.
- Makes CH584/CH585 tolerant of short STM32 CS gaps and back-to-back frames.
- Lets diagnostics focus on bytes received, parser recovery, and ring overrun instead of per-frame DMA completion.

Expected tradeoffs:

- Frame boundary is no longer provided by DMA completion; parser must resync from the byte stream.
- Diagnostics need to change: `dma_end` is no longer frame count.
- Need ring overrun protection and parser recovery counters.
- Need to confirm CH58x SPI slave DMA loop behavior with CS-framed master transactions.

Suggested first implementation:

- Use 512B or 1024B RX ring.
- Keep SPI RX DMA enabled continuously in loop mode if supported by the CH58x SPI0 DMA path.
- Poll DMA current pointer from `rfm_spi_bridge_poll()` or use lower-frequency half/full notifications.
- Parse only host-to-module frames first; keep response/IRQ slow path unchanged.
- Add diagnostics:
  - `rx_bytes`
  - `frame_ok`
  - `bad_sync`
  - `bad_cmd`
  - `bad_len`
  - `bad_sum`
  - `ring_ov`
  - `dma_pos`

Success target:

- `frame_ok` close to STM32 input count, about `39.5K-40K / 5s`.
- `ring_ov:0`.
- Bad frame counters near zero.

## Build Verification

Most recent successful build:

```powershell
make -C RF_PHY_Hop\TX -j4
```

Output:

- `RF_PHY_Hop/TX/build_tx/RF_PHY_Hop_TX.bin`

STM32 build was previously passing with the current SPI4 DMA completion path:

```powershell
make -C application -j4
```

Output:

- `application/build/application.bin`

Known warning:

- STM32 linker still reports a RWX LOAD segment warning. This is pre-existing and unrelated to RF24G SPI bring-up.

## Files To Start From

STM32 send path:

- `application/Cpp_Core/Src/rf_bridge_port.cpp`
- `application/Core/Src/stm32h7xx_it.c`
- `application/Drivers/SPI-ST7789/spi-st7789.c`

CH584/CH585 receive path:

- `RF_PHY_Hop/TX/APP/rfm_spi_port_ch585.c`
- `RF_PHY_Hop/TX/APP/rfm_spi_bridge.c`
- `RF_PHY_Hop/TX/APP/include/rfm_spi_port.h`
- `RF_PHY_Hop/TX/APP/include/rfm_spi_bridge.h`

## Caution

The workspace already contains multiple RF24G/SPI bring-up edits. Do not revert unrelated changes casually. Use `git diff` before continuing.
