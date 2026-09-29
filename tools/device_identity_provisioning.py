#!/usr/bin/env python3
"""HBox V2 device identity manufacturing helpers.

The production path intentionally never accepts or exports a device private
key:

1. the device creates Kdev and a signed PKCS#10 CSR (or a raw public-key
   proof-of-possession);
2. this tool creates the fixed 144-byte certificate TBS;
3. an offline CA/HSM signs the TBS;
4. this tool verifies and assembles the fixed 208-byte certificate; and
5. factory firmware installs that certificate beside Kdev in the linker-
   reserved internal-Flash identity region.

Commands which handle a private scalar are named ``development-*`` and require
an explicit acknowledgement.  They exist for deterministic format tests and
must not be used as a production provisioning workflow.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct
import subprocess
import tempfile
import time
from pathlib import Path
from typing import Sequence

from firmware_signing import der_signature_to_raw, raw_signature_to_der


DEVICE_CERTIFICATE_MAGIC = 0x31434448
DEVICE_CERTIFICATE_VERSION = 1
DEVICE_CERTIFICATE_SIZE = 208
DEVICE_CERTIFICATE_SIGNED_SIZE = 144
DEVICE_CERTIFICATE_SIGNATURE_SIZE = 64
DEFAULT_PRODUCT_ID = "HBOX"
DEFAULT_PRODUCT_ID_CODE = 0x584F4248

DEVICE_IDENTITY_MAGIC = 0x31444948
DEVICE_IDENTITY_VERSION = 1
DEVICE_IDENTITY_RECORD_SIZE = 256
DEVICE_IDENTITY_CRC_OFFSET = 8
DEVICE_IDENTITY_PRIVATE_KEY_OFFSET = 12
DEVICE_IDENTITY_CERTIFICATE_OFFSET = 44
DEVICE_IDENTITY_COMMIT_MAGIC = 0x31434948
DEVICE_IDENTITY_COMMIT_VERSION = 1
DEVICE_IDENTITY_COMMITTED = 0x54494D43
DEVICE_IDENTITY_COMMIT_SIZE = 32
DEVICE_IDENTITY_COMMIT_CRC_OFFSET = 20
DEVICE_IDENTITY_SLOT_SIZE = (
    DEVICE_IDENTITY_RECORD_SIZE + DEVICE_IDENTITY_COMMIT_SIZE
)
DEVICE_IDENTITY_REGION_ADDRESS = 0x0801C000
DEVICE_IDENTITY_REGION_SIZE = 0x1000
DEVICE_IDENTITY_SLOT_COUNT = 14
SECURITY_VERSION_REGION_ADDRESS = 0x0801D000
SECURITY_VERSION_REGION_SIZE = 0x3000
INTERNAL_FLASH_PROGRAM_SIZE = 32

P256_PUBLIC_KEY_SIZE = 65
P256_PRIVATE_KEY_SIZE = 32
P256_ORDER = int(
    "FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551",
    16,
)
P256_FIELD = int(
    "FFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF",
    16,
)
P256_GENERATOR_X = int(
    "6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296",
    16,
)
P256_GENERATOR_Y = int(
    "4FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5",
    16,
)
FACTORY_POP_DOMAIN = b"HBOX-FACTORY-POP-V1\0"


class ProvisioningError(RuntimeError):
    """A fail-closed provisioning or format validation error."""


def _run_openssl(
    arguments: Sequence[str], *, input_data: bytes | None = None
) -> bytes:
    try:
        result = subprocess.run(
            ["openssl", *arguments],
            input=input_data,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
    except FileNotFoundError as exc:
        raise ProvisioningError("OpenSSL was not found") from exc
    if result.returncode != 0:
        message = result.stderr.decode("utf-8", errors="replace").strip()
        raise ProvisioningError(f"OpenSSL failed: {message}")
    return result.stdout


def _extract_sec1_public_key(spki_der: bytes) -> bytes:
    marker = b"\x03\x42\x00\x04"
    offset = spki_der.rfind(marker)
    if offset < 0 or offset + len(marker) + 64 != len(spki_der):
        raise ProvisioningError(
            "input is not an uncompressed P-256 SubjectPublicKeyInfo"
        )
    public_key = spki_der[offset + 3 :]
    validate_public_key(public_key)
    return public_key


def _public_key_spki_der(public_key: bytes) -> bytes:
    validate_public_key_shape(public_key)
    # id-ecPublicKey + prime256v1 followed by a 65-byte SEC1 BIT STRING.
    return (
        b"\x30\x59"
        b"\x30\x13"
        b"\x06\x07\x2A\x86\x48\xCE\x3D\x02\x01"
        b"\x06\x08\x2A\x86\x48\xCE\x3D\x03\x01\x07"
        b"\x03\x42\x00"
        + public_key
    )


def validate_public_key_shape(public_key: bytes) -> None:
    if (
        len(public_key) != P256_PUBLIC_KEY_SIZE
        or public_key[0] != 0x04
        or not any(public_key[1:])
    ):
        raise ProvisioningError(
            "public key must be a 65-byte uncompressed SEC1 P-256 point"
        )


def validate_public_key(public_key: bytes) -> None:
    validate_public_key_shape(public_key)
    with tempfile.TemporaryDirectory(prefix="hbox-point-verify-") as temp_dir:
        public_der = Path(temp_dir) / "public.der"
        public_der.write_bytes(_public_key_spki_der_unchecked(public_key))
        _run_openssl(
            [
                "pkey",
                "-pubin",
                "-inform",
                "DER",
                "-in",
                str(public_der),
                "-noout",
            ]
        )


def _public_key_spki_der_unchecked(public_key: bytes) -> bytes:
    return (
        b"\x30\x59"
        b"\x30\x13"
        b"\x06\x07\x2A\x86\x48\xCE\x3D\x02\x01"
        b"\x06\x08\x2A\x86\x48\xCE\x3D\x03\x01\x07"
        b"\x03\x42\x00"
        + public_key
    )


def load_public_key(path: Path) -> bytes:
    encoded = path.read_bytes()
    if len(encoded) == P256_PUBLIC_KEY_SIZE and encoded[0] == 0x04:
        validate_public_key(encoded)
        return encoded
    der = _run_openssl(
        ["pkey", "-pubin", "-in", str(path), "-pubout", "-outform", "DER"]
    )
    return _extract_sec1_public_key(der)


def load_verified_csr_public_key(path: Path) -> bytes:
    """Verify CSR proof-of-possession and return its P-256 public key."""

    _run_openssl(
        [
            "req",
            "-config",
            os.devnull,
            "-in",
            str(path),
            "-verify",
            "-noout",
        ]
    )
    public_pem = _run_openssl(
        [
            "req",
            "-config",
            os.devnull,
            "-in",
            str(path),
            "-pubkey",
            "-noout",
        ]
    )
    der = _run_openssl(
        ["pkey", "-pubin", "-pubout", "-outform", "DER"],
        input_data=public_pem,
    )
    return _extract_sec1_public_key(der)


def verify_raw_signature(
    public_key: bytes, message: bytes, raw_signature: bytes
) -> None:
    validate_public_key(public_key)
    if len(raw_signature) != DEVICE_CERTIFICATE_SIGNATURE_SIZE:
        raise ProvisioningError("raw P-256 signature must be exactly 64 bytes")
    with tempfile.TemporaryDirectory(prefix="hbox-pop-verify-") as temp_dir:
        directory = Path(temp_dir)
        public_der = directory / "public.der"
        message_file = directory / "message.bin"
        signature_file = directory / "signature.der"
        public_der.write_bytes(_public_key_spki_der(public_key))
        message_file.write_bytes(message)
        signature_file.write_bytes(raw_signature_to_der(raw_signature))
        _run_openssl(
            [
                "dgst",
                "-sha256",
                "-verify",
                str(public_der),
                "-keyform",
                "DER",
                "-signature",
                str(signature_file),
                str(message_file),
            ]
        )


def load_public_key_with_proof(
    *,
    csr: Path | None,
    device_public_key: Path | None,
    factory_challenge: Path | None,
    proof_signature: Path | None,
) -> bytes:
    if csr is not None:
        if any(
            value is not None
            for value in (device_public_key, factory_challenge, proof_signature)
        ):
            raise ProvisioningError(
                "--csr cannot be combined with raw public-key proof options"
            )
        return load_verified_csr_public_key(csr)
    if (
        device_public_key is None
        or factory_challenge is None
        or proof_signature is None
    ):
        raise ProvisioningError(
            "use --csr, or provide --device-public-key, "
            "--factory-challenge and --proof-signature together"
        )
    challenge = factory_challenge.read_bytes()
    if len(challenge) != 32:
        raise ProvisioningError("factory challenge must be exactly 32 bytes")
    public_key = load_public_key(device_public_key)
    verify_raw_signature(
        public_key,
        FACTORY_POP_DOMAIN + challenge,
        proof_signature.read_bytes(),
    )
    return public_key


def parse_hex_bytes(value: str, length: int, label: str) -> bytes:
    if len(value) != length * 2:
        raise ProvisioningError(
            f"{label} must contain exactly {length * 2} hexadecimal characters"
        )
    try:
        decoded = bytes.fromhex(value)
    except ValueError as exc:
        raise ProvisioningError(f"{label} must be hexadecimal") from exc
    if len(decoded) != length:
        raise ProvisioningError(f"{label} has the wrong length")
    return decoded


def encode_hardware_version(value: str) -> int:
    parts = value.split(".")
    if len(parts) != 3:
        raise ProvisioningError("hardware version must be MAJOR.MINOR.PATCH")
    try:
        numbers = [int(part, 10) for part in parts]
    except ValueError as exc:
        raise ProvisioningError(
            "hardware version components must be decimal integers"
        ) from exc
    if any(number < 0 or number > 255 for number in numbers):
        raise ProvisioningError(
            "hardware version components must be between 0 and 255"
        )
    return (numbers[0] << 16) | (numbers[1] << 8) | numbers[2]


def decode_hardware_version(value: int) -> str:
    return f"{(value >> 16) & 0xFF}.{(value >> 8) & 0xFF}.{value & 0xFF}"


def encode_product_id(value: str) -> int:
    """Encode the four-character manufacturer product family identifier."""

    if not isinstance(value, str) or len(value) != 4:
        raise ProvisioningError("product ID must contain exactly 4 ASCII characters")
    try:
        encoded = value.encode("ascii")
    except UnicodeEncodeError as exc:
        raise ProvisioningError("product ID must contain ASCII only") from exc
    if any(not (0x30 <= byte <= 0x39 or 0x41 <= byte <= 0x5A) for byte in encoded):
        raise ProvisioningError("product ID must use uppercase A-Z or digits")
    return int.from_bytes(encoded, "little")


def decode_product_id(value: int) -> str:
    if value < 0 or value > 0xFFFFFFFF:
        raise ProvisioningError("product ID must fit in uint32")
    encoded = value.to_bytes(4, "little")
    try:
        decoded = encoded.decode("ascii")
    except UnicodeDecodeError as exc:
        raise ProvisioningError("product ID is not ASCII") from exc
    if encode_product_id(decoded) != value:
        raise ProvisioningError("product ID is not canonical")
    return decoded


def encode_production_batch(value: str) -> bytes:
    try:
        encoded = value.encode("ascii")
    except UnicodeEncodeError as exc:
        raise ProvisioningError("production batch must contain ASCII only") from exc
    if len(encoded) == 0 or len(encoded) > 16:
        raise ProvisioningError(
            "production batch must contain between 1 and 16 ASCII bytes"
        )
    return encoded.ljust(16, b"\0")


def create_certificate_tbs(
    public_key: bytes,
    *,
    certificate_serial: bytes,
    product_id: int = DEFAULT_PRODUCT_ID_CODE,
    hardware_version: int,
    issued_at: int,
    production_batch: bytes,
    auth_level: int = 1,
) -> bytes:
    validate_public_key(public_key)
    if len(certificate_serial) != 16 or not any(certificate_serial):
        raise ProvisioningError(
            "certificate serial must be a nonzero 16-byte value"
        )
    decode_product_id(product_id)
    if hardware_version < 0 or hardware_version > 0xFFFFFFFF:
        raise ProvisioningError("hardware version must fit in uint32")
    if issued_at < 0 or issued_at > 0xFFFFFFFF:
        raise ProvisioningError("issued_at must fit in uint32")
    if len(production_batch) > 16 or not any(production_batch):
        raise ProvisioningError(
            "production batch must contain 1 to 16 nonzero bytes"
        )
    if auth_level not in (1, 2, 3):
        raise ProvisioningError("auth level must be MCU, retrofit or secure-element")

    tbs = bytearray(DEVICE_CERTIFICATE_SIGNED_SIZE)
    struct.pack_into(
        "<IBBH",
        tbs,
        0,
        DEVICE_CERTIFICATE_MAGIC,
        DEVICE_CERTIFICATE_VERSION,
        auth_level,
        DEVICE_CERTIFICATE_SIGNED_SIZE,
    )
    tbs[8:24] = certificate_serial
    tbs[24:40] = hashlib.sha256(public_key).digest()[:16]
    struct.pack_into("<II", tbs, 40, hardware_version, issued_at)
    tbs[48:113] = public_key
    tbs[113:129] = production_batch.ljust(16, b"\0")
    struct.pack_into("<I", tbs, 129, product_id)
    # bytes 133..143 are reserved and remain zero.
    return bytes(tbs)


def validate_certificate_tbs(tbs: bytes) -> dict[str, object]:
    if len(tbs) != DEVICE_CERTIFICATE_SIGNED_SIZE:
        raise ProvisioningError("certificate TBS must be exactly 144 bytes")
    magic, version, auth_level, signed_size = struct.unpack_from("<IBBH", tbs, 0)
    if (
        magic != DEVICE_CERTIFICATE_MAGIC
        or version != DEVICE_CERTIFICATE_VERSION
        or signed_size != DEVICE_CERTIFICATE_SIGNED_SIZE
        or auth_level not in (1, 2, 3)
    ):
        raise ProvisioningError("certificate TBS header is invalid")
    if not any(tbs[8:24]) or any(tbs[133:144]):
        raise ProvisioningError(
            "certificate serial must be nonzero and reserved bytes must be zero"
        )
    public_key = tbs[48:113]
    validate_public_key(public_key)
    expected_device_id = hashlib.sha256(public_key).digest()[:16]
    if tbs[24:40] != expected_device_id:
        raise ProvisioningError(
            "certificate deviceId does not match SHA-256(public key)[:16]"
        )
    hardware_version, issued_at = struct.unpack_from("<II", tbs, 40)
    product_id = struct.unpack_from("<I", tbs, 129)[0]
    if (hardware_version & 0xFF000000) != 0 or issued_at == 0:
        raise ProvisioningError(
            "hardware version high byte must be zero and issued_at nonzero"
        )
    encoded_batch = tbs[113:129]
    production_batch = encoded_batch.rstrip(b"\0")
    if (
        not production_batch
        or encoded_batch != production_batch.ljust(16, b"\0")
        or any(value < 0x20 or value > 0x7E for value in production_batch)
    ):
        raise ProvisioningError(
            "production batch must be printable ASCII with zero padding"
        )
    return {
        "certificateSerial": tbs[8:24].hex(),
        "deviceId": tbs[24:40].hex(),
        "hardwareVersion": decode_hardware_version(
            hardware_version
        ),
        "pcbRevision": decode_hardware_version(hardware_version),
        "productId": decode_product_id(product_id),
        "productIdCode": product_id,
        "issuedAt": issued_at,
        "authLevel": auth_level,
        "productionBatch": production_batch.decode("ascii"),
        "publicKeyFingerprint": hashlib.sha256(public_key).hexdigest(),
    }


def sign_certificate_tbs_for_lab(tbs: bytes, private_key: Path) -> bytes:
    """Sign a TBS using a local key. Tests/labs only, never production CA."""

    validate_certificate_tbs(tbs)
    with tempfile.TemporaryDirectory(prefix="hbox-lab-ca-") as temp_dir:
        tbs_file = Path(temp_dir) / "certificate.tbs"
        tbs_file.write_bytes(tbs)
        der = _run_openssl(
            ["dgst", "-sha256", "-sign", str(private_key), str(tbs_file)]
        )
    return der_signature_to_raw(der)


def _verify_ca_signature(
    tbs: bytes, signature: bytes, manufacturer_ca_public_key: Path
) -> None:
    if len(signature) != DEVICE_CERTIFICATE_SIGNATURE_SIZE:
        raise ProvisioningError(
            "manufacturer signature must be a raw 64-byte P-256 signature"
        )
    with tempfile.TemporaryDirectory(prefix="hbox-ca-verify-") as temp_dir:
        directory = Path(temp_dir)
        tbs_file = directory / "certificate.tbs"
        signature_file = directory / "signature.der"
        tbs_file.write_bytes(tbs)
        signature_file.write_bytes(raw_signature_to_der(signature))
        _run_openssl(
            [
                "dgst",
                "-sha256",
                "-verify",
                str(manufacturer_ca_public_key),
                "-signature",
                str(signature_file),
                str(tbs_file),
            ]
        )


def assemble_certificate(
    tbs: bytes,
    signature: bytes,
    manufacturer_ca_public_key: Path,
) -> bytes:
    validate_certificate_tbs(tbs)
    _verify_ca_signature(tbs, signature, manufacturer_ca_public_key)
    certificate = tbs + signature
    if len(certificate) != DEVICE_CERTIFICATE_SIZE:
        raise ProvisioningError("assembled certificate has an invalid size")
    return certificate


def verify_certificate(
    certificate: bytes, manufacturer_ca_public_key: Path
) -> dict[str, object]:
    if len(certificate) != DEVICE_CERTIFICATE_SIZE:
        raise ProvisioningError("device certificate must be exactly 208 bytes")
    summary = validate_certificate_tbs(
        certificate[:DEVICE_CERTIFICATE_SIGNED_SIZE]
    )
    _verify_ca_signature(
        certificate[:DEVICE_CERTIFICATE_SIGNED_SIZE],
        certificate[DEVICE_CERTIFICATE_SIGNED_SIZE:],
        manufacturer_ca_public_key,
    )
    summary["certificateFingerprint"] = hashlib.sha256(certificate).hexdigest()
    return summary


def crc32_skipping(
    data: bytes, skip_offset: int, skip_size: int
) -> int:
    if skip_offset < 0 or skip_size < 0 or skip_offset + skip_size > len(data):
        raise ProvisioningError("invalid CRC skip range")
    crc = 0xFFFFFFFF
    for index, value in enumerate(data):
        if skip_offset <= index < skip_offset + skip_size:
            continue
        crc ^= value
        for _ in range(8):
            mask = -(crc & 1) & 0xFFFFFFFF
            crc = ((crc >> 1) ^ (0xEDB88320 & mask)) & 0xFFFFFFFF
    return crc ^ 0xFFFFFFFF


def _public_key_from_private_scalar(private_scalar: bytes) -> bytes:
    if len(private_scalar) != P256_PRIVATE_KEY_SIZE:
        raise ProvisioningError("device private scalar must be exactly 32 bytes")
    scalar = int.from_bytes(private_scalar, "big")
    if scalar <= 0 or scalar >= P256_ORDER:
        raise ProvisioningError("device private scalar is outside P-256 range")
    point: tuple[int, int] | None = None
    addend: tuple[int, int] | None = (
        P256_GENERATOR_X,
        P256_GENERATOR_Y,
    )

    def add(
        left: tuple[int, int] | None,
        right: tuple[int, int] | None,
    ) -> tuple[int, int] | None:
        if left is None:
            return right
        if right is None:
            return left
        x1, y1 = left
        x2, y2 = right
        if x1 == x2 and (y1 + y2) % P256_FIELD == 0:
            return None
        if left == right:
            slope = (
                (3 * x1 * x1 - 3)
                * pow(2 * y1, P256_FIELD - 2, P256_FIELD)
            ) % P256_FIELD
        else:
            slope = (
                (y2 - y1)
                * pow((x2 - x1) % P256_FIELD, P256_FIELD - 2, P256_FIELD)
            ) % P256_FIELD
        x3 = (slope * slope - x1 - x2) % P256_FIELD
        y3 = (slope * (x1 - x3) - y1) % P256_FIELD
        return x3, y3

    remaining = scalar
    while remaining:
        if remaining & 1:
            point = add(point, addend)
        addend = add(addend, addend)
        remaining >>= 1
    if point is None:
        raise ProvisioningError("device private scalar produced infinity")
    public_key = (
        b"\x04"
        + point[0].to_bytes(32, "big")
        + point[1].to_bytes(32, "big")
    )
    validate_public_key(public_key)
    return public_key


def build_development_identity_record(
    private_scalar: bytes,
    certificate: bytes,
    manufacturer_ca_public_key: Path,
) -> bytes:
    """Build the fixed record for tests only.

    Production factory firmware must perform this operation inside the MCU so
    Kdev never crosses the USB/SWD/host boundary.
    """

    verify_certificate(certificate, manufacturer_ca_public_key)
    public_key = _public_key_from_private_scalar(private_scalar)
    if public_key != certificate[48:113]:
        raise ProvisioningError(
            "device private scalar does not match certificate public key"
        )
    record = bytearray(DEVICE_IDENTITY_RECORD_SIZE)
    struct.pack_into(
        "<IBBH",
        record,
        0,
        DEVICE_IDENTITY_MAGIC,
        DEVICE_IDENTITY_VERSION,
        1,
        DEVICE_IDENTITY_RECORD_SIZE,
    )
    record[
        DEVICE_IDENTITY_PRIVATE_KEY_OFFSET:
        DEVICE_IDENTITY_PRIVATE_KEY_OFFSET + P256_PRIVATE_KEY_SIZE
    ] = private_scalar
    record[
        DEVICE_IDENTITY_CERTIFICATE_OFFSET:
        DEVICE_IDENTITY_CERTIFICATE_OFFSET + DEVICE_CERTIFICATE_SIZE
    ] = certificate
    crc = crc32_skipping(record, DEVICE_IDENTITY_CRC_OFFSET, 4)
    if crc == 0:
        raise ProvisioningError("identity record CRC unexpectedly equals zero")
    struct.pack_into("<I", record, DEVICE_IDENTITY_CRC_OFFSET, crc)
    return bytes(record)


def verify_development_identity_record(
    record: bytes, manufacturer_ca_public_key: Path
) -> dict[str, object]:
    if len(record) != DEVICE_IDENTITY_RECORD_SIZE:
        raise ProvisioningError(
            "internal-Flash identity record must be exactly 256 bytes"
        )
    magic, version, locked, total_size = struct.unpack_from("<IBBH", record, 0)
    if (
        magic != DEVICE_IDENTITY_MAGIC
        or version != DEVICE_IDENTITY_VERSION
        or locked != 1
        or total_size != DEVICE_IDENTITY_RECORD_SIZE
        or any(record[252:256])
    ):
        raise ProvisioningError(
            "internal-Flash identity record header/reserved bytes invalid"
        )
    actual_crc = struct.unpack_from("<I", record, DEVICE_IDENTITY_CRC_OFFSET)[0]
    expected_crc = crc32_skipping(record, DEVICE_IDENTITY_CRC_OFFSET, 4)
    if actual_crc == 0 or actual_crc != expected_crc:
        raise ProvisioningError("internal-Flash identity record CRC mismatch")
    private_scalar = record[
        DEVICE_IDENTITY_PRIVATE_KEY_OFFSET:
        DEVICE_IDENTITY_PRIVATE_KEY_OFFSET + P256_PRIVATE_KEY_SIZE
    ]
    certificate = record[
        DEVICE_IDENTITY_CERTIFICATE_OFFSET:
        DEVICE_IDENTITY_CERTIFICATE_OFFSET + DEVICE_CERTIFICATE_SIZE
    ]
    summary = verify_certificate(certificate, manufacturer_ca_public_key)
    if _public_key_from_private_scalar(private_scalar) != certificate[48:113]:
        raise ProvisioningError(
            "identity private scalar does not match certificate public key"
        )
    summary["recordCrc32"] = f"{actual_crc:08x}"
    summary["recordSize"] = len(record)
    return summary


def build_development_identity_slot(
    private_scalar: bytes,
    certificate: bytes,
    manufacturer_ca_public_key: Path,
    *,
    slot_ordinal: int = 1,
) -> bytes:
    """Build a complete 288-byte slot image for format tests only.

    The first 256 bytes are programmed and verified before the final 32-byte
    commit flashword.  Production firmware must assemble this image on-device;
    it must never receive an exported Kdev from this helper.
    """

    if slot_ordinal < 1 or slot_ordinal > DEVICE_IDENTITY_SLOT_COUNT:
        raise ProvisioningError("identity slot ordinal is out of range")
    record = build_development_identity_record(
        private_scalar,
        certificate,
        manufacturer_ca_public_key,
    )
    record_crc = struct.unpack_from(
        "<I", record, DEVICE_IDENTITY_CRC_OFFSET
    )[0]
    commit = bytearray(DEVICE_IDENTITY_COMMIT_SIZE)
    struct.pack_into(
        "<IBBHIII",
        commit,
        0,
        DEVICE_IDENTITY_COMMIT_MAGIC,
        DEVICE_IDENTITY_COMMIT_VERSION,
        0,
        DEVICE_IDENTITY_COMMIT_SIZE,
        slot_ordinal,
        record_crc,
        (~record_crc) & 0xFFFFFFFF,
    )
    struct.pack_into(
        "<I", commit, 24, DEVICE_IDENTITY_COMMITTED
    )
    commit_crc = crc32_skipping(
        commit, DEVICE_IDENTITY_COMMIT_CRC_OFFSET, 4
    )
    if commit_crc == 0:
        raise ProvisioningError("identity commit CRC unexpectedly equals zero")
    struct.pack_into(
        "<I", commit, DEVICE_IDENTITY_COMMIT_CRC_OFFSET, commit_crc
    )
    return record + bytes(commit)


def verify_development_identity_slot(
    slot: bytes,
    manufacturer_ca_public_key: Path,
    *,
    expected_slot_ordinal: int = 1,
) -> dict[str, object]:
    if len(slot) != DEVICE_IDENTITY_SLOT_SIZE:
        raise ProvisioningError(
            "internal-Flash identity slot must be exactly 288 bytes"
        )
    if (
        expected_slot_ordinal < 1
        or expected_slot_ordinal > DEVICE_IDENTITY_SLOT_COUNT
    ):
        raise ProvisioningError("identity slot ordinal is out of range")

    record = slot[:DEVICE_IDENTITY_RECORD_SIZE]
    commit = slot[DEVICE_IDENTITY_RECORD_SIZE:]
    summary = verify_development_identity_record(
        record, manufacturer_ca_public_key
    )
    (
        magic,
        version,
        reserved0,
        total_size,
        slot_ordinal,
        record_crc,
        record_crc_inverse,
    ) = struct.unpack_from("<IBBHIII", commit, 0)
    commit_crc = struct.unpack_from(
        "<I", commit, DEVICE_IDENTITY_COMMIT_CRC_OFFSET
    )[0]
    committed = struct.unpack_from("<I", commit, 24)[0]
    expected_record_crc = struct.unpack_from(
        "<I", record, DEVICE_IDENTITY_CRC_OFFSET
    )[0]
    expected_commit_crc = crc32_skipping(
        commit, DEVICE_IDENTITY_COMMIT_CRC_OFFSET, 4
    )
    if (
        magic != DEVICE_IDENTITY_COMMIT_MAGIC
        or version != DEVICE_IDENTITY_COMMIT_VERSION
        or reserved0 != 0
        or total_size != DEVICE_IDENTITY_COMMIT_SIZE
        or slot_ordinal != expected_slot_ordinal
        or record_crc != expected_record_crc
        or record_crc_inverse != ((~record_crc) & 0xFFFFFFFF)
        or commit_crc == 0
        or commit_crc != expected_commit_crc
        or committed != DEVICE_IDENTITY_COMMITTED
        or any(commit[28:32])
    ):
        raise ProvisioningError(
            "internal-Flash identity commit flashword is invalid"
        )
    summary["slotOrdinal"] = slot_ordinal
    summary["slotSize"] = len(slot)
    summary["commitCrc32"] = f"{commit_crc:08x}"
    return summary


def _render_array(name: str, value: bytes, indent: str = "    ") -> str:
    rows = []
    for offset in range(0, len(value), 8):
        rows.append(
            indent
            + ", ".join(
                f"0x{byte:02X}u" for byte in value[offset : offset + 8]
            )
            + ","
        )
    return f"static const uint8_t {name}[65] = {{\n" + "\n".join(rows) + "\n};"


def render_trust_bundle_header(
    *,
    manufacturer_ca_public_key: bytes,
    firmware_release_public_key: bytes,
    authorization_current_public_key: bytes,
    authorization_next_public_key: bytes | None = None,
) -> str:
    for public_key in (
        manufacturer_ca_public_key,
        firmware_release_public_key,
        authorization_current_public_key,
    ):
        validate_public_key(public_key)
    if authorization_next_public_key is not None:
        validate_public_key(authorization_next_public_key)
    next_key = authorization_next_public_key or bytes(P256_PUBLIC_KEY_SIZE)
    provisioned_mask = 0x03 if authorization_next_public_key is not None else 0x01

    current_rows = _render_array(
        "HBOX_WEBCONFIG_AUTHORIZATION_PUBLIC_KEY_CURRENT",
        authorization_current_public_key,
    ).splitlines()[1:-1]
    next_rows = _render_array(
        "HBOX_WEBCONFIG_AUTHORIZATION_PUBLIC_KEY_NEXT", next_key
    ).splitlines()[1:-1]
    return (
        "#ifndef HBOX_PRODUCTION_TRUST_BUNDLE_GENERATED_H\n"
        "#define HBOX_PRODUCTION_TRUST_BUNDLE_GENERATED_H\n\n"
        "#include <stdint.h>\n\n"
        "/* Public keys only. Generated by device_identity_provisioning.py. */\n"
        "#define HBOX_MANUFACTURER_CA_KEY_PROVISIONED 1u\n"
        + _render_array(
            "HBOX_MANUFACTURER_CA_PUBLIC_KEY",
            manufacturer_ca_public_key,
        )
        + "\n\n"
        "#define HBOX_FIRMWARE_RELEASE_PUBLIC_KEY_PROVISIONED 1u\n"
        + _render_array(
            "hbox_firmware_release_public_key",
            firmware_release_public_key,
        )
        + "\n\n"
        "#define HBOX_WEBCONFIG_AUTH_KEY_SLOT_COUNT 2u\n"
        f"#define HBOX_WEBCONFIG_AUTH_KEY_PROVISIONED_MASK 0x{provisioned_mask:02X}u\n"
        "static const uint8_t HBOX_WEBCONFIG_AUTHORIZATION_PUBLIC_KEYS[2][65] = {\n"
        "  {\n"
        + "\n".join("  " + row for row in current_rows)
        + "\n  },\n  {\n"
        + "\n".join("  " + row for row in next_rows)
        + "\n  }\n};\n\n"
        "#endif /* HBOX_PRODUCTION_TRUST_BUNDLE_GENERATED_H */\n"
    )


def _atomic_write(
    output: Path,
    data: bytes,
    *,
    force: bool,
    private: bool = False,
) -> None:
    output = output.resolve()
    if output.exists() and not force:
        raise ProvisioningError(
            f"refusing to overwrite existing output without --force: {output}"
        )
    output.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{output.name}.", dir=output.parent
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        if private:
            os.chmod(temporary, 0o600)
        os.replace(temporary, output)
    finally:
        if temporary.exists():
            temporary.unlink()


def _load_signature(path: Path, signature_format: str) -> bytes:
    signature = path.read_bytes()
    if signature_format == "der":
        signature = der_signature_to_raw(signature)
    if len(signature) != DEVICE_CERTIFICATE_SIGNATURE_SIZE:
        raise ProvisioningError("P-256 signature must decode to exactly 64 bytes")
    return signature


def _print_summary(summary: dict[str, object]) -> None:
    print(json.dumps(summary, indent=2, sort_keys=True))


def _require_development_acknowledgement(args: argparse.Namespace) -> None:
    if not args.acknowledge_private_key_export_risk:
        raise ProvisioningError(
            "this DEVELOPMENT-ONLY command handles Kdev; pass "
            "--acknowledge-private-key-export-risk after confirming it will "
            "never be used for production devices"
        )


def _add_output_options(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--force", action="store_true", help="replace an existing output"
    )


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="HBox V2 fail-closed identity provisioning utilities"
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    tbs_parser = subparsers.add_parser(
        "make-certificate-tbs",
        help="verify device proof-of-possession and create a 144-byte CA TBS",
    )
    proof_group = tbs_parser.add_mutually_exclusive_group(required=True)
    proof_group.add_argument("--csr", type=Path)
    proof_group.add_argument("--device-public-key", type=Path)
    tbs_parser.add_argument("--factory-challenge", type=Path)
    tbs_parser.add_argument("--proof-signature", type=Path)
    tbs_parser.add_argument("--certificate-serial", required=True)
    tbs_parser.add_argument(
        "--product-id",
        default=DEFAULT_PRODUCT_ID,
        help="four-character product family identifier (default: HBOX)",
    )
    tbs_parser.add_argument("--hardware-version", required=True)
    tbs_parser.add_argument("--issued-at", type=int, default=None)
    tbs_parser.add_argument("--production-batch", required=True)
    tbs_parser.add_argument("--auth-level", type=int, choices=(1, 2, 3), default=1)
    _add_output_options(tbs_parser)

    assemble_parser = subparsers.add_parser(
        "assemble-certificate",
        help="verify an offline/HSM signature and assemble the 208-byte cert",
    )
    assemble_parser.add_argument("--tbs", type=Path, required=True)
    assemble_parser.add_argument(
        "--manufacturer-signature", type=Path, required=True
    )
    assemble_parser.add_argument(
        "--signature-format", choices=("raw", "der"), default="raw"
    )
    assemble_parser.add_argument(
        "--manufacturer-ca-public", type=Path, required=True
    )
    _add_output_options(assemble_parser)

    verify_parser = subparsers.add_parser(
        "verify-certificate", help="verify fixed fields and manufacturer signature"
    )
    verify_parser.add_argument("--certificate", type=Path, required=True)
    verify_parser.add_argument(
        "--manufacturer-ca-public", type=Path, required=True
    )

    development_parser = subparsers.add_parser(
        "development-build-flash-slot",
        help=(
            "DEVELOPMENT ONLY: build a 288-byte internal-Flash slot from "
            "an exported Kdev"
        ),
    )
    development_parser.add_argument(
        "--device-private-scalar", type=Path, required=True
    )
    development_parser.add_argument("--certificate", type=Path, required=True)
    development_parser.add_argument(
        "--manufacturer-ca-public", type=Path, required=True
    )
    development_parser.add_argument(
        "--acknowledge-private-key-export-risk", action="store_true"
    )
    development_parser.add_argument(
        "--slot-ordinal",
        type=int,
        choices=range(1, DEVICE_IDENTITY_SLOT_COUNT + 1),
        default=1,
    )
    _add_output_options(development_parser)

    development_verify_parser = subparsers.add_parser(
        "development-verify-flash-slot",
        help=(
            "DEVELOPMENT ONLY: inspect an internal-Flash slot containing "
            "an exported Kdev"
        ),
    )
    development_verify_parser.add_argument("--slot", type=Path, required=True)
    development_verify_parser.add_argument(
        "--manufacturer-ca-public", type=Path, required=True
    )
    development_verify_parser.add_argument(
        "--acknowledge-private-key-export-risk", action="store_true"
    )
    development_verify_parser.add_argument(
        "--slot-ordinal",
        type=int,
        choices=range(1, DEVICE_IDENTITY_SLOT_COUNT + 1),
        default=1,
    )

    header_parser = subparsers.add_parser(
        "render-trust-header",
        help="render a public-only force-include header for a provisioned build",
    )
    header_parser.add_argument(
        "--manufacturer-ca-public", type=Path, required=True
    )
    header_parser.add_argument(
        "--firmware-release-public", type=Path, required=True
    )
    header_parser.add_argument(
        "--authorization-current-public", type=Path, required=True
    )
    header_parser.add_argument("--authorization-next-public", type=Path)
    _add_output_options(header_parser)
    return parser


def main() -> int:
    parser = build_argument_parser()
    args = parser.parse_args()
    try:
        if args.command == "make-certificate-tbs":
            public_key = load_public_key_with_proof(
                csr=args.csr,
                device_public_key=args.device_public_key,
                factory_challenge=args.factory_challenge,
                proof_signature=args.proof_signature,
            )
            issued_at = int(time.time()) if args.issued_at is None else args.issued_at
            tbs = create_certificate_tbs(
                public_key,
                certificate_serial=parse_hex_bytes(
                    args.certificate_serial, 16, "certificate serial"
                ),
                product_id=encode_product_id(args.product_id),
                hardware_version=encode_hardware_version(args.hardware_version),
                issued_at=issued_at,
                production_batch=encode_production_batch(args.production_batch),
                auth_level=args.auth_level,
            )
            _atomic_write(args.output, tbs, force=args.force)
            _print_summary(validate_certificate_tbs(tbs))
        elif args.command == "assemble-certificate":
            certificate = assemble_certificate(
                args.tbs.read_bytes(),
                _load_signature(
                    args.manufacturer_signature, args.signature_format
                ),
                args.manufacturer_ca_public,
            )
            _atomic_write(args.output, certificate, force=args.force)
            _print_summary(
                verify_certificate(certificate, args.manufacturer_ca_public)
            )
        elif args.command == "verify-certificate":
            _print_summary(
                verify_certificate(
                    args.certificate.read_bytes(),
                    args.manufacturer_ca_public,
                )
            )
        elif args.command == "development-build-flash-slot":
            _require_development_acknowledgement(args)
            slot = build_development_identity_slot(
                args.device_private_scalar.read_bytes(),
                args.certificate.read_bytes(),
                args.manufacturer_ca_public,
                slot_ordinal=args.slot_ordinal,
            )
            _atomic_write(args.output, slot, force=args.force, private=True)
            _print_summary(
                verify_development_identity_slot(
                    slot,
                    args.manufacturer_ca_public,
                    expected_slot_ordinal=args.slot_ordinal,
                )
            )
        elif args.command == "development-verify-flash-slot":
            _require_development_acknowledgement(args)
            _print_summary(
                verify_development_identity_slot(
                    args.slot.read_bytes(),
                    args.manufacturer_ca_public,
                    expected_slot_ordinal=args.slot_ordinal,
                )
            )
        elif args.command == "render-trust-header":
            rendered = render_trust_bundle_header(
                manufacturer_ca_public_key=load_public_key(
                    args.manufacturer_ca_public
                ),
                firmware_release_public_key=load_public_key(
                    args.firmware_release_public
                ),
                authorization_current_public_key=load_public_key(
                    args.authorization_current_public
                ),
                authorization_next_public_key=(
                    load_public_key(args.authorization_next_public)
                    if args.authorization_next_public
                    else None
                ),
            )
            _atomic_write(
                args.output,
                rendered.encode("utf-8"),
                force=args.force,
            )
            print(f"Wrote public-only trust header: {args.output.resolve()}")
        else:
            raise ProvisioningError(f"unsupported command: {args.command}")
    except (OSError, ProvisioningError) as exc:
        parser.error(str(exc))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
