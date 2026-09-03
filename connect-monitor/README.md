# connect-monitor Design Notes

`connect-monitor` is a PC client for observing HBox USB and wireless input paths.

## 1. Scope

Observed paths:

- Wired mode: `HBox(application) -> USB(XInput) -> PC`
- Wireless mode: `HBox(application) -> RFModule -> dongle -> USB(XInput) -> PC`

Monitoring goals:

- Live connection state
- Packet-level counters and abnormal packet counts
- Report rate, packet loss, and segmented latency
- Error source, code, severity, and frequency

Non-goals:

- Do not change the main XInput report format.
- Do not require WebSocket for XInput-only monitoring.

## 2. Data Sources

Priority order:

1. `application HID telemetry` for wired mode
2. `dongle HID telemetry` for wireless mode
3. Dongle text telemetry for development and debugging
4. PC-side XInput observation for future cross-checking

The application telemetry path uses a separate HID IN interface in the XInput composite device and emits `MON1` binary frames. The dongle telemetry path uses an independent telemetry channel and emits `DMN1` binary frames.

## 3. Client Layers

- Source layer: reads HID or text input and parses raw frames.
- Pipeline layer: buffers events, stores history, and broadcasts batches.
- UI layer: renders status cards, charts, packet logs, channel events, and error logs.

Key files:

- `electron/main.ts`
- `electron/preload.ts`
- `electron/pipeline/event-bus.ts`
- `electron/pipeline/event-store.ts`
- `electron/sources/hid-telemetry-source.ts`
- `electron/sources/application-hid-telemetry-source.ts`
- `electron/sources/dongle-hid-telemetry-source.ts`
- `renderer/src/ui/App.tsx`

## 4. Runtime Behavior

1. The client enumerates target HID devices.
2. Matching devices are opened and subscribed through `data` events.
3. Frame magic selects the parser:
   - `MON1` -> application parser
   - `DMN1` -> dongle parser
4. Parsed events are published through `EventBus`.
5. Renderer state derives packet windows, report-rate series, packet-loss series, channel events, and error rows.

## 5. Main-Link Isolation

The dongle keeps the main XInput report path ahead of telemetry:

- Main XInput reports are attempted in real time.
- Telemetry is queued at a lower frequency.
- A full queue drops telemetry rather than blocking input.
- Busy USB endpoints delay or drop telemetry instead of taking priority.

## 6. Configuration

Environment variables:

- `MONITOR_VID`: optional target USB VID. By default the monitor accepts HBox release HID `0x045E:0x02FF` and current RX debug HID `0x1A86:0xFE0C`
- `MONITOR_PID`: optional target USB PID
- `MONITOR_SERIAL_ENABLE`: set to `1` to enable CDC text telemetry; HID telemetry is used by default
- `MONITOR_SERIAL_PATH`: optional CDC serial path, for example `COM8`; also enables CDC text telemetry
- `MONITOR_SERIAL_VID` / `MONITOR_SERIAL_PID`: optional CDC serial VID/PID match
- `MONITOR_SERIAL_BAUD`: CDC serial baud rate, default `115200`

Dependencies:

- `node-hid` is optional and enables HID telemetry collection.
- `serialport` is optional and enables CDC text telemetry collection.

## 7. Current Status

- Application `MON1` telemetry is available.
- Dongle `DMN1` telemetry framing is available.
- PC-side HID collection and parsing are available.
- Renderer dashboards, logs, and Markdown export are available.
