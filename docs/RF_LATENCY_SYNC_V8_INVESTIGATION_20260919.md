# Latency measurement investigation, 2026-09-19

Status: incomplete. Do not label this firmware/monitor combination as accepted end-to-end latency measurement.

## Observations

RX v8 build 0x1908 was confirmed from RHD1 after the user flashed it. RHC4 reports measured request residence before the first ACK preparation. Subtracting this known residence did not make the clock interval sufficiently narrow. An isolated HID reader, without Electron charts, still observed approximately 40 ms residual round trip. This does not prove 40 ms button latency.

Windows exposes two XInput slots. The old tracker rejected every poll with multiple slots, independently preventing matching. Track slots independently now; reject ambiguous candidate transitions rather than rejecting the presence of another controller. Correlation still cannot prove VID/PID identity.

STM32 timestamp capture previously happened after the entire SPI read and IRQ-deassertion wait. Capture at the physically received event header now. The runtime path can read an aligned five-byte TIME_SYNC frame from the TX prefilled FIFO without millisecond refill delays; longer/control responses retain their existing pacing. This did not resolve the residual synchronization interval. Recent live capture: 30 sync replies, clock interval min 19.85 ms, median 37.21 ms, max 46.07 ms, no lock. Keep the 10 ms precision gate; no fabricated latency values.

TX full trace/sync packets use all ten data bytes. ACK requests overwrote the final two bytes with token/reservation. Full trace/sync now remain pending during an ACK request and are sent in a following non-ACK packet. A host test runs the actual packet builder and checks both preserved queue state and exact 32-bit timestamps.

Trace gaps before the RX HID FIFO remain unresolved; a zero RX HID drop count does not prove lossless upstream trace delivery.

## Installed artifacts

- RX: user's v8, build 0x1908; no additional RX flash requested.
- STM32: local unlocked-development artifact, application SHA-256 `3dd36836109717937c9d925e63af002c4a4be5d4ef773362ac276ae3a1a25d49`, flashed through `python tools/hbox.py flash app A`, readback verified, metadata committed last.
- TX: SHA-256 `095f300a341d8d1636e0437d34437ee3ddb4140a1e3f3a26f9617f1f3f81cbe3`, updated through `python tools/hbox.py flash tx`, APPLIED / COMPLETE / 100% verified.
- No protection bits or lock states changed; TX IAP 0x0000–0x0FFF preserved.
- Monitor rebuilt and restarted using the original `%APPDATA%/connect-monitor` profile. Multi-slot matching is installed. Fixed channel 39 / auto-hop off retained.

## Application Fault during investigation

The user reported a Fault. GDB confirmed `InputState.activeBoardMode=Fault`, `inputPipelineRunning=false`; CPU was executing normal InputState/LatencyMonitor code, and CFSR/HFSR were zero. This was application Fault, not CPU HardFault. A normal reset restored RF mode and sampling. Subsequent OpenOCD running-memory read (without halt) confirmed `isRunning=1`, `inputPipelineRunning=1`, `activeBoardMode=1 (Rf)` at the current ELF's InputState singleton.

Debug halts and the update status read are a plausible trigger: the ADC/DMA sampling pipeline can continue while the core cannot service it. The exact pre-reset fault trigger was not preserved, so this is not a proven root cause. Avoid further halting live acquisition to measure latency. Never weaken sampling safety checks just to hide the Fault.

## Evidence and checks

Evidence under `.hbox/rf-investigation-20260919`: `v8-before-isolation.jsonl`, `v8-sync-isolated.json`, `post-spi-tx-events.json`, `latency-spi-flash.log`, `trace-overlap-tx-flash.log`, `check-fault.log`, `check-board-state.log`, `restore-running.log`.

Monitor: 21 latency tests, typecheck and production build passed. Native trace/recovery/reliability/frozen-flash checks passed (33 before adding the packet-overlap regression, plus that regression passed separately). RX v8 and updated TX/STM32 builds passed.

Remaining: locate unmeasured residence on the synchronization path, explain upstream trace gaps, then validate actual button-to-Windows correlation. No end-to-end latency acceptance or stability acceptance claimed.

## Follow-up: RAM correlation and SPI ownership

The STM32 main loop and input pipeline were verified running through non-halting SRAM reads. Diagnostic reads show two paths: aligned runtime sync reads take approximately 6–13 us to the header and 4 us afterwards; prefixed frames can spend 10–22 ms scanning, and status-query reads retain millisecond pacing. Sparse RAM records correlate accepted SPI echo sequence and both timestamps without UART output.

In a 20-second correlation window, 165 distinct accepted STM32 sync echoes were retained, 117 with processing below 2 ms. Only 28 correlated RHC4 reports appeared in monitor history, including one fast echo. This localizes a major loss on the diagnostic return path, but does not yet distinguish SPI from radio. Independent raw HID reads found valid CRCs on received RHC4 frames; no observed monitor-parser CRC rejection explains the missing majority.

TX time-sync notification no longer reconfigures SPI from the RF ISR. An atomic generation/sequence mailbox is consumed after main-loop control parsing, with checks for NSS high, no pending response, no queued controls, and no unread DMA bytes. Requests arriving during an output attempt remain pending. This removes the demonstrated ownership race; it has not restored end-to-end measurement.

A bounded 4 ms post-ACK metadata deferral experiment did not improve delivery and was reverted. Normal input cadence was unchanged during that experiment.

The TX SPI timeout clock was also found to increment by 10 on each function call rather than measure elapsed time. It now uses a private hardware-cycle accumulator, independently of service-call frequency and without calling the SDK scheduler clock. Regression tests cover repeated calls with no elapsed cycles and cycle-counter wrap. No end-to-end measurement acceptance is claimed.

Evidence: `sync-sent-ram.json`, `sync-postack-hid.json`, `sync-clock-hid.json`, and corresponding build/flash logs under `.hbox/rf-investigation-20260919`. New reserved SPI status bytes 21/22 will distinguish parsed SPI echo count from constructed radio echo count; they preserve status frame length and existing command fields.
