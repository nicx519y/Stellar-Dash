"""Fail-closed validation of trust headers, manifests and signed OTA packages."""

import hashlib
import json
import os
import re
import struct
import zipfile
from pathlib import Path
from typing import Any, Dict, Optional, Tuple

from firmware_metadata import (
    BOOTLOADER_VERSION, COMPONENT_STRUCT_SIZE, DEVICE_MODEL_STRING,
    FIRMWARE_COMPONENT_COUNT, FIRMWARE_HASH_OFFSET, FIRMWARE_MAGIC,
    FIRMWARE_SECURITY_VERSION, FIRMWARE_SECURITY_VERSION_OFFSET,
    FIRMWARE_SIGNATURE_ALGORITHM_OFFSET, FIRMWARE_SIGNATURE_ECDSA_P256_SHA256,
    FIRMWARE_SIGNATURE_OFFSET, FIRMWARE_SLOT_A, FIRMWARE_SLOT_B,
    FIRMWARE_WEBRESOURCES_OPTIONAL_OFFSET, METADATA_CRC32_OFFSET,
    METADATA_STRUCT_SIZE, METADATA_VERSION_MAJOR, METADATA_VERSION_MINOR,
)
from firmware_signing import canonical_metadata
from .constants import (
    HARDWARE_VERSION_CODE_V2, LATEST_HARDWARE_VERSION, SIGNED_METADATA_FILENAME,
    STM32_OTA_COMPONENTS, STM32_OTA_LAYOUT, _P256_SIGNATURE_RE, _SEMVER_RE,
    _SHA256_RE, _TRUST_HEADER_MARKERS,
)
from .metadata import calculate_crc32, verify_signed_metadata

def require_v2_trust_bundle(
    environment: Optional[Dict[str, str]] = None,
) -> Tuple[Path, str]:
    """Require an approved public-only trust header for a V2 release build."""

    environment = os.environ if environment is None else environment
    raw_path = environment.get("HBOX_TRUST_HEADER", "").strip()
    expected_hash = environment.get(
        "HBOX_TRUST_HEADER_SHA256", ""
    ).strip().lower()
    if not raw_path:
        raise RuntimeError(
            "V2正式发布必须设置HBOX_TRUST_HEADER，禁止发布全零信任开发镜像"
        )
    if not _SHA256_RE.fullmatch(expected_hash):
        raise RuntimeError(
            "V2正式发布必须设置已审批的HBOX_TRUST_HEADER_SHA256"
        )

    header = Path(raw_path).expanduser().resolve()
    if not header.is_file():
        raise RuntimeError(f"HBOX_TRUST_HEADER不存在: {header}")
    data = header.read_bytes()
    if len(data) == 0 or len(data) > 64 * 1024:
        raise RuntimeError("HBOX_TRUST_HEADER大小无效")
    digest = hashlib.sha256(data).hexdigest()
    if digest != expected_hash:
        raise RuntimeError(
            "HBOX_TRUST_HEADER_SHA256与实际公钥bundle不一致"
        )
    text = data.decode("utf-8", errors="strict")
    if any(marker not in text for marker in _TRUST_HEADER_MARKERS):
        raise RuntimeError("HBOX_TRUST_HEADER缺少已配置公钥门禁")
    mask_match = re.search(
        r"#define\s+HBOX_WEBCONFIG_AUTH_KEY_PROVISIONED_MASK\s+"
        r"0x([0-9A-Fa-f]{2})u",
        text,
    )
    if not mask_match or int(mask_match.group(1), 16) == 0:
        raise RuntimeError("WebConfig authorization公钥槽未配置")
    if "PRIVATE KEY" in text or "BEGIN EC" in text or "BEGIN PRIVATE" in text:
        raise RuntimeError("trust header只能包含公钥，检测到私钥材料")
    return header, digest


