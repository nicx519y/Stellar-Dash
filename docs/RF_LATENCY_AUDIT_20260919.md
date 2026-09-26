# RF latency audit — 2026-09-19

## Confirmed observations

- User confirmed that the missing button visualization was caused by connect-monitor lacking focus. Physical Down was observed in both RX RHI1 (`keyMask=2`) and Windows XInput (`buttons=2`). This is not evidence of an input transport outage.
- Read-only Windows hub connection query (`.hbox/rf-investigation-20260919/usb_speed_readonly.py`) reports receiver VID:PID `045E:02FF`, **Full-Speed**, configuration 1, XInput IN endpoint `0x82`, `bInterval=1`.
- This is explicitly selected in firmware: `RF_PHY_Hop/RX/APP/include/dongle_config.h` sets `DONGLE_USB_FORCE_FULLSPEED=1`; `ch585_usbhs_device.c::USBHS_Device_Init` applies the Full-Speed mode. It is not currently evidence of a bad cable or host downshift.
- Full-Speed interrupt interval 1 is 1 ms. At High-Speed interval 1 is 125 us. RF DATA at 8 kHz and USB input at 1 kHz are independent rates.
- The captured monitor session contained 126 RHL2 frames: STM32=6016 us in 69, 1280 us in 55, 116 us in 2. All 69 values of 6016 had `latencyStageFlags=3`, i.e. split-valid plus STM32 saturation. Median EPWait across these frames was 941 us. These include telemetry frames without a visible state transition and are not 126 independently validated physical button events.

## STM32 measurement limitation

Current source calculates `age_us` in `RFTransport::sendInput` from the report's ADC trigger cycle stamp to preparation of the SPI input payload. It includes ADC completion, main-loop waiting and input processing; it is not CPU execution time alone, and does not start at physical contact.

TX compresses the 16-bit age into q8. Code 255 decodes to 6016 us and is marked saturated by RX. Due to rounding, code 255 starts at 5952 us; it also represents every larger input age. Therefore the UI's repeated 6.02 ms is **not an exact measurement**, and should be marked saturated rather than used as an ordinary value in averages or percentiles.

The parser retains stage flags in packet events, but the latency table does not display them. Current source uses matching little-endian age fields and cycle-based elapsed arithmetic. No byte-order mismatch was found in source. The installed STM32's uncompressed edge ages and separate ADC/processing timings have not been captured in this investigation, so a real multi-millisecond backlog versus a stale/mismatched timestamp cannot yet be distinguished. Do not claim ADC itself takes 6 ms or blame RF loss for this column.

## RX measurement boundary

`demo_service_xinput_report` waits for EP2 not-busy before calling `USBHS_Endp_DataUp`. With Full-Speed this accounts for the observed near-millisecond EPWait.

`demo_complete_xinput_latency_if_pending` measures:

- IRQ: RF callback stamp to deferred packet processing, not hardware interrupt entry latency alone.
- Decode: deferred processing to XInput report ready, including intervening input FIFO wait.
- EPWait: report ready to USB submission start.
- Submit: duration of the submission function.
- RX: RF callback to submission **start**; Submit is shown separately and is not included in RX or Total.

Total is a sum of local measured segments, not physical press-to-Windows/game latency. It omits the RF interval after TX payload formation until RX callback, SPI transmission after the STM32 age stamp, USB transfer completion and host processing. Saturated components further invalidate treating it as an exact end-to-end number.

## Next changes and validation

1. Correct monitor display/aggregation for saturated values and clarify measurement boundaries.
2. Capture uncompressed STM32 edge age together with ADC trigger/completion, report-ready and SPI submission stamps; keep each edge identified across stages. Establish whether the inflated field is a measurement mismatch or real backlog before tuning the input scheduler.
3. Validate an RX High-Speed build with endpoint 0x82 interval 1. Verify actual Windows enumeration at High-Speed, endpoint completion cadence, RF loss/CRC and short press/release order before accepting it. The potential improvement concerns the ~1 ms USB wait, not a promise of 125 us end-to-end latency.

This audit does not flash firmware, change USB mode, restart the monitor, or enable automatic channel hopping. The preceding native-XInput display work remains local and has not been deployed to the running monitor.

Reference: [Microsoft USB endpoint descriptor documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/usbspec/ns-usbspec-_usb_endpoint_descriptor).
