"""Parse legacy release filenames without treating them as trusted metadata."""

import re
from pathlib import Path
from typing import Dict, Optional


def parse_package_info(package_path: str) -> Optional[Dict]:
    """从包名解析版本和槽位信息"""
    package_file = Path(package_path)
    package_name = package_file.name

    # 解析包名格式: hbox_firmware_{version}_{slot}_{timestamp}.zip
    pattern = r'hbox_firmware_(.+)_([ab])_(\d{8}_\d{6})\.zip'
    match = re.match(pattern, package_name)

    if not match:
        return None

    version, slot, timestamp = match.groups()

    return {
        'version': version,
        'slot': slot.upper(),
        'timestamp': timestamp,
        'filename': package_name
    }
