# RF Freeze Baseline

Baseline date: 2026-07-23

This baseline protects the existing STM32 -> CH585 TX SPI protocol and the
CH585 TX -> CH585 RX radio implementation while the latest PCB is brought up.
Only board-level CH585 pin, clock, and debug-port configuration may change in
this migration.

Run the optional audit from the repository root:

```powershell
mingw32-make -C RF_PHY_Hop/RX -j4 TOOLCHAIN_BIN="<WCH GCC15 bin>"
python tools/check_rf_frozen.py --require-rx-binary
```

The authoritative source hashes are stored in
`docs/rf_frozen_manifest.sha256`. The reproducible RX binary baseline is stored
separately in `docs/rf_frozen_binaries.sha256`. A strict post-build check is:

```powershell
python tools/check_rf_frozen.py --require-rx-binary
```

Generated artifacts remain outside the source manifest. The source and binary
baselines are retained for explicit regression audits, but normal root,
bootloader, application, CH585 TX, build-tool and release-tool entry points no
longer run this check automatically or block builds when the baseline differs.
The historical pairing/hopping/ACK/8K parameters and trace samples are also
stored in machine-readable form in
`docs/rf_frozen_behavior_baseline.json`; they remain marked as requiring a
latest-PCB hardware rerun.

## Frozen behavior

- STM32 -> TX input payload: 10 bytes.
- STM32 -> TX input frame: 14 bytes (`A5 06 0A`, 10-byte payload, checksum).
- Normal TX -> RX input packet: 7 bytes.
- RF full/control packet: 12 bytes.
- Data rates: Off/1K/2K/4K/8K with an 8K slot of 125 us.
- ACK cadence: 500 ms, three request packets per logical ACK token, and a
  1200 us TX ACK receive timeout.
- Pairing, reconnect, bond, access-address, hopping, scoring, ACK, retry,
  timeout, ISR, and queue behavior remain unchanged.
- Hop table order: `10, 16, 22, 24, 28, 34, 39`.
- Discovery channels: `16, 39`.

The representative 8K/ACK measurements preserved in `RF_PHY_Hop/AGENTS.md`
remain the comparison baseline:

```text
R8 c0 d38557 q10 a10/0 e232/0 x0/0/0 v1
T8 c0 h39 p10 hz8000 due39959 tx39921 fin10 aq10 ack9 to1 fail0 e0/0 dr38 st0
T8 c0 h39 p10 hz8000 due39961 tx39941 fin10 aq10 ack10 to0 fail0 e0/0 dr20 st0
```

These are preserved historical hardware measurements, not a new hardware run.
The same pairing, hopping, ACK, and 8K tests must be rerun on the latest PCB
before release.

## RX reproducible-build check

The frozen RX was force-built on 2026-07-23 with the repository's unchanged
flags and the local WCH GCC15 toolchain.  Its raw binary is byte-for-byte
identical to the pre-migration `build_rx/RF_PHY_Hop_RX.bin` dated 2026-07-05:

```text
size    46952 bytes
sha256  ab6c5e659133797853f81981e8e768c5fc7aede1587968be6709438eb2c8a0c1
```

The ELF hash is not used as a gate because absolute dependency/output paths
are present in its debug records.  The loadable `.bin`, frozen RX sources and
RX Makefile are the reproducibility authorities.

## Golden byte vectors

All values below are hexadecimal. The input vector uses sequence `2A`, format
flags `11`, key mask `00012345`, input age `125 us`, and battery/reserved byte
`00`.

### 10-byte processed input payload

```text
2A 11 45 23 01 00 7D 00 00 21
```

The gate generates this vector from the logical fields and CRC-8/ATM encoder;
it does not only compare its length.

### 14-byte SPI INPUT_DATA frame

```text
A5 06 0A 2A 11 45 23 01 00 7D 00 00 21 F7
```

The gate generates this frame from the 10-byte payload and checksum encoder.

### 7-byte normal RF input packet

```text
78 2A 45 23 01 1F 10
```

This represents DATA/8K/LINK_OK, RF sequence `2A`, the low 24 key-mask bits,
125 us encoded as `1F`, and 64 us encoded as `10`. The gate regenerates both
latency q8 values with the frozen encoder rules.

### 12-byte RF control packet

```text
7B 2A 10 16 00 00 07 27 34 12 55 02
```

This is a full DATA/8K command packet with LINK_OK, CMD_PRESENT, and CMD_ACK
flags. It records an existing-format HOP_PREPARE example only; it does not add
or change a command.

## Frozen source coverage

- Every tracked file under `RF_PHY_Hop/Common/include/`.
- Every tracked file under `RF_PHY_Hop/RX/`.
- TX RF PHY, SPI bridge, command transaction, reliable event, cold-boot, and
  input-stream sources and headers, plus `rfm_config.h`.
- STM32 RF transport, RF command transaction, RF reliable event, and report
  scheduler sources and headers.

`RF_PHY_Hop/TX/APP/rfm_spi_port_ch585.c`, `RF_main.c`, the TX Makefile, and
`TX/BOARD/` are intentionally outside the frozen set. In this migration they
may contain board-level adaptation only.
