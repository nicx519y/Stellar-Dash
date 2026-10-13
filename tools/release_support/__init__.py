"""Offline helpers for the XORA release command. No device or server operations.

Support both the historical tools-directory imports and ``tools.release_support``.
"""

import sys
from pathlib import Path

_PROJECT_ROOT = Path(__file__).resolve().parents[2]
for _directory in (_PROJECT_ROOT / "common", _PROJECT_ROOT / "tools"):
    if str(_directory) not in sys.path:
        sys.path.append(str(_directory))
