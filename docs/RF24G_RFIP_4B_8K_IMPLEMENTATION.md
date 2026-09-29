# RF24G RFIP 4B Payload 8K Implementation

Date: 2026-05-17

This document records the current working RF implementation in `RF_PHY_Hop/TX` and `RF_PHY_Hop/RX`.

## Current Status

The working RF path is **self-managed hopping over RFIP Basic**.

- RF channels: `{4, 8, 12, 16, 20, 24, 28, 32, 36}`
- Hop dwell: `16` packets per channel
- RF PHY: `LLE_MODE_PHY_2M`
- RF access address: `0x71764129`
- RF CRC init: `0x555555`
- RF packet length: `12` bytes
- RF input payload length: `4` bytes
- RF cadence: `8000 Hz`
- TX input source: STM32 application sends 15-byte `INPUT_DATA` frames over SPI
- TX RF payload: currently uses the first 4 bytes of the latest SPI payload inside a protocol packet
- Pairing/binding: disabled for this working path

The verified working result is approximately:

```text
TX upd_hz ~= 7970, txok ~= 40000 / 5s
RX hz ~= 7980, fail very low
```

## Why This Path

The current path uses the lower-level RFIP Basic flow:

```text
RFRole_BasicInit()
RFRole_SetParam()
RFIP_SetTxStart()
RFIP_SetTxParm()
RFIP_SetRx()
```

The previous 15-byte RF payload did not fit reliably at 8K. A 4-byte input payload works for the Hitbox product because all buttons are 1-bit states. The RF packet is now 12 bytes because it includes a protocol header, sequence number, hop epoch, and CRC16.

## Data Flow

### STM32 Application to TX

The STM32 application sends normal 15-byte `INPUT_DATA` SPI frames.

Relevant application logs:

```text
[RF_BRIDGE][5s] input_hz:7923 ...
```

### TX SPI to RF

```text
rfm_spi_bridge.c
  -> RF_SPI_FastWriteInput(payload, len)
  -> rfm_input_stream_push(payload, len)
  -> RF_PHY.c / rf_fill_payload()
  -> RFIP TX DMA buffer
  -> RFIP_SetTxStart()
  -> RFIP_SetTxParm()
```

Current RFIP TX DMA layout:

```text
TxBuf[0] = 0x55
TxBuf[1] = 12
TxBuf[2] = magic        (0xA7)
TxBuf[3] = type         (DATA=0x01)
TxBuf[4] = session_id   (0x21)
TxBuf[5] = seq
TxBuf[6] = hop_epoch    (0)
TxBuf[7] = flags
TxBuf[8..11] = latest_spi_payload[0..3]
TxBuf[12..13] = crc16 over TxBuf[2..11]
```

The `0x55 + len + payload` layout follows the WCH `RF_Basic` RFIP example.

### RX RF Receive

```text
RFIP_SetRx()
  -> RF_ProcessCallBack()
  -> RX counters
  -> tmos_set_event(..., SBP_RF_RF_RX_EVT)
  -> RFIP_SetRx() re-arm
```

RX currently only counts received packets and errors. It does not yet decode the 4-byte payload into XInput button state.

## File and Function Index

### TX Main

Path: `RF_PHY_Hop/TX/APP/RF_main.c`

- `main()`
  - Initializes CH58x/BLE/HAL.
  - Calls `RF_RoleInit()`.
  - Calls `RF_Init()`.
  - Calls `rfm_spi_bridge_init()`.
- `Main_Circulation()`
  - Polls SPI bridge.
  - Calls `RF_TxMainLoopProcess()`.
  - Calls `TMOS_SystemProcess()`.

### TX RF

Path: `RF_PHY_Hop/TX/APP/RF_PHY.c`

Important configuration:

```c
#define RF_TEST_FREQUENCY           16
#define RF_TEST_DATA_LEN            12
#define RF_REPORT_PPS               8000
#define RF_USE_LOW_LEVEL_BASIC      0
#define RF_TX_USE_TMR0_IRQ          1
#define RF_HOP_DWELL_PACKETS        16
```

Public functions:

- `RF_Init()`
  - Registers TMOS RF task.
  - Enables RF interrupts.
  - Initializes SPI input stream.
  - Configures Timer0 for 8K.
  - Starts RFIP Basic TX path via `rf_basic_start_tx()`.
- `RF_TxMainLoopProcess()`
  - Currently no heavy RF work when `RF_TX_USE_TMR0_IRQ == 1`; retained as the main-loop RF hook.
