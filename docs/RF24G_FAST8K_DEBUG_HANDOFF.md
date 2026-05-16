# RF24G FAST 8K Debug Handoff

Date: 2026-05-16

This document hands off the current RF24G FAST 8K bring-up state for the next agent. The active debugging target is `RF_PHY_Hop/TX` and `RF_PHY_Hop/RX`, not `RFModule`.

## Goal

Bring up CH58x RF FAST 8K so that:

- STM32 sends 15-byte input payloads to TX over high-speed SPI.
- TX updates the RF payload at about 8 kHz.
- RX/dongle receives those RF packets and eventually reports them over USB.

## Current Build Outputs

Current generated binaries:

- TX: `RF_PHY_Hop/TX/build_tx/RF_PHY_Hop_TX.bin`
- RX: `RF_PHY_Hop/RX/build_rx/RF_PHY_Hop_RX.bin`

Build commands:

```powershell
make -C RF_PHY_Hop\TX -j4
make -C RF_PHY_Hop\RX -j4
```

Avoid `make clean all -j4`; the Makefile has a parallel clean/build race. Use separate commands:

```powershell
make -C RF_PHY_Hop\TX clean; make -C RF_PHY_Hop\TX -j4
make -C RF_PHY_Hop\RX clean; make -C RF_PHY_Hop\RX -j4
```

## Files Modified In This Debug Session

- `RF_PHY_Hop/TX/APP/RF_main.c`
  - `RFM_SPI_ONLY_MODE` changed from `1` to `0` so RF path runs.

- `RF_PHY_Hop/TX/APP/rfm_spi_bridge.c`
  - Fixed 1-second diagnostic `dt` conversion from TMOS ticks to real milliseconds.
  - Added `hz` to SPI bridge log.

- `RF_PHY_Hop/TX/Makefile`
  - Added `-DRF_8K=1`.
  - This is required for HAL `Clock1`/TMR3 setup used by WCH 8K bound mode.

- `RF_PHY_Hop/TX/APP/RF_PHY.c`
  - Added FAST 8K diagnostics.
  - Uses `RFBound_Start8kDevice()`.
  - Adds `RFBound_SetFrequencyList()`.
  - Adds explicit `OwnInfo`/`PeerInfo`.
  - Uses TMR0 IRQ to update payload at about 8 kHz.
  - Adds retry-on-bind-fail logic.

- `RF_PHY_Hop/RX/Makefile`
  - Added `-DRF_8K=1`.

- `RF_PHY_Hop/RX/APP/RF_PHY.c`
  - Changed RX from `RFBound_Start8kDevice()` to `RFBound_Start8kHost()`.
  - Added `RFBound_SetSpeedType()`.
  - Added `RFBound_SetFrequencyList()`.
  - Added explicit `OwnInfo`/`PeerInfo`.
  - Added readable RX diagnostics.
  - Current code also contains experimental Host restart logic. This is a debug experiment, not a final design.

## Important Findings

### 1. SPI Frequency Is Basically OK When Measured Correctly

Early logs showed about 5 kHz because `dt` was printed in TMOS ticks but read like milliseconds. After converting ticks to real ms, SPI-only logs showed around 8 kHz:

```text
[1s-4] dt:1036 raw:169984 ok:8945 hz:8634 ...
[1s-3] dt:1033 raw:157696 ok:8296 hz:8030 ...
```

Later, when RF/debug load was high, SPI dropped closer to 6.7-7.0 kHz with ring overruns:

```text
ov:26496 ... hz:6749
```

This appears related to current debug/retry/RF load. Do not treat those later numbers as the clean SPI-only baseline.

### 2. `RF_8K=1` Is Mandatory

Before adding `-DRF_8K=1`, `RFBound_Start8kDevice()` returned:

```text
start:3
```

From disassembly, `RFBound_Start8kDevice()` returns `3` when the 8K `Clock1` callback is NULL. `HAL_TimeInit()` only registers that Clock1 callback inside `#if RF_8K`.

After adding `-DRF_8K=1`:

```text
[5s][FAST] ... sw:0 init:0 start:0 ...
```

### 3. TX Should Be 8K Device, RX Should Be 8K Host

WCH definitions in `wchrf.h`:

```c
#define RF_ROLE_RX_MOD0 0  // host
#define RF_ROLE_TX_MOD0 1  // device
```

Practical mapping for this project:

- TX module attached to STM32 SPI: `RFBound_Start8kDevice()`
- RX/dongle side: `RFBound_Start8kHost()`

Trying TX as Host after `RF_8K=1` produced:

```text
start:4
```

So TX Host is not the right path for this setup.

### 4. RX Host Needs `RFBound_SetSpeedType()`

RX Host initially returned:

```text
[RX5] ok:0 start:4 cb:0 sta:255 role:255
```

Disassembly of `RFBound_Start8kHost()` showed return `4` when `pSpeedList == NULL`. Adding:

```c
RFBound_SetSpeedType(&gHostSpeedList)
```

fixed Host start:

```text
[RX5] ok:0 speed:0 freq:0 start:0 cb:0 sta:255 role:255
```

### 5. Binding Still Does Not Complete

Typical current TX log:

```text
[5s][FAST] upd:42786 upd_hz:7949 sw:0 init:0 freq:0 start:0 bound:6 fail:6 rst:6 sta:1 role:1 id:0 type:0 hop:0
```

Meaning:

- TX FAST mode switch succeeded.
- TX FastInit succeeded.
- TX frequency list setup succeeded.
- TX Device start succeeded.
- Payload update is about 8 kHz.
- Binding callback reports `sta:1`, i.e. Device-side bind failure.

Typical RX log:

```text
[RX5] ok:0 speed:0 freq:0 start:0 cb:0 sta:255 role:255
```

