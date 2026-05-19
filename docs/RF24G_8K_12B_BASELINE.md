# RF24G 8K 12B Baseline

Date: 2026-05-19

This document records the current RF24G tuning baseline for the `RF_PHY_Hop` prototype. The goal is a stable low-latency 8K wireless input link with a validated reverse-link hop request path.

## Goal

```text
STM32 application -> SPI INPUT_DATA -> CH58x TX -> 2.4G RF -> CH58x RX
```

Current baseline:

- RF trigger rate: `8000 Hz`
- RF air payload: `12 B`
- User data over SPI: `10 B`
- STM32 SPI command frame: `14 B`
- TX owns air byte `0` as RF sequence
- TX owns air byte `1` as control/countdown/HOP_ADV
- User data is carried in air bytes `2..11`
- Normal mode is fixed-channel 8K TX
- Hop test mode is RX-triggered reverse request, not TX CCA

## Payload Layout

### STM32 SPI Frame

File:

- `application/Cpp_Core/Src/rf_transport.cpp`

Frame:

```text
0xA5, 0x06, 0x0A, payload[10], checksum8
```

Bandwidth:

```text
14 B * 8000/s = 112000 B/s = 896 kbit/s raw SPI framed stream
10 B * 8000/s =  80000 B/s = 640 kbit/s useful SPI payload
```

### RF Air Frame

Files:

- `RF_PHY_Hop/TX/APP/RF_PHY.c`
- `RF_PHY_Hop/RX/APP/RF_PHY.c`

RFIP DMA layout:

```text
TxBuf[0] = 0x55
TxBuf[1] = 12
TxBuf[2..13] = air payload[12]
```

Air payload:

```text
air[0]     = TX-generated sequence
air[1]     = control byte
air[2..11] = latest 10B SPI payload
```

Control byte:

```text
0xFF       = far from reverse listen window
1..254     = ms countdown to next TX reverse listen window
0xA5       = HOP_ADV marker when forward hop advertisement is used
```

Air payload bandwidth:

```text
12 B * 8000/s = 96000 B/s = 768 kbit/s
```

## TX Timing

TX uses `TMR0` as the single air scheduler.

Parameters:

- `RF_REPORT_PPS = 8000`
- `RF_REV_PERIOD_MS = 20000`
- `RF_REV_COUNTDOWN_LEAD_MS = 200`
- `RF_REV_LISTEN_WINDOW_MS = 200`
- `RF_REV_LISTEN_PACKETS = 1600`
- `RF_TEST_DATA_LEN = 12`
- RF PHY: `LLE_MODE_PHY_2M`
- Access address: `0x71764129`
- CRC init: `0x555555`

TMR0 behavior:

```text
Every 125 us:
  if reverse RX window is active:
    do not TX
    count down the 200ms RX window
    close RX when 1600 ticks expire
  else if reverse RX is pending:
    do not TX
    RFRole_Stop()
    RFIP_SetRx()
  else:
    fill latest payload
    RFIP_SetTxStart()
    RFIP_SetTxParm()
```

This avoids the earlier race where TMOS opened RX while TMR0 continued to transmit.

Expected TX impact of one 200ms reverse handshake window:

```text
8000/s * 200ms = 1600 skipped TX packets if no request/ACK is received
When TX receives a reverse request, it sends one forward ACK and exits the window early
```

## Reverse Hop Request

Current test does not use TX-side CCA. RX triggers hop through the reverse-link path only after sustained poor receive quality.

RX reverse request parameters:

- `RF_REV_REQ_REPEAT = 220`
- `RF_REV_REQ_INTERVAL_MS = 1`
- `RF_REV_REQ_START_COUNTDOWN_MS = 2`
- Reverse request marker: `0xC3`
- RX quality trigger: `tr:1`
- RX quality trigger threshold: `h < 7600` or `g > 2500` for 2 consecutive 5s windows
- RX quality trigger cooldown: `30000 ms`

Reverse request payload:

```text
air[0] = 0xC3
air[1] = request epoch
air[2] = next channel
air[3] = reason
```

Current channel table:

```text
{4, 8, 12, 16, 20, 24, 28, 32, 36}
```

Validated behavior:

```text
TX sends countdown in air[1]
RX sees cd <= 2ms and starts sending reverse request packets only if a quality request is pending
TX opens a 200ms reverse RX window from the TMR0 state machine
TX accepts one valid reverse request carrying RX timestamp t
TX exits RX, sends one forward ACK carrying t and the accepted channel
TX switches channel after the ACK is sent
RX receives the ACK, computes reverse RTT from t, and switches channel
```

Representative successful logs:

```text
[TX][win] ... ch:24 ... rq:1/1 ... hp:1 ...
[TX][win] ... ch:4  ... rq:1/1 ... hp:1 ...
```

Meaning:

- `rq:1/1`: one valid reverse request was received and accepted.
- `hp:1`: TX completed one requested hop.
- `ch` changing confirms TX changed RF channel.

