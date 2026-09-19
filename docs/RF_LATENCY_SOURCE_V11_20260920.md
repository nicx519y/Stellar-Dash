# RF latency source repair v11 — verification incomplete

## Status

STM32 and TX were updated through the existing unlocked flows. RX stays at
v10 / build 0x1910; no new RX image is required. The temporary NSS/tag probes
were removed from the final source and image. No protection bits or locking
states were changed; TX's original 4 KB IAP remains byte-identical.

This is a diagnostic repair, **not an accepted latency release**. One complete
idle baseline record was observed during the probe run, with stages
92 / 49 / 52061 / 14 / 336 / 92 / 1708 / 1822 us (56.174 ms total estimate).
That includes a startup/control wait and is not a physical button measurement.
Repeated empty baseline rows and missing source records were also observed.
The final image still needs actual press/release validation; do not claim that
record completeness or latency accuracy has passed.

## Changes

- TX accepts STM32 source durations independently of an available NSS timestamp.
  Missing TX timing stays invalid; the UI must not manufacture a complete sum.
- DMA cursor comparison normalizes SRAM bus offsets and CPU pointer addresses.
  An end flag with no cursor advance no longer causes the whole 1024-byte ring
  to be parsed again. Ambiguous wraps discard parser state instead of replaying
  old input as new events.
- SPI status appends byte 23: 0xA0 disabled, 0xA1 enabled. STM32 recognizes this
  explicit extension, applies changes idempotently, and ignores legacy counters.
  This repairs loss of the one-shot capture notification via existing polling.
- Existing SPI status bytes 21/22 expose source-frame count and capture progress.
- The monitor reapplies the current configuration once on RF connection recovery,
  including a TX-only restart with the RX USB handle still open. Repeated
  Connected reports do not repeatedly send configuration.
- All RF input payloads remain 5/7 bytes. No standalone RF trace/sync packet
  was added. Fixed pairing and the fixed channel 39 baseline are preserved.

## Evidence and limitations

The initial installed STM32 image matched v9 SHA-256, and TX matched v10.
After the changes, SPI status reported source frames received and trace
publication, and a complete baseline reached the desktop. Those observations
do not identify every cause of the remaining repeated/missing rows.

Several old headless monitor processes from previous debugging sessions were
removed; the visible monitor was retained. Their causal effect was not isolated.
SWD RAM observations can lag because STM32 D-cache is enabled; a stagnant RAM
value alone was not proof of a stopped main loop. Core inspection found no
HardFault. A normal reset restored communication after the update workflow.

49 native/contract tests pass, including capture-state recovery, cursor address
formats, delayed DMA end flags and normal ring wrap. Monitor typecheck/build and
33 tests pass, including reconfiguration after RF recovery. These are host tests,
not hardware performance acceptance. The rebuilt monitor needs a restart to run
its new reconnect behavior; measurement remains opt-in on startup.

## Final images

- TX.bin: 89108 bytes, SHA-256
  `1dae75db62d528a5b784295b24c2ab6b1fb96f702cc8ff9c635a009b4be0fa2d`
- STM32 application-slot-a.bin: 384824 bytes, SHA-256
  `454a5cf6558830e006dfae6ac06141adbc0512822a4a90e4255e7d7cc6eaf48b`

Snapshot: `.hbox/rf-latency-v11-20260920/`. Preserve the accepted commands
`python tools/hbox.py flash tx` and `python tools/hbox.py flash app A`; do not
write the combined TX image directly over the IAP region.