Meaning:

- RX Host start succeeded.
- Speed list setup succeeded.
- Frequency list setup succeeded.
- But Host has not received/processed a binding callback.

One test after adding explicit `OwnInfo`/`PeerInfo` produced:

```text
[RX5] ok:0 speed:0 freq:0 start:0 cb:2 sta:23 role:0
```

`23` is `0x17 = BOUND_STA_TIMEOUT`, meaning Host saw enough of the process to trigger a timeout callback. This was progress but not stable in later tests.

## Current Diagnostics

TX prints:

```text
[5s][RF] dt:... irq:... due:... sent:... try_hz:... ok_hz:...
[5s][RF][api] start_fail:... parm_fail:... cb_other:...
[5s][FAST] upd:... upd_hz:... sw:... init:... freq:... start:... bound:... fail:... rst:... sta:... role:... id:... type:... hop:...
[5s][SPI] rx_entries:... total:... drop:... last_seq:...
```

RX prints:

```text
[RX5] ok:... speed:... freq:... start:... cb:... sta:... role:...
```

and should also print full serial logs:

```text
[5s][RX] dt:... rx:... ok:... fail:... rx_hz:... txok:...
[5s][RX][FAST] sw:... init:... speed:... freq:... start:... bound:... ok:... rst:... sta:... role:... id:... type:... hop:...
```

If only `[RX5]` is visible, the UART/CDC path being observed may differ; check both serial outputs.

## Current Hypotheses

### Most Likely

FAST 8K Host/Device binding parameters still do not match fully, even though the obvious setup calls now return success.

Candidates:

- `hop` mode mismatch or wrong mode for 8K bound.
- Fixed `RF_HOP_OFF` plus explicit frequency list may not match what 8K bound expects.
- `OwnInfo`/`PeerInfo` use may still be wrong.
- Host speed list `deviceId = RF_ROLE_ID_INVALD` might not be the desired list entry for this library.
- `RF_HOP_MODE` compile values (`TX=1`, `RX=2`) may influence lower-level library behavior in a way that conflicts with FAST bound.

### Also Plausible

RF physical/channel issue:

- Need to prove a simple Basic fixed-channel TX/RX link works on these exact two boards.
- Current RF tests have many moving pieces: FAST bound, 8K Clock1, SPI pressure, USB logging, and retry loops.

## Recommendations For Next Agent

### 1. Do Not Keep RX Host Auto-Restart As Final Behavior

The current RX code has experimental Host restart logic:

```c
RFBound_Stop();
RFBound_Start8kHost(...);
```

This was added to test timing-window theories. The user challenged this, correctly. It may disturb the WCH state machine. Prefer reverting or gating it behind a debug macro before finalizing.

### 2. First Establish A Minimal RF Link

Before more FAST 8K tuning, create/restore a minimal Basic fixed-channel test:

- TX sends one short packet periodically on frequency/channel `16`.
- RX listens on the same frequency/channel.
- Confirm RX callback increments.

If Basic fixed channel fails, stop debugging FAST bound and fix RF hardware/channel first.

### 3. Then Test WCH FAST Bound Without SPI Load

Temporarily disable STM32 SPI input pressure and use a static payload on TX. Keep:

- TX = `RFBound_Start8kDevice()`
- RX = `RFBound_Start8kHost()`
- `RF_8K=1` on both
- Host speed list configured

Goal: get binding success (`sta:0`) before reintroducing SPI.

### 4. Instrument Binding Parameters

Print or hardcode clearly:

- Host `OwnInfo`, `PeerInfo`
- Device `OwnInfo`, `PeerInfo`
- Host speed list `deviceId`, `devType`, `peerInfo`
- Frequency list values
- `hop`
- `timeout`

Current test values:

```c
TX OwnInfo = "HBOXTX"
TX PeerInfo = "HBOXRX"
RX OwnInfo = "HBOXRX"
RX PeerInfo/list peerInfo = "HBOXTX"
frequency list = { 16 }
host hop = RF_HOP_OFF
device speed = 8
```

### 5. Consider Trying No Explicit PeerInfo

Although explicit `OwnInfo/PeerInfo` once produced RX `cb:2 sta:23`, it is not clear if the library expects zeros to mean wildcard or exact match. Test combinations:

- Both `PeerInfo` zero.
- Host list `peerInfo` zero but Host/Device `OwnInfo` nonzero.
- Both `OwnInfo` and `PeerInfo` zero.
- Explicit matching pair as current.

Only vary one thing at a time.

### 6. Watch SPI Load

When RF debug/retry is active, SPI logs showed overruns and effective Hz around 6.7-7.0 kHz. Once binding is solved, revisit SPI under final RF path.

## Known Status Codes Observed

- `RFBound_Start8kDevice ret:3`
  - Caused by missing `RF_8K=1`, Clock1 callback NULL.

- `RFBound_Start8kHost ret:4`
  - Caused by missing `RFBound_SetSpeedType()` / `pSpeedList == NULL`.

- Device callback `sta:1 role:1`
  - Device-side bind failure.

- Host callback `sta:23 role:0`
  - Host-side timeout (`0x17 = BOUND_STA_TIMEOUT`).

- `sta:255 role:255`
  - No binding callback has happened yet; debug default.

## Suggested Immediate Next Step

Create a branch or local experiment that disables current FAST retry churn and implements a minimal Basic fixed-frequency RF TX/RX sanity test using the same boards and frequency `16`.

If Basic works:

1. Return to FAST 8K.
2. Disable SPI load temporarily.
3. Start RX Host once.
4. Start TX Device once.
5. Try the `OwnInfo/PeerInfo` matrix above.

If Basic does not work:

Debug RF hardware/channel/power before touching FAST 8K again.