- `RF_SPI_FastWriteInput(const uint8_t *payload, uint8_t len)`
  - Entry point used by SPI bridge to submit the latest 15-byte input payload.

Internal functions:

- `rf_basic_start_tx()`
  - Calls `RFRole_BasicInit()`.
  - Sets `gParm.accessAddress`, `gParm.crcInit`, `gParm.properties`, `gParm.sendTime`.
  - Calls `RFRole_SetParam()`.
  - Initializes `gTxParam`.
- `TMR0_IRQHandler()`
  - Runs at 8K.
  - Calls `rf_fill_payload()`.
  - Calls `rf_tx_start()`.
  - Updates TX cadence counters.
- `rf_fill_payload()`
  - Pulls latest SPI payload via `rfm_input_stream_take_latest()`.
  - Builds the 12-byte RFIP DATA packet with 4-byte input payload, `seq`, and CRC16.
- `rf_tx_start()`
  - Calls `RFIP_SetTxStart()`.
  - Selects the RF channel from `seq / 16`.
  - Calls `RFIP_SetTxParm(&gTxParam)`.
- `RF_ProcessCallBack(rfRole_States_t sta, uint8_t id)`
  - Counts `RF_STATE_TX_FINISH`, `RF_STATE_TIMEOUT`, and `RF_STATE_TX_IDLE`.
- `RF_ProcessEvent(uint8_t task_id, uint16_t events)`
  - Handles 5-second TX statistics logging.

### TX SPI Bridge

Path: `RF_PHY_Hop/TX/APP/rfm_spi_bridge.c`

- `rfm_spi_bridge_init()`
  - Initializes SPI bridge state and SPI port.
- `rfm_spi_bridge_poll()`
  - Polls SPI RX bytes and feeds parser.
- `fast_parser_feed_byte(uint8_t b)`
  - Parses high-rate `INPUT_DATA` frames:

```text
0xA5, cmd=0x06, len=15, payload[15], checksum8(sum)
```

- Calls `RF_SPI_FastWriteInput()` after a valid frame.

Path: `RF_PHY_Hop/TX/APP/rfm_input_stream.c`

- `rfm_input_stream_init()`
- `rfm_input_stream_push(const uint8_t *payload, uint8_t len)`
- `rfm_input_stream_take_latest(uint8_t *payload, uint8_t len)`

This is a latest-sample buffer between SPI input and RF transmission.

Path: `RF_PHY_Hop/TX/APP/rfm_spi_port_ch585.c`

- Implements CH58x SPI0 slave DMA ring.
- Keeps high-rate SPI input from blocking the RF path.

### RX Main

Path: `RF_PHY_Hop/RX/APP/RF_main.c`

- `main()`
  - Initializes CH58x/BLE/HAL and USB composite.
  - Enters `Main_Circulation()`.
- `Main_Circulation()`
  - Waits for USB enumeration.
  - Calls `RF_Init()` after USB is up.
  - Every 5 seconds calls `RF_GetStatsLine()` and sends CDC log.
  - Uses a pending-log retry so CDC endpoint busy does not drop the whole log.

### RX RF

Path: `RF_PHY_Hop/RX/APP/RF_PHY.c`

Important configuration:

```c
#define RF_TEST_FREQUENCY           16
#define RF_TEST_DATA_LEN            12
#define RF_USE_LOW_LEVEL_BASIC      0
#define RF_HOP_DWELL_PACKETS        16
#define RF_RX_TIMEOUT_HALF_US       1000
```

Public functions:

- `RF_Init()`
  - Calls `RF_RoleInit()` internally.
  - Registers TMOS RF task.
  - Enables RF interrupts.
  - Calls `RFRole_BasicInit()`.
  - Calls `RFRole_SetParam()`.
  - Initializes `gRxParam`.
  - Calls `RFIP_SetRx()`.
- `RF_GetStatsLine(char *buf, uint16_t len)`
  - Formats 5-second RX statistics for the main loop.

Internal functions:

- `rf_rx_start()`
  - Calls `RFIP_SetRx(&gRxParam)`.
- `RF_ProcessCallBack(rfRole_States_t sta, uint8_t id)`
  - Validates the 12-byte RFIP DATA packet.
  - Checks magic/type/session/hop epoch/CRC16.
  - Tracks sequence gaps and duplicate packets.
  - Tunes RX to the next expected channel after valid packets.
  - Advances reacquire scanning after CRC errors, invalid packets, or timeouts.
