"""Offline service-token loading and request payload construction."""

import os
import re
from pathlib import Path
from .constants import STM32_OTA_COMPONENTS


def get_service_token(token_file: str = None) -> str | None:
    """Read an administrator service token from a dedicated file."""
    configured = token_file or os.getenv('HBOX_ADMIN_SERVICE_TOKEN_FILE')
    if not configured:
        print(
            "错误: 需要 --service-token-file 或 "
            "HBOX_ADMIN_SERVICE_TOKEN_FILE"
        )
        return None
    token_path = Path(configured).expanduser().resolve()
    try:
        token = token_path.read_text(encoding='utf-8').strip()
    except OSError as exc:
        print(f"错误: 无法读取服务令牌文件 {token_path}: {exc}")
        return None
    if not re.fullmatch(r'stsvc_[A-Za-z0-9_-]{43}', token):
        print("错误: 服务令牌文件格式无效")
        return None
    return token


def service_token_headers(token: str | None) -> dict | None:
    if not token:
        return None
    return {"Authorization": f"Bearer {token}"}


def upload_form_data(version: str, description: str, hardware_version: str) -> dict:
    return {
        "version": version,
        "desc": description,
        "hardwareVersion": hardware_version,
        "otaComponents": ",".join(STM32_OTA_COMPONENTS),
        "ch585Update": "manual-independent-flash",
    }


def clear_versions_request(target_version: str, hardware_version: str) -> dict:
    return {"targetVersion": target_version, "hardwareVersion": hardware_version}
