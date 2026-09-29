"""Shared include paths and bounded native compiler/test stages for STM32 tests."""

from pathlib import Path
import os
import signal
import subprocess
import time


ROOT = Path(__file__).resolve().parents[2]


def application_include_dirs():
    """Read the same ordered project include directories as the firmware build."""
    manifest = (ROOT / "application/source_files.mk").read_text(encoding="utf-8")
    declaration = manifest.split("APPLICATION_INCLUDE_DIRS :=", 1)[1].split("\n\n", 1)[0]
    directories = declaration.replace("\\", "").split()
    return [ROOT / "application" / directory for directory in directories]


def application_include_flags():
    return [f"-I{directory}" for directory in application_include_dirs()]


def run_native(command, *, capture_output=False, check=False, timeout=None, **kwargs):
    """Preserve subprocess.run results while bounding each owned process tree."""
    compiler = any(name in Path(command[0]).name.lower() for name in ("gcc", "g++", "clang"))
    phase = "compile" if compiler else "execute"
    limit = timeout if timeout is not None else (120 if compiler else 30)
    if capture_output:
        if "stdout" in kwargs or "stderr" in kwargs:
            raise ValueError("capture_output cannot be combined with stdout/stderr")
        kwargs.update(stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    input_data = kwargs.pop("input", None)
    if input_data is not None:
        kwargs["stdin"] = subprocess.PIPE
    started = time.monotonic()
    process = subprocess.Popen(command, start_new_session=os.name != "nt", **kwargs)
    print(f"native {phase}: PID={process.pid} timeout={limit}s", flush=True)
    try:
        stdout, stderr = process.communicate(input=input_data, timeout=limit)
    except subprocess.TimeoutExpired:
        if os.name == "nt":
            subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                           capture_output=True, timeout=15, check=False)
        else:
            os.killpg(process.pid, signal.SIGKILL)
        stdout, stderr = process.communicate(timeout=15)
        print(f"native {phase}: TIMEOUT after {time.monotonic() - started:.2f}s", flush=True)
        raise subprocess.TimeoutExpired(command, limit, output=stdout, stderr=stderr)
    result = subprocess.CompletedProcess(command, process.returncode, stdout, stderr)
    print(f"native {phase}: exit={result.returncode} elapsed={time.monotonic() - started:.2f}s", flush=True)
    if check:
        result.check_returncode()
    return result
