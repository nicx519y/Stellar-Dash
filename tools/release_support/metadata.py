"""Create and verify the exact signed metadata bytes used by XORA releases."""

import struct
import subprocess
import tempfile
import time
import zlib
from pathlib import Path

from firmware_metadata import (
    BOOTLOADER_VERSION, DEVICE_MODEL_STRING, FIRMWARE_COMPONENT_COUNT,
    FIRMWARE_MAGIC, FIRMWARE_SECURITY_VERSION,
    FIRMWARE_SIGNATURE_ECDSA_P256_SHA256, FIRMWARE_SIGNATURE_OFFSET,
    FIRMWARE_SLOT_A, FIRMWARE_SLOT_B, HARDWARE_VERSION,
    METADATA_STRUCT_SIZE, METADATA_VERSION_MAJOR, METADATA_VERSION_MINOR,
)
from firmware_signing import canonical_metadata, raw_signature_to_der, sign_metadata

def verify_signed_metadata(
    metadata: bytes,
    firmware_release_public_key: bytes,
) -> None:
    """Verify the exact metadata signature using an approved release key."""

    if len(metadata) != METADATA_STRUCT_SIZE:
        raise ValueError(
            f"metadata.bin大小错误: {len(metadata)} != {METADATA_STRUCT_SIZE}"
        )
    if (
        len(firmware_release_public_key) != 65
        or firmware_release_public_key[0] != 0x04
    ):
        raise ValueError("firmware release公钥格式无效")

    # id-ecPublicKey + prime256v1 followed by a 65-byte SEC1 BIT STRING.
    public_der = (
        b"\x30\x59"
        b"\x30\x13"
        b"\x06\x07\x2A\x86\x48\xCE\x3D\x02\x01"
        b"\x06\x08\x2A\x86\x48\xCE\x3D\x03\x01\x07"
        b"\x03\x42\x00"
        + firmware_release_public_key
    )
    signature = metadata[
        FIRMWARE_SIGNATURE_OFFSET :
        FIRMWARE_SIGNATURE_OFFSET + 64
    ]
    if not any(signature[:32]) or not any(signature[32:]):
        raise ValueError("metadata.bin包含零值P-256签名分量")

    with tempfile.TemporaryDirectory(prefix="hbox-metadata-verify-") as temp:
        directory = Path(temp)
        public_file = directory / "release-public.der"
        metadata_file = directory / "metadata.canonical.bin"
        signature_file = directory / "metadata-signature.der"
        public_file.write_bytes(public_der)
        metadata_file.write_bytes(canonical_metadata(metadata))
        signature_file.write_bytes(raw_signature_to_der(signature))
        try:
            result = subprocess.run(
                [
                    "openssl",
                    "dgst",
                    "-sha256",
                    "-verify",
                    str(public_file),
                    "-keyform",
                    "DER",
                    "-signature",
                    str(signature_file),
                    str(metadata_file),
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
        except FileNotFoundError as exc:
            raise ValueError("OpenSSL不可用，无法验证metadata签名") from exc
    if result.returncode != 0:
        detail = result.stderr.decode("utf-8", errors="replace").strip()
        raise ValueError(f"metadata.bin发布签名验证失败: {detail or 'invalid signature'}")


def calculate_crc32(data, skip_offset=None, skip_size=None):
    """计算CRC32校验和，可以跳过指定区域"""
    if skip_offset is None:
        return zlib.crc32(data) & 0xffffffff

    # 跳过指定区域计算CRC32
    before_skip = data[:skip_offset]
    after_skip = data[skip_offset + skip_size:]

    crc = zlib.crc32(before_skip) & 0xffffffff
    crc = zlib.crc32(after_skip, crc) & 0xffffffff
    return crc


def create_metadata_binary(
    version,
    slot,
    build_date,
    components,
    *,
    signing_key=None,
    security_version=FIRMWARE_SECURITY_VERSION,
    webresources_optional=False,
    build_timestamp=None,
):
    """创建并签名与C结构体严格对齐的V2元数据。"""

    print(f"创建元数据二进制: 版本={version}, 槽位={slot}, 组件数={len(components)}")

    # 准备二进制数据缓冲区 - 使用统一的大小定义
    metadata_buffer = bytearray(METADATA_STRUCT_SIZE)
    offset = 0

    # === 安全校验区域 ===
    # magic (uint32_t)
    struct.pack_into('<I', metadata_buffer, offset, FIRMWARE_MAGIC)
    offset += 4

    # metadata_version_major (uint32_t)
    struct.pack_into('<I', metadata_buffer, offset, METADATA_VERSION_MAJOR)
    offset += 4

    # metadata_version_minor (uint32_t)
    struct.pack_into('<I', metadata_buffer, offset, METADATA_VERSION_MINOR)
    offset += 4

    # metadata_size (uint32_t) - 使用统一的大小定义
    struct.pack_into('<I', metadata_buffer, offset, METADATA_STRUCT_SIZE)
    offset += 4

    # metadata_crc32 (uint32_t) - 先填0，最后计算
    crc32_offset = offset
    struct.pack_into('<I', metadata_buffer, offset, 0)
    offset += 4

    # === 固件信息区域 ===
    # firmware_version (char[32])
    version_bytes = version.encode('utf-8')[:31]
    metadata_buffer[offset:offset+len(version_bytes)] = version_bytes
    offset += 32

    # target_slot (uint8_t) - 1字节，无padding
    slot_value = FIRMWARE_SLOT_A if slot.upper() == 'A' else FIRMWARE_SLOT_B
    struct.pack_into('<B', metadata_buffer, offset, slot_value)
    offset += 1

    # build_date (char[32]) - 紧接着target_slot，无padding
    build_date_bytes = build_date.encode('utf-8')[:31]
    metadata_buffer[offset:offset+len(build_date_bytes)] = build_date_bytes
    offset += 32

    # build_timestamp (uint32_t)
    timestamp = int(time.time()) if build_timestamp is None else int(build_timestamp)
    if timestamp < 0 or timestamp > 0xFFFFFFFF:
        raise ValueError("build_timestamp必须是uint32")
    struct.pack_into('<I', metadata_buffer, offset, timestamp)
    offset += 4

    # === 设备兼容性区域 ===
    # device_model (char[32])
    device_model_bytes = DEVICE_MODEL_STRING.encode('utf-8')[:31]
    metadata_buffer[offset:offset+len(device_model_bytes)] = device_model_bytes
    offset += 32

    # hardware_version (uint32_t)
    struct.pack_into('<I', metadata_buffer, offset, HARDWARE_VERSION)
    offset += 4

    # bootloader_min_version (uint32_t)
    struct.pack_into('<I', metadata_buffer, offset, BOOTLOADER_VERSION)
    offset += 4

    # === 组件信息区域 ===
    # component_count (uint32_t)
    struct.pack_into('<I', metadata_buffer, offset, len(components))
    offset += 4

    # components array - 每个组件170字节，无padding（因为使用__attribute__((packed))）
    for i in range(FIRMWARE_COMPONENT_COUNT):
        component_start = offset

        if i < len(components):
            comp = components[i]

            # name (char[32])
            name_bytes = comp['name'].encode('utf-8')[:31]
            metadata_buffer[offset:offset+len(name_bytes)] = name_bytes
            offset += 32

            # file (char[64])
            file_bytes = comp['file'].encode('utf-8')[:63]
            metadata_buffer[offset:offset+len(file_bytes)] = file_bytes
            offset += 64

            # address (uint32_t)
            address = int(comp['address'], 16) if isinstance(comp['address'], str) else comp['address']
            struct.pack_into('<I', metadata_buffer, offset, address)
            offset += 4

            # size (uint32_t)
            struct.pack_into('<I', metadata_buffer, offset, comp['size'])
            offset += 4

            # sha256 (char[65])
            sha256_bytes = comp['sha256'].encode('utf-8')[:64]
            metadata_buffer[offset:offset+len(sha256_bytes)] = sha256_bytes
            offset += 65

            # active (bool/uint8_t)
            struct.pack_into('<B', metadata_buffer, offset, 1 if comp.get('active', True) else 0)
            offset += 1

        else:
            # 未使用的组件槽位填充为0
            offset += 32 + 64 + 4 + 4 + 65 + 1  # 170字节

        # 确保每个组件占用170字节（包括编译器padding）
        # 但由于使用了__attribute__((packed))，实际上是170字节
        component_actual_size = 32 + 64 + 4 + 4 + 65 + 1  # 170字节
        component_end = component_start + component_actual_size
        if offset < component_end:
            offset = component_end

    # === 安全签名区域 ===
    # firmware_hash (uint8_t[32]) - 签名阶段填充
    firmware_hash_offset = offset
    offset += 32

    # signature (uint8_t[64]) - raw r || s，签名阶段填充
    signature_offset = offset
    offset += 64

    # signature_algorithm (uint32_t)
    struct.pack_into(
        '<I',
        metadata_buffer,
        offset,
        FIRMWARE_SIGNATURE_ECDSA_P256_SHA256,
    )
    offset += 4

    # === 安全策略/预留区域 ===
    if (
        isinstance(security_version, bool)
        or not isinstance(security_version, int)
        or security_version < FIRMWARE_SECURITY_VERSION
        or security_version > 0xFFFFFFFF
    ):
        raise ValueError("security_version必须是有效且不低于当前门限的uint32")
    struct.pack_into('<I', metadata_buffer, offset, security_version)
    offset += 4
    struct.pack_into(
        '<B', metadata_buffer, offset, 1 if webresources_optional else 0
    )
    offset += 1
    offset += 59  # reserved，保持全0并纳入签名

    # 验证总大小
    if offset != METADATA_STRUCT_SIZE:
        raise RuntimeError(
            f"元数据大小不匹配: 期望={METADATA_STRUCT_SIZE}, 实际={offset}"
        )

    digest, signature = sign_metadata(bytes(metadata_buffer), signing_key)
    metadata_buffer[
        firmware_hash_offset : firmware_hash_offset + len(digest)
    ] = digest
    metadata_buffer[
        signature_offset : signature_offset + len(signature)
    ] = signature

    # 计算并设置CRC32校验和（跳过CRC字段本身）
    crc32_value = calculate_crc32(metadata_buffer, crc32_offset, 4)
    struct.pack_into('<I', metadata_buffer, crc32_offset, crc32_value)

    # 添加调试输出，显示关键字段的偏移
    print(f"Python字段偏移计算:")
    print(f"  - magic: 0")
    print(f"  - metadata_version_major: 4")
    print(f"  - metadata_version_minor: 8")
    print(f"  - metadata_size: 12")
    print(f"  - metadata_crc32: 16")
    print(f"  - firmware_version: 20")
    print(f"  - target_slot: 52 (1字节)")
    print(f"  - build_date: 53")
    print(f"  - build_timestamp: 85")
    print(f"  - device_model: 89")
    print(f"  - hardware_version: 121")
    print(f"  - bootloader_min_version: 125")
    print(f"  - component_count: 129")
    print(f"  - components[0]: 133")

    print(f"元数据生成完成:")
    print(f"  - 魔数: 0x{FIRMWARE_MAGIC:08X}")
    print(f"  - 版本: {version}")
    print(f"  - 槽位: {slot}")
    print(f"  - 设备型号: {DEVICE_MODEL_STRING}")
    print(f"  - 组件数量: {len(components)}")
    print(f"  - 安全版本: {security_version}")
    print(f"  - 签名算法: ECDSA-P256-SHA256")
    print(f"  - CRC32: 0x{crc32_value:08X}")
    print(f"  - 总大小: {len(metadata_buffer)} 字节")

    return bytes(metadata_buffer)
