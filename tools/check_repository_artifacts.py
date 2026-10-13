#!/usr/bin/env python3
"""Reject known generated caches in the index without scanning vendor sources."""

from pathlib import Path, PurePosixPath
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
HISTORICAL_BUILD_DIRS = (
    "application/build_rf_recovery_unlocked",
    "application/build_screen_report_rate_unlocked",
    "application/build_tx_backup_dma_unlocked",
    "application/build_size_eval",
    "application/build_size_eval_lto",
    "application/build_size_eval_unlocked",
    "RF_PHY_Hop/TX/build_tx_backup_dma_check",
)
CACHE_SUFFIXES = frozenset({".o", ".d", ".lst", ".elf", ".map"})
GENERATED_FILES = frozenset({
    "RF_PHY_Hop/HAL/MCU.o", "RF_PHY_Hop/HAL/MCU.d",
    "RF_PHY_Hop/HAL/RTC.o", "RF_PHY_Hop/HAL/RTC.d",
    "RF_PHY_Hop/HAL/SLEEP.o", "RF_PHY_Hop/HAL/SLEEP.d",
    "RF_PHY_Hop/LIB/ble_task_scheduler.o", "RF_PHY_Hop/LIB/ble_task_scheduler.d",
    "connect-monitor/renderer/tsconfig.tsbuildinfo",
    "application/www/playwright-report/index.html",
})


def is_generated_cache(path: str) -> bool:
    if path in GENERATED_FILES:
        return True
    file = PurePosixPath(path)
    return any(path.startswith(directory + "/") for directory in HISTORICAL_BUILD_DIRS) and (
        file.suffix in CACHE_SUFFIXES or file.name == ".toolchain_ok"
    )


def main() -> int:
    result = subprocess.run(
        ["git", "ls-files", "-z"], cwd=ROOT, capture_output=True, check=True, timeout=30,
    )
    offenders = sorted(path for path in result.stdout.decode("utf-8").split("\0")
                       if is_generated_cache(path))
    if offenders:
        print("Generated caches remain tracked:\n" + "\n".join(offenders), file=sys.stderr)
        return 1
    print("Repository artifact check passed; historical BIN/HEX images remain tracked.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
