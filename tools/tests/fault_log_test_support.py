"""Bound host-only child process trees and print their identity and duration."""
import os
import signal
import subprocess
import time

def run_checked(command, *, timeout=120, **kwargs):
    start = time.monotonic()
    process = subprocess.Popen(command, start_new_session=os.name != "nt", **kwargs)
    print(f"START host pid={process.pid} timeout={timeout}s: {command[0]}", flush=True)
    try:
        result = process.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        if os.name == "nt":
            subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=15)
        else:
            os.killpg(process.pid, signal.SIGKILL)
        process.wait(timeout=15)
        raise
    finally:
        print(f"END host pid={process.pid} elapsed={time.monotonic()-start:.1f}s exit={process.returncode}", flush=True)
    if result:
        raise subprocess.CalledProcessError(result, command)
