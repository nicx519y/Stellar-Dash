# connect-monitor Design Notes

`connect-monitor` is a PC client for observing XORA USB and wireless input paths.

Current USB protocol, measurement boundaries and acceptance steps: [USB XInput monitoring](../docs/usb-connect-monitor.md).

Current source ownership, worker boundaries and commands: [AGENTS.md](AGENTS.md) and [architecture](ARCHITECTURE.md). Automatic regression, device sampling and restoring automatic RF hopping remain paused; test examples below are references, not instructions to run them now.

## 1. Scope

Observed paths:

- Wired mode: `XORA STM32 -> SPI BoardLink -> CH585 TX -> USB XInput -> PC`
- Wireless mode: `XORA STM32 -> CH585 TX -> RF -> CH585 RX -> USB XInput -> PC`

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

1. CH585 TX USB XInput telemetry for wired mode
2. CH585 RX HID telemetry for wireless mode
3. Dongle text telemetry for development and debugging
4. PC-side XInput observation for future cross-checking

The USB XInput telemetry interface emits `UMS1` completion statistics, `UME1` button events and existing `MPW2` power reports, controlled by `UMC1`. RF uses its existing `RHM1` statistics and relative latency protocol. `MON1`/`DMN1` remain legacy parsers; a legacy target rate is never treated as measured USB throughput.

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

Start through `npm start` or `npm run dev` (`electron .`) so Electron reads the
package identity and uses `%APPDATA%/connect-monitor` on Windows. Passing
`dist/electron/main.js` directly uses the generic `%APPDATA%/Electron` profile,
which can conflict with other running Electron instances and cause
`Unable to move the cache` / `Gpu Cache Creation failed` errors.
The monitor allows one instance per profile; launching it again focuses the
existing window before opening another telemetry reader or touching its database.
When switching between `npm start` and `npm run dev`, close the existing monitor
first. After upgrading from the old script entry point, close all old monitor
windows once before restarting. The old profile is left untouched; saved debug
settings from that profile are not automatically migrated.

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

- `MONITOR_VID`: optional target USB VID. By default the monitor accepts legacy/release HBox telemetry HID `0x045E:0x028E`, `0x045E:0x02FF`, and current RX debug HID `0x1A86:0xFE0C`. The dedicated WebConfig HID `0xCAFE:0x4021` is always excluded.
- `MONITOR_PID`: optional target USB PID
- `MONITOR_SERIAL_ENABLE`: set to `1` to enable CDC text telemetry; HID telemetry is used by default
- `MONITOR_SERIAL_PATH`: optional CDC serial path, for example `COM8`; also enables CDC text telemetry
- `MONITOR_SERIAL_VID` / `MONITOR_SERIAL_PID`: optional CDC serial VID/PID match
- `MONITOR_SERIAL_BAUD`: CDC serial baud rate, default `115200`

Dependencies:

- `node-hid` is optional and enables HID telemetry collection.
- `serialport` is optional and enables CDC text telemetry collection.

## 7. Current Status

- USB `UMS1`/`UME1` and RF statistics/relative-latency parsing are available; formats and measurement limits follow the producer and matching parser.
- `MON1` and `DMN1` remain legacy parsers, not the current primary measurement protocols.
- PC-side HID collection and parsing are available.
- Renderer dashboards, logs, and Markdown export are available.

## Long-running monitoring diagnostics

Live IPC delivery has one outstanding batch per renderer, at most 500 rows per
batch and 2000 pending rows. A stalled renderer resumes with recent data; older
pending display rows are dropped and counted. Monitor history is still recorded
separately by the event bus. Serial rows dropped before delivery are not written
to the renderer's IndexedDB. Worker snapshots also wait for acknowledgement.

History queries read backwards in 64 KiB blocks instead of loading the entire
session into RAM. Disk-write failures keep live monitoring running and appear as
`historyWriteError` in diagnostics; history may be incomplete after such errors.

`runtime-diagnostics.jsonl` in the Electron user-data directory records process
memory every 30 seconds, queue drops, renderer/child-process exits, and fatal JS
exceptions. It keeps two files of about 2 MiB each (current and `.1`) across
restarts. The default Windows directory is `%APPDATA%/connect-monitor`; a custom
`--user-data-dir` uses that directory instead. Native main-process crashes may
only leave Windows WER records; absence of a JS exception does not rule them out.

Regression checks (after `npm run build`):

```powershell
node --test tests/memory-stability.test.cjs
npx electron tests/electron-stream-smoke.cjs
```

The Electron smoke check uses synthetic input and a separate temporary profile,
including a stalled renderer and reload. It does not open hardware devices.

## USB / RF device binding

Gamepad Buttons follows the selected USB / RF source and identifies controllers by Windows hardware identity. Click its source badge to choose a gamepad and telemetry device when automatic identification is ambiguous. Windows builds now include a C++/WinRT helper and require CMake, the MSVC desktop toolchain, and a Windows SDK. See [device binding and manual acceptance](docs/device-binding.md). Restart the complete monitor after rebuilding; automatic regression and hardware sampling remain paused under AGENTS.md.
