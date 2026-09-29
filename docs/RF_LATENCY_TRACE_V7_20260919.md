# RF latency trace v7 — 2026-09-19

## Measurement boundary

The source timestamp is the ADC sampling trigger associated with the first processed button-state change. It is not physical switch travel or the instant an analog voltage crosses a threshold before sampling. STM32 carries the original 32-bit microsecond timestamp through TX/RX. Windows uses a dedicated native XInput worker, independent of browser focus and renderer rendering.

The Windows column bounds when the matching state became observable through XInput, between the previous read and the first changed read. This covers the SPI/RF/receiver/USB/Windows path. It is not an instrumented USB host-controller completion timestamp or game-engine response. A range is shown, not a falsely precise scalar.

## Protocol and compatibility

- STM32 normal SPI INPUT_DATA remains 10 payload bytes. Independent CMD 0x09 (edge) or 0x0A (sync echo), each 9 payload bytes, can follow in the same 27-byte DMA transfer; each frame has its own checksum.
- Edge payload: sequence8, tagged keyMask32, original sampleTimeUs32. Mask bit31 identifies v3; bit30 marks a baseline. Sync payload: sequence8, STM32 receiveTimeUs32, sendTimeUs32.
- Existing 12-byte RF control packets transport diagnostics; ordinary 7-byte RF input packets remain unchanged. Diagnostics may be lost, and never request input retransmission or pairing changes.
- RX build ID 0x1907: RHC3/RHE3 HID reports are 32 bytes, version3 at offset4, sequence at5, two uint32 fields at8/12, FIFO drop count at16, CRC8 at31 over bytes0..30.
- Trace HID FIFO drains independently of the 100ms statistics period, after normal RF/input service. Sync uses the existing ACK channel, below configuration priority.
- Trace capture is leased for 3 seconds by sync requests. The monitor sends sync only with a compatible RX and one HID control interface. Three matching firmware components are required; old RX remains usable for local stage diagnostics.
- Fixed pairing, 8K default, fixed channel39 and disabled auto-hop are retained. RX remains forced Full-Speed USB with 1ms XInput interval.

## Clock and matching rules

Sync offset PC−STM32 is bounded by [PC_send−STM_receive, PC_receive−STM_send]; no symmetric path assumption. Intersect recent intervals with 250ppm oscillator allowance and 20us timestamp capture margin. The drift allowance is an engineering bound requiring hardware validation; it is not a laboratory calibration guarantee. Intervals older than8s or wider than10ms do not produce a measurement. RTT, edge age and sync age drift are included.

Updated during v8 investigation: every connected XInput slot is tracked independently; only a unique transition across all slots can match. An idle second controller no longer disables measurement. Identical candidate transitions on different slots are rejected. This is state/time correlation, not VID/PID identity binding. Native Guide button is unavailable through XInputGetState. Trigger threshold is128. State transitions must uniquely match both previous and next masks, event order and the timestamp interval; this is conservative correlation, not a shared event ID in the XInput report.

Duplicate, reordered, missing-sequence, dropped, ambiguous, disconnected and stale records are rejected. Trace metadata older than50ms is rejected. Results wait until the100ms matching window plus50ms delivery allowance closes so competing events can be excluded. This display delay is not included in measured latency. Poll intervals over20ms are rejected. Rapid transitions between polls may be unobservable and generate no result.

The worker requests timeBeginPeriod(1) while running and balances it on normal stop. Actual read intervals are measured, never assumed1ms. A short local Windows check observed heartbeat-sampled intervals P50 1.54ms / P95 2.685ms / max3.123ms; these are host scheduling observations, not device latency acceptance results.

## Legacy display

RHL2 local stage durations remain available. Saturated STM32/TX fields display SAT instead of an exact6.02ms. Local stage sums are excluded from the Windows column and summary because they omit transport boundaries. This release fixes measurement correctness; it does not establish that actual STM32 processing delay has been reduced.

## Validation

- 30 monitor tests: matching, gaps, duplicates, competing origins, timestamps/sequence wrap, multiple controllers, unplug, stale clock/poll/metadata, CRC/version, saturated display, native buttons and telemetry diagnostics.
- 31 native/Python tests: actual SPI serialization, clock wrap/inactivity, trace FIFO bounds, RF reliability/recovery and frozen flash contract.
- Monitor typecheck and production build; STM32 accepted unlocked-development build; TX/RX native builds.
- Full physical edge-to-Windows validation awaits the user's RX v7 flash. No two-hour RF stability or end-to-end latency acceptance is claimed.

Deployment results and SHA-256 are recorded in .hbox/rf-latency-v7-20260919/manifest.json.