- `RF_ProcessEvent(uint8_t task_id, uint16_t events)`
  - Re-arms RX after callbacks.

## Public Interfaces After Cleanup

TX public header: `RF_PHY_Hop/TX/APP/include/RF_PHY.h`

```c
void RF_Init(void);
void RF_TxMainLoopProcess(void);
bool RF_SPI_FastWriteInput(const uint8_t *payload, uint8_t len);
```

RX public header: `RF_PHY_Hop/RX/APP/include/RF_PHY.h`

```c
void RF_Init(void);
uint16_t RF_GetStatsLine(char *buf, uint16_t len);
```

TMOS event bits and WCH RF details are intentionally kept inside `RF_PHY.c`.

## Build Outputs

TX:

```text
RF_PHY_Hop/TX/build_tx/RF_PHY_Hop_TX.bin
```

RX:

```text
RF_PHY_Hop/RX/build_rx/RF_PHY_Hop_RX.bin
```

Build commands:

```powershell
make -C RF_PHY_Hop\TX -j4
make -C RF_PHY_Hop\RX -j4
```

Default build is validation mode B because validation mode A showed very low TX finish callbacks with 12-byte packets:

```text
RF_TEST_PROTOCOL_PACKET=0
RF_TEST_ENABLE_HOP=0
```

Use this to confirm the old 4-byte fixed-channel baseline before changing one variable at a time.

Validation modes:

```powershell
# A: 12B protocol packet, fixed channel.
make -C RF_PHY_Hop\TX clean; make -C RF_PHY_Hop\TX -j4 EXTRA_DEFINES="-DRF_TEST_PROTOCOL_PACKET=1 -DRF_TEST_ENABLE_HOP=0"
make -C RF_PHY_Hop\RX clean; make -C RF_PHY_Hop\RX -j4 EXTRA_DEFINES="-DRF_TEST_PROTOCOL_PACKET=1 -DRF_TEST_ENABLE_HOP=0"

# B: old 4B fixed-channel baseline. Default.
make -C RF_PHY_Hop\TX clean; make -C RF_PHY_Hop\TX -j4
make -C RF_PHY_Hop\RX clean; make -C RF_PHY_Hop\RX -j4

# C: 12B protocol packet with self-managed hopping.
make -C RF_PHY_Hop\TX clean; make -C RF_PHY_Hop\TX -j4 EXTRA_DEFINES="-DRF_TEST_PROTOCOL_PACKET=1 -DRF_TEST_ENABLE_HOP=1"
make -C RF_PHY_Hop\RX clean; make -C RF_PHY_Hop\RX -j4 EXTRA_DEFINES="-DRF_TEST_PROTOCOL_PACKET=1 -DRF_TEST_ENABLE_HOP=1"
```

## Current Logs

TX 5-second log:

```text
[TX][5s] dt:... irq:... sent:... upd:... upd_hz:... txok:... fail:... idle:... sf:... pf:... spi:win/total ... cfg:0 ch:16
```

Important TX fields:

- `irq`: Timer0 8K interrupt count.
- `sent`: attempted RFIP sends.
- `upd_hz`: RF payload update/send attempt frequency.
- `txok`: RF TX finish callback count.
- `fail`: RF timeout count.
- `sf`: `RFIP_SetTxStart()` failure count.
- `pf`: `RFIP_SetTxParm()` failure count.
- `spi`: SPI input frames seen by TX RF layer.

RX 5-second log:

```text
[RX][5s] dt:... ok:... fail:... hz:... gap:... ch:... cfg:0 r:0
```

Important RX fields:

- `ok`: valid RX packets in the stats window.
- `fail`: CRC/error packets in the stats window.
- `hz`: valid RX packet rate.
- `gap`: sequence gap count.
- `ch`: current receive channel.
- `cfg`: RF init return code.
- `r`: `RFIP_SetRx()` return code.

## Known Limitations and Next Steps

- TX currently uses `latest_spi_payload[0..3]` as the 4-byte input payload. A formal Hitbox bitmask mapping still needs to be defined.
- RX currently counts packets only. It still needs to decode the 4-byte payload and feed XInput state.
- Pair/Search and adaptive channel masks are not implemented yet; the current hop schedule uses a fixed session and fixed table.
- Debug logs are still verbose. They are useful during bring-up and should be gated or reduced before release.