def load_firmware_release_public_key_from_trust_header(
    header: Path,
) -> bytes:
    """Extract the provisioned SEC1 P-256 release key from a trust header."""

    text = header.read_text(encoding="utf-8", errors="strict")
    match = re.search(
        r"hbox_firmware_release_public_key\s*\[\s*65\s*\]\s*=\s*"
        r"\{(?P<body>.*?)\}\s*;",
        text,
        re.DOTALL,
    )
    if match is None:
        raise RuntimeError(
            "HBOX_TRUST_HEADER缺少hbox_firmware_release_public_key[65]"
        )
    values = re.findall(r"0x([0-9A-Fa-f]{2})(?:u|U)?", match.group("body"))
    if len(values) != 65:
        raise RuntimeError("firmware release公钥必须恰好包含65字节")
    public_key = bytes(int(value, 16) for value in values)
    if public_key[0] != 0x04 or not any(public_key[1:]):
        raise RuntimeError("firmware release公钥不是有效的SEC1未压缩P-256格式")
    return public_key


def _parse_manifest_address(value: Any, field_name: str) -> int:
    if isinstance(value, bool):
        raise ValueError(f"{field_name} 不是有效地址")
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        try:
            return int(value, 0)
        except ValueError as exc:
            raise ValueError(f"{field_name} 不是有效地址: {value}") from exc
    raise ValueError(f"{field_name} 不是有效地址")


def validate_stm32_ota_manifest(manifest: Dict[str, Any]) -> None:
    if not isinstance(manifest, dict):
        raise ValueError("manifest根节点必须是对象")

    version = manifest.get("version")
    if not isinstance(version, str) or not _SEMVER_RE.fullmatch(version):
        raise ValueError("manifest.version必须是三段式版本号")

    slot_value = manifest.get("slot")
    if slot_value not in STM32_OTA_LAYOUT:
        raise ValueError("manifest.slot必须是A或B")
    slot = slot_value

    if manifest.get("hardware_version") != LATEST_HARDWARE_VERSION:
        raise ValueError(
            f"V2 OTA包hardware_version必须是{LATEST_HARDWARE_VERSION}"
        )
    if manifest.get("hardware_version_code") != HARDWARE_VERSION_CODE_V2:
        raise ValueError(
            f"V2 OTA包hardware_version_code必须是0x{HARDWARE_VERSION_CODE_V2:08X}"
        )
    if manifest.get("ota_scope") != "STM32_ONLY":
        raise ValueError("V2 OTA包ota_scope必须是STM32_ONLY")
    if manifest.get("ch585_update") != "MANUAL_INDEPENDENT_FLASH":
        raise ValueError("CH585不得包含在STM32 OTA包内")
    if manifest.get("signature_algorithm") != FIRMWARE_SIGNATURE_ECDSA_P256_SHA256:
        raise ValueError("V2 OTA必须使用ECDSA_P256_SHA256签名")
    if not _SHA256_RE.fullmatch(str(manifest.get("firmware_hash", ""))):
        raise ValueError("V2 OTA缺少有效firmware_hash")
    if not _P256_SIGNATURE_RE.fullmatch(str(manifest.get("signature", ""))):
        raise ValueError("V2 OTA缺少有效P-256 raw签名")
    if not _SHA256_RE.fullmatch(
        str(manifest.get("trust_bundle_sha256", ""))
    ):
        raise ValueError("V2 OTA缺少构建信任bundle SHA-256")
    security_version = manifest.get("security_version")
    if (
        isinstance(security_version, bool)
        or not isinstance(security_version, int)
        or security_version < FIRMWARE_SECURITY_VERSION
    ):
        raise ValueError("V2 OTA安全版本低于门限")
    build_timestamp = manifest.get("build_timestamp")
    if (
        isinstance(build_timestamp, bool)
        or not isinstance(build_timestamp, int)
        or not 0 <= build_timestamp <= 0xFFFFFFFF
    ):
        raise ValueError("V2 OTA缺少有效build_timestamp")
    if not isinstance(manifest.get("webresources_optional"), bool):
        raise ValueError("V2 OTA缺少webresources_optional策略")

    metadata_info = manifest.get("metadata")
    if not isinstance(metadata_info, dict):
        raise ValueError("V2 OTA缺少signed metadata描述")
    if metadata_info.get("file") != SIGNED_METADATA_FILENAME:
        raise ValueError(
            f"signed metadata文件必须是{SIGNED_METADATA_FILENAME}"
        )
    if metadata_info.get("size") != METADATA_STRUCT_SIZE:
        raise ValueError(
            f"signed metadata大小必须是{METADATA_STRUCT_SIZE}字节"
        )
    if not _SHA256_RE.fullmatch(str(metadata_info.get("sha256", ""))):
        raise ValueError("signed metadata缺少有效SHA-256")

    components = manifest.get("components")
    if not isinstance(components, list):
        raise ValueError("manifest缺少components列表")
    if not all(isinstance(component, dict) for component in components):
        raise ValueError("manifest.components成员必须是对象")
    names = tuple(component.get("name") for component in components)
    if len(names) != len(STM32_OTA_COMPONENTS) or set(names) != set(STM32_OTA_COMPONENTS):
        raise ValueError(
            "STM32 OTA只允许三个组件: " + ", ".join(STM32_OTA_COMPONENTS)
        )

    for component in components:
        name = component["name"]
        optional_webresources = (
            name == "webresources"
            and manifest["webresources_optional"]
            and component.get("active") is False
            and component.get("size") == 0
        )
        if optional_webresources:
            if component.get("file") != "" or component.get("file_type") != "none":
                raise ValueError("可选webresources必须使用空文件和none类型")
            if component.get("sha256") != "0" * 64:
                raise ValueError("可选webresources必须使用零SHA-256占位")
            address = _parse_manifest_address(
                component.get("address"), f"{name}.address"
            )
            expected_address, _ = STM32_OTA_LAYOUT[slot][name]
            if address != expected_address:
                raise ValueError("可选webresources地址错误")
            continue
        if not isinstance(component.get("file"), str) or not component["file"]:
            raise ValueError(f"组件 {name} 缺少有效文件名")
        if Path(component["file"]).name != component["file"]:
            raise ValueError(f"组件 {name} 文件名不能包含路径")
        if component.get("file_type") != "bin":
            raise ValueError(f"组件 {name} file_type必须是bin")

        expected_address, max_size = STM32_OTA_LAYOUT[slot][name]
        address = _parse_manifest_address(component.get("address"), f"{name}.address")
        if address != expected_address:
            raise ValueError(
                f"组件 {name} 地址错误: 0x{address:08X}, "
                f"槽{slot}应为0x{expected_address:08X}"
            )

        size = component.get("size")
        if isinstance(size, bool) or not isinstance(size, int) or size <= 0:
            raise ValueError(f"组件 {name} size必须是正整数")
        if size > max_size:
            raise ValueError(
                f"组件 {name} 大小0x{size:X}超过区域上限0x{max_size:X}"
            )

        sha256 = component.get("sha256")
        if not isinstance(sha256, str) or not _SHA256_RE.fullmatch(sha256):
            raise ValueError(f"组件 {name} sha256必须是64位小写十六进制")


