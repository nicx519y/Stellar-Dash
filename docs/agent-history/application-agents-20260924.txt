# AGENTS - application logging notes

## Logging switches

- Treat `application/Makefile` as the authoritative logging configuration for Makefile builds.
- Use `APP_LOG_ENABLE` for normal UART logging. It drives:
  - `APPLICATION_SERIAL_PRINT`
  - `APPLICATION_DEBUG_PRINT`
- Use `APP_LOG_VERBOSE` only for noisy diagnostics. It drives:
  - `APP_LOG_VERBOSE`
  - `USB_DEBUG_PRINT`
  - `RF_SPI_PROTOCOL_LOG`
  - `RF_COMMAND_TRANSACTION_LOG`
  - `RF_RELIABLE_EVENT_LOG`
  - `ROTENC_DEBUG_PRINT`
  - `CFG_TUSB_DEBUG`
- If `APP_LOG_ENABLE=0`, `APP_LOG_VERBOSE` is forced to `0` in the Makefile.
- Do not add new independent `*_DEBUG_PRINT` or `*_LOG` switches unless there is a strong reason. Prefer wiring module diagnostics to `APP_LOG_VERBOSE`.
- `board_cfg.h` keeps fallback defaults only for IDE or ad-hoc builds that do not pass Makefile `-D` flags.

## HardFault logging

- `HardFault_Handler()` prints fault registers through `APP_DBG` when `APP_LOG_ENABLE=1`.
- The `Logger_*` / `LOG_*` API in `common/system_logger.h` is currently a stub and does not persist logs to QSPI.
