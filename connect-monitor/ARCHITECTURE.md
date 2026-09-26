# connect-monitor Architecture

## 1. Goal

`connect-monitor` observes two HBox input paths from the PC side:

- Wired XInput: device -> USB -> PC
- Wireless XInput: STM32 -> RFModule -> dongle -> USB -> PC

The monitor focuses on:

- Connection state changes
- Packet counters and TX/RX activity
- Report rate targets and measured values
- Error source, severity, count, and context
- Segment latency for device processing, RF transfer, and USB submit

## 2. Data Source Priority

1. `dongle telemetry`
2. PC-side XInput observation
3. `application` internal telemetry
4. USB capture for deeper troubleshooting

XInput-only operation does not require WebSocket by default. If a network interface is added later, WebSocket can become an extra telemetry source rather than a required path.

## 3. Event Model

Renderer and pipeline code consume normalized monitor events:

- `device_status`: connection mode, link state, target rate, measured rate
- `packet`: channel, direction, message type, payload length, RF counters
- `latency`: sequence number and segment latency values
- `error`: source, code, level, message, and optional count

## 4. Application Telemetry

The firmware-side probe module is `monitor_telemetry.*`.

- `MonitorTelemetry_NextSequence()` creates the global sequence.
- `MonitorTelemetry_OnReportReady(seq)` records report-ready timing.
- `MonitorTelemetry_SetPendingUsbSeq(seq)` marks a pending wired frame.
- `MonitorTelemetry_OnUsbReportSubmitted(len)` records USB completion and latency.
- `MonitorTelemetry_OnRfTransfer(seq, cmd, len, ok)` records RF transfer results.
- `MonitorTelemetry_OnLinkStateChanged(mode, state)` records link state changes.
- `MonitorTelemetry_OnError(source, code, message)` normalizes error reporting.

The XInput descriptor includes a separate `HID IN` telemetry interface. `XInputDriver` sends the `MON1` binary frame periodically without changing the main XInput report.

## 5. Client Runtime Flow

1. The main process starts telemetry sources.
2. Each source parses raw input into normalized events.
3. `EventBus` buffers events and stores them for history queries.
4. The renderer subscribes to live batches and renders dashboards, charts, and logs.

Currently connected sources:

- HID telemetry source through optional `node-hid`
- Application `MON1` frame parser
- Dongle `DMN1` frame parser
- Optional serial text telemetry source

## 6. Milestones

- M1: Receive and display dongle/application telemetry with status and errors.
- M2: Show packet windows, report-rate trends, packet-loss trends, and channel events.
- M3: Add PC-side XInput observation and cross-source timing alignment.