## Diagnostics

### TX Log

Current compact TX format:

```text
[TX][win] l:%u dt:%lums irq:%lu hz:%lu pk:%lu/%lu ch:%u cd:%u rq:%lu/%lu bad:%lu/%lu crc:%lu lm:%u/%u rr:%u hp:%lu ls:%lu/%lu e:%lu/%lu/%lu/%lu
```

Fields:

- `l`: RF air payload length, baseline `12`
- `dt`: stats window in ms
- `irq`: TMR0 ISR count, target about `40000 / 5s`
- `hz`: payload TX rate; about `7980` normally, lower during a reverse handshake window
- `pk`: SPI latest-frame peek `ok/miss`, miss should be near `0`
- `ch`: current TX RF channel
- `cd`: current reverse-window countdown field
- `rq`: accepted valid reverse requests / valid reverse packets received
- `ack`: forward ACK packets sent by TX
- `bad`: invalid reverse length / invalid reverse marker
- `crc`: reverse-window CRCERR count
- `lm`: last reverse RX length / marker
- `rr`: last `RFIP_SetRx()` return, `0` means success
- `hp`: requested hop completed count
- `ls`: reverse listen windows / listen timeouts
- `e`: TX start fail / TX param fail / sequence rollback / reverse RX start fail

Healthy reverse-hop success pattern:

```text
TX sends `ack:1`
ls:1/0 or ls:1/1
rq:1/1
hp:1
e:0/0/0/0
```

`ls:1/0` can be normal when a valid reverse request is received before the 200ms window expires. TX still opens periodic reverse windows, but RX no longer sends periodic test requests.

### RX Log

Current compact RX format:

```text
[R5]h%lu g%lu c%lu t%lu ch%u hp%lu tr%u rq%lu ak%lu/%lu
```

Fields:

- `h`: valid RX packet rate in the window
- `g`: sequence gap count
- `c`: CRCERR count
- `t`: timeout count
- `ch`: current RX RF channel
- `hp`: RX channel changes in this window
- `tr`: last reverse request trigger reason, `1` RX quality
- `rq`: reverse request burst count
- `ak`: ACK count in this log window / last reverse request RTT in ms

Use `hp`, `h`, `g`, `c`, and `t` together to estimate hop cost. A hop window should show `hp:1`; the same window's `h/g/c/t` is the first coarse loss estimate for that hop.

## Current Interpretation Rules

- If TX `pk` miss is near `0`, SPI-to-TX handoff is healthy.
- If TX `irq` is about `40000/5s`, TMR0 cadence is healthy.
- If TX `hz` drops with `ls:1`, the reverse RX window really happened.
- If TX shows `rq:1/1 hp:1`, reverse hop request succeeded.
- If TX shows high `crc` but no `rq`, TX is hearing something in the reverse window but the reverse RF packet is not valid.
- If RX `hp:1` appears, compare that line's `h/g/c/t` against adjacent windows to estimate hop loss.

## RX Quality Trigger

The first automatic trigger is intentionally conservative. Every 5s RX statistics window is evaluated:

```text
bad window if h < 7600 or g > 2500
trigger after 2 consecutive bad windows
cool down quality-triggered requests for 30s
```

The quality trigger does not transmit immediately. It arms a pending request and waits for the next TX countdown/reverse-window opportunity. If a reverse request is missed, the pending request remains armed and retries at the next TX reverse window. The quality cooldown starts only after RX actually changes channel.

The earlier periodic RX request trigger has been removed to avoid confusing channel-quality evaluation. TX still advertises countdown windows, but RX only transmits a reverse request when a quality-triggered request is pending.

## Next Tuning Steps

Do these after this baseline remains stable:

1. Observe whether `tr:1` appears only on genuinely poor channels.
2. If quality trigger is too sensitive, lower the `h` threshold or require 3 consecutive bad windows.
3. If it reacts too slowly, reduce the consecutive-window requirement or add a short-window detector.
4. Keep fixed channel-table order first.
5. Only after trigger behavior is stable, consider channel scoring based on RX history.

## Build Reference

Build both RF images:

```powershell
make -C .\RF_PHY_Hop both
```

Outputs:

```text
RF_PHY_Hop/TX/build_tx/RF_PHY_Hop_TX.bin
RF_PHY_Hop/RX/build_rx/RF_PHY_Hop_RX.bin
```

Known warnings are unused legacy helpers from disabled protocol paths. They are not part of the current hot path.

## Files To Check First

- `application/Cpp_Core/Src/rf_transport.cpp`
- `RF_PHY_Hop/TX/APP/include/rfm_config.h`
- `RF_PHY_Hop/TX/APP/rfm_spi_port_ch585.c`
- `RF_PHY_Hop/TX/APP/RF_PHY.c`
- `RF_PHY_Hop/RX/APP/RF_PHY.c`
