# connect-monitor Implementation Plan

## 1. PC / Electron Client

- `connect-monitor/package.json`
  - Project scripts, dependencies, and runtime entry points.
- `connect-monitor/tsconfig.json`
  - Shared TypeScript configuration.
- `connect-monitor/electron/main.ts`
  - Main process entry, source lifecycle, IPC, persistence, and window controls.
- `connect-monitor/electron/preload.ts`
  - Safe bridge exposing monitor APIs to the renderer.
- `connect-monitor/electron/pipeline/types.ts`
  - Normalized monitor event definitions.
- `connect-monitor/electron/pipeline/event-bus.ts`
  - Event buffering and subscription dispatch.
- `connect-monitor/electron/pipeline/event-store.ts`
  - Runtime event persistence for history queries.
- `connect-monitor/electron/sources/dongle-telemetry-source.ts`
  - Text telemetry parser for development and debugging.
- `connect-monitor/electron/sources/hid-telemetry-source.ts`
  - HID telemetry collection through optional `node-hid`.
- `connect-monitor/electron/sources/application-hid-telemetry-source.ts`
  - Application `MON1` frame parser.
- `connect-monitor/electron/sources/dongle-hid-telemetry-source.ts`
  - Dongle `DMN1` frame parser.
- `connect-monitor/renderer/src/types/monitor.ts`
  - Renderer-side monitor data types.
- `connect-monitor/renderer/src/ui/*`
  - Dashboard panels, charts, virtualized tables, and Markdown export helpers.

## 2. Firmware-Side Integration

- `application/Cpp_Core/Inc/monitor_telemetry.hpp`
  - Probe API and snapshot structures.
- `application/Cpp_Core/Src/monitor_telemetry.cpp`
  - Sequence tracking, timing records, USB/RF latency statistics, and error counters.
- `application/Cpp_Core/Src/states/input_state.cpp`
  - Per-frame sequence creation and report-ready timing.
- `application/Cpp_Core/Src/connection_manager.cpp`
  - Link state and RF send-path telemetry.
- `application/Cpp_Core/Inc/connection_manager.hpp`
  - `onReportReady` sequence parameter.
- `application/Cpp_Core/Src/rf_transport.cpp`
  - RF `INPUT_DATA` transfer telemetry.
- `application/Cpp_Core/Inc/rf_transport.hpp`
  - External sequence parameter for `sendInput`.
- `application/Cpp_Core/Src/usbdriver.cpp`
  - USB report completion telemetry.

## 3. Remaining Work

- Replace dongle `usb_hid_stub.c` weak symbols with the real CH585 endpoint driver.
- Add optional RF-side statistics such as retry count, CRC failures, and RSSI buckets.
- Add PC-side XInput observation and timing alignment.

## 4. Current Status

- Application `MON1` telemetry path is implemented.
- Dongle `DMN1` telemetry framing and queue path are implemented.
- PC-side HID collection and dual-protocol parsing are implemented.
- Renderer panels display status, traffic, report rate, channel events, packet logs, and error logs.