def _decode_fixed_metadata_text(
    metadata: bytes,
    offset: int,
    width: int,
    field_name: str,
) -> str:
    encoded = metadata[offset : offset + width]
    content, separator, padding = encoded.partition(b"\0")
    if not separator or any(padding):
        raise ValueError(f"metadata.bin字段{field_name}不是规范零填充")
    try:
        return content.decode("utf-8", errors="strict")
    except UnicodeDecodeError as exc:
        raise ValueError(f"metadata.bin字段{field_name}不是有效UTF-8") from exc


def validate_signed_metadata_matches_manifest(
    metadata: bytes,
    manifest: Dict[str, Any],
    firmware_release_public_key: bytes | None = None,
) -> None:
    """Require metadata.bin to be the exact signed boot contract in manifest."""

    if len(metadata) != METADATA_STRUCT_SIZE:
        raise ValueError(
            f"metadata.bin大小错误: {len(metadata)} != {METADATA_STRUCT_SIZE}"
        )
    if struct.unpack_from("<I", metadata, 0)[0] != FIRMWARE_MAGIC:
        raise ValueError("metadata.bin magic错误")
    if struct.unpack_from("<I", metadata, 4)[0] != METADATA_VERSION_MAJOR:
        raise ValueError("metadata.bin major version错误")
    if struct.unpack_from("<I", metadata, 8)[0] != METADATA_VERSION_MINOR:
        raise ValueError("metadata.bin minor version错误")
    if struct.unpack_from("<I", metadata, 12)[0] != METADATA_STRUCT_SIZE:
        raise ValueError("metadata.bin声明大小错误")
    stored_crc = struct.unpack_from("<I", metadata, METADATA_CRC32_OFFSET)[0]
    expected_crc = calculate_crc32(
        metadata, METADATA_CRC32_OFFSET, 4
    )
    if stored_crc == 0 or stored_crc != expected_crc:
        raise ValueError("metadata.bin CRC32错误")

    if _decode_fixed_metadata_text(metadata, 20, 32, "firmware_version") != manifest["version"]:
        raise ValueError("metadata.bin firmware_version与manifest不一致")
    expected_slot = (
        FIRMWARE_SLOT_A
        if str(manifest["slot"]).upper() == "A"
        else FIRMWARE_SLOT_B
    )
    if metadata[52] != expected_slot:
        raise ValueError("metadata.bin target_slot与manifest不一致")
    if _decode_fixed_metadata_text(metadata, 53, 32, "build_date") != manifest["build_date"]:
        raise ValueError("metadata.bin build_date与manifest不一致")
    if struct.unpack_from("<I", metadata, 85)[0] != manifest["build_timestamp"]:
        raise ValueError("metadata.bin build_timestamp与manifest不一致")
    if _decode_fixed_metadata_text(metadata, 89, 32, "device_model") != DEVICE_MODEL_STRING:
        raise ValueError("metadata.bin device_model错误")
    if struct.unpack_from("<I", metadata, 121)[0] != manifest["hardware_version_code"]:
        raise ValueError("metadata.bin hardware_version与manifest不一致")
    if struct.unpack_from("<I", metadata, 125)[0] != BOOTLOADER_VERSION:
        raise ValueError("metadata.bin bootloader_min_version错误")
    if struct.unpack_from("<I", metadata, 129)[0] != FIRMWARE_COMPONENT_COUNT:
        raise ValueError("metadata.bin component_count必须为3")

    component_offset = 133
    for index, component in enumerate(manifest["components"]):
        base = component_offset + index * COMPONENT_STRUCT_SIZE
        if _decode_fixed_metadata_text(metadata, base, 32, f"components[{index}].name") != component["name"]:
            raise ValueError("metadata.bin组件name与manifest不一致")
        if _decode_fixed_metadata_text(metadata, base + 32, 64, f"components[{index}].file") != component["file"]:
            raise ValueError("metadata.bin组件file与manifest不一致")
        if struct.unpack_from("<I", metadata, base + 96)[0] != _parse_manifest_address(
            component["address"], f"{component['name']}.address"
        ):
            raise ValueError("metadata.bin组件address与manifest不一致")
        if struct.unpack_from("<I", metadata, base + 100)[0] != component["size"]:
            raise ValueError("metadata.bin组件size与manifest不一致")
        if _decode_fixed_metadata_text(
            metadata, base + 104, 65, f"components[{index}].sha256"
        ) != component["sha256"]:
            raise ValueError("metadata.bin组件SHA-256与manifest不一致")
        expected_active = 1 if component.get("active", True) else 0
        if metadata[base + 169] != expected_active:
            raise ValueError("metadata.bin组件active与manifest不一致")

    embedded_hash = metadata[
        FIRMWARE_HASH_OFFSET : FIRMWARE_HASH_OFFSET + 32
    ]
    expected_hash = hashlib.sha256(canonical_metadata(metadata)).digest()
    if embedded_hash != expected_hash:
        raise ValueError("metadata.bin firmware_hash错误")
    if embedded_hash.hex() != manifest["firmware_hash"]:
        raise ValueError("metadata.bin firmware_hash与manifest不一致")
    embedded_signature = metadata[
        FIRMWARE_SIGNATURE_OFFSET : FIRMWARE_SIGNATURE_OFFSET + 64
    ]
    if (
        not any(embedded_signature[:32])
        or not any(embedded_signature[32:])
    ):
        raise ValueError("metadata.bin包含零值P-256签名分量")
    if embedded_signature.hex() != manifest["signature"]:
        raise ValueError("metadata.bin signature与manifest不一致")
    if struct.unpack_from(
        "<I", metadata, FIRMWARE_SIGNATURE_ALGORITHM_OFFSET
    )[0] != manifest["signature_algorithm"]:
        raise ValueError("metadata.bin signature_algorithm与manifest不一致")
    if struct.unpack_from(
        "<I", metadata, FIRMWARE_SECURITY_VERSION_OFFSET
    )[0] != manifest["security_version"]:
        raise ValueError("metadata.bin security_version与manifest不一致")
    if metadata[FIRMWARE_WEBRESOURCES_OPTIONAL_OFFSET] != int(
        manifest["webresources_optional"]
    ):
        raise ValueError("metadata.bin webresources_optional与manifest不一致")
    if any(metadata[FIRMWARE_WEBRESOURCES_OPTIONAL_OFFSET + 1 :]):
        raise ValueError("metadata.bin reserved区域必须全零")

    if firmware_release_public_key is not None:
        verify_signed_metadata(metadata, firmware_release_public_key)


def validate_stm32_ota_package(
    package_path: Path,
    firmware_release_public_key: bytes | None = None,
) -> Dict[str, Any]:
    with zipfile.ZipFile(package_path, "r") as archive:
        if any(info.is_dir() for info in archive.infolist()):
            raise ValueError(f"{package_path.name} 不允许包含目录条目")
        infos = [info for info in archive.infolist() if not info.is_dir()]
        archive_names = [info.filename for info in infos]
        if len(archive_names) != len(set(archive_names)):
            raise ValueError(f"{package_path.name} 包含重复ZIP条目")
        if any(Path(name).name != name for name in archive_names):
            raise ValueError(f"{package_path.name} ZIP条目不得包含路径")

        try:
            manifest = json.loads(archive.read("manifest.json").decode("utf-8"))
        except KeyError as exc:
            raise ValueError(f"{package_path.name} 缺少 manifest.json") from exc
        validate_stm32_ota_manifest(manifest)

        metadata_info = manifest["metadata"]
        expected_files = {"manifest.json", metadata_info["file"]}
        component_files = [
            component["file"]
            for component in manifest["components"]
            if component.get("active", True) and component.get("size", 0) > 0
        ]
        if len(component_files) != len(set(component_files)):
            raise ValueError("三个组件必须使用不同的文件名")
        expected_files.update(component_files)
        archive_files = {info.filename for info in archive.infolist() if not info.is_dir()}
        if archive_files != expected_files:
            extras = sorted(archive_files - expected_files)
            missing = sorted(expected_files - archive_files)
            raise ValueError(
                f"OTA包文件集合不匹配; extra={extras or 'none'} missing={missing or 'none'}"
            )

        metadata = archive.read(metadata_info["file"])
        if len(metadata) != metadata_info["size"]:
            raise ValueError(
                "metadata.bin size不一致: "
                f"manifest={metadata_info['size']} zip={len(metadata)}"
            )
        actual_metadata_sha256 = hashlib.sha256(metadata).hexdigest()
        if actual_metadata_sha256 != metadata_info["sha256"]:
            raise ValueError(
                "metadata.bin SHA-256不一致: "
                f"manifest={metadata_info['sha256']} "
                f"zip={actual_metadata_sha256}"
            )
        validate_signed_metadata_matches_manifest(
            metadata, manifest, firmware_release_public_key
        )

        for component in manifest["components"]:
            if not component.get("active", True) and component.get("size") == 0:
                continue
            data = archive.read(component["file"])
            if len(data) != component["size"]:
                raise ValueError(
                    f"组件 {component['name']} size不一致: "
                    f"manifest={component['size']} zip={len(data)}"
                )
            actual_sha256 = hashlib.sha256(data).hexdigest()
            if actual_sha256 != component["sha256"]:
                raise ValueError(
                    f"组件 {component['name']} SHA-256不一致: "
                    f"manifest={component['sha256']} zip={actual_sha256}"
                )

        return manifest
