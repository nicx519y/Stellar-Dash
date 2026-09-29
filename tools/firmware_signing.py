#!/usr/bin/env python3
"""Fail-closed P-256 signing helpers for HBox firmware metadata."""

from __future__ import annotations

import argparse
import hashlib
import os
import subprocess
import tempfile
from pathlib import Path
from typing import Tuple

PROJECT_ROOT = Path(__file__).resolve().parents[1]

import sys

sys.path.insert(0, str(PROJECT_ROOT / "common"))
from firmware_metadata import (  # noqa: E402
    FIRMWARE_HASH_OFFSET,
    FIRMWARE_SIGNATURE_OFFSET,
    METADATA_CRC32_OFFSET,
    METADATA_STRUCT_SIZE,
)


class FirmwareSigningError(RuntimeError):
    pass


def _read_der_length(data: bytes, offset: int) -> Tuple[int, int]:
    if offset >= len(data):
        raise FirmwareSigningError("truncated DER length")
    first = data[offset]
    offset += 1
    if first < 0x80:
        return first, offset
    width = first & 0x7F
    if width == 0 or width > 2 or offset + width > len(data):
        raise FirmwareSigningError("invalid DER length")
    return int.from_bytes(data[offset : offset + width], "big"), offset + width


def _read_der_integer(data: bytes, offset: int) -> Tuple[int, int]:
    if offset >= len(data) or data[offset] != 0x02:
        raise FirmwareSigningError("ECDSA signature is not a DER INTEGER")
    length, offset = _read_der_length(data, offset + 1)
    end = offset + length
    if length == 0 or end > len(data):
        raise FirmwareSigningError("truncated DER INTEGER")
    encoded = data[offset:end]
    if encoded[0] & 0x80:
        raise FirmwareSigningError("negative DER INTEGER")
    if len(encoded) > 1 and encoded[0] == 0:
        encoded = encoded[1:]
    if len(encoded) > 32:
        raise FirmwareSigningError("P-256 scalar exceeds 32 bytes")
    return int.from_bytes(encoded, "big"), end


def der_signature_to_raw(signature: bytes) -> bytes:
    if not signature or signature[0] != 0x30:
        raise FirmwareSigningError("ECDSA signature is not a DER SEQUENCE")
    length, offset = _read_der_length(signature, 1)
    if offset + length != len(signature):
        raise FirmwareSigningError("invalid DER signature length")
    r, offset = _read_der_integer(signature, offset)
    s, offset = _read_der_integer(signature, offset)
    if offset != len(signature) or r == 0 or s == 0:
        raise FirmwareSigningError("invalid ECDSA signature scalar")
    return r.to_bytes(32, "big") + s.to_bytes(32, "big")


def raw_signature_to_der(signature: bytes) -> bytes:
    if len(signature) != 64:
        raise FirmwareSigningError("raw P-256 signature must be 64 bytes")

    def encode_integer(value: bytes) -> bytes:
        value = value.lstrip(b"\0") or b"\0"
        if value[0] & 0x80:
            value = b"\0" + value
        return b"\x02" + bytes((len(value),)) + value

    body = encode_integer(signature[:32]) + encode_integer(signature[32:])
    return b"\x30" + bytes((len(body),)) + body


def canonical_metadata(metadata: bytes) -> bytes:
    if len(metadata) != METADATA_STRUCT_SIZE:
        raise FirmwareSigningError(
            f"metadata must be exactly {METADATA_STRUCT_SIZE} bytes"
        )
    canonical = bytearray(metadata)
    canonical[METADATA_CRC32_OFFSET : METADATA_CRC32_OFFSET + 4] = b"\0" * 4
    canonical[FIRMWARE_HASH_OFFSET : FIRMWARE_HASH_OFFSET + 32] = b"\0" * 32
    canonical[
        FIRMWARE_SIGNATURE_OFFSET : FIRMWARE_SIGNATURE_OFFSET + 64
    ] = b"\0" * 64
    return bytes(canonical)


def _run_openssl(arguments: list[str], *, input_data: bytes | None = None) -> bytes:
    try:
        result = subprocess.run(
            ["openssl", *arguments],
            input=input_data,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
    except FileNotFoundError as exc:
        raise FirmwareSigningError("OpenSSL was not found") from exc
    if result.returncode != 0:
        message = result.stderr.decode("utf-8", errors="replace").strip()
        raise FirmwareSigningError(f"OpenSSL failed: {message}")
    return result.stdout


def resolve_signing_key(signing_key: str | os.PathLike[str] | None) -> Path:
    candidate = signing_key or os.environ.get("HBOX_FIRMWARE_SIGNING_KEY")
    if not candidate:
        raise FirmwareSigningError(
            "firmware signing key is required; set HBOX_FIRMWARE_SIGNING_KEY"
        )
    path = Path(candidate).expanduser().resolve()
    if not path.is_file():
        raise FirmwareSigningError(f"firmware signing key does not exist: {path}")
    return path


def sign_metadata(metadata: bytes, signing_key: str | os.PathLike[str] | None) -> tuple[bytes, bytes]:
    key_path = resolve_signing_key(signing_key)
    canonical = canonical_metadata(metadata)
    digest = hashlib.sha256(canonical).digest()
    with tempfile.TemporaryDirectory(prefix="hbox-sign-") as temporary_directory:
        canonical_file = Path(temporary_directory) / "metadata.canonical.bin"
        canonical_file.write_bytes(canonical)
        der = _run_openssl(
            ["dgst", "-sha256", "-sign", str(key_path), str(canonical_file)]
        )
    raw = der_signature_to_raw(der)
    return digest, raw


def export_uncompressed_public_key(
    signing_key: str | os.PathLike[str] | None,
) -> bytes:
    key_path = resolve_signing_key(signing_key)
    der = _run_openssl(
        ["pkey", "-in", str(key_path), "-pubout", "-outform", "DER"]
    )
    marker = b"\x03\x42\x00\x04"
    marker_offset = der.rfind(marker)
    if marker_offset < 0 or marker_offset + len(marker) + 64 != len(der):
        raise FirmwareSigningError("signing key is not an uncompressed P-256 key")
    public_key = der[marker_offset + 3 :]
    if len(public_key) != 65 or public_key[0] != 0x04:
        raise FirmwareSigningError("invalid uncompressed P-256 public key")
    return public_key


def render_public_key_header(public_key: bytes) -> str:
    if len(public_key) != 65 or public_key[0] != 0x04:
        raise FirmwareSigningError("invalid uncompressed P-256 public key")
    rows = []
    for offset in range(0, len(public_key), 8):
        rows.append(
            "    "
            + ", ".join(f"0x{value:02X}u" for value in public_key[offset : offset + 8])
            + ","
        )
    return (
        "#ifndef HBOX_FIRMWARE_RELEASE_PUBLIC_KEY_PROVISIONED_HEADER_H\n"
        "#define HBOX_FIRMWARE_RELEASE_PUBLIC_KEY_PROVISIONED_HEADER_H\n\n"
        "#include <stdint.h>\n\n"
        "#define HBOX_FIRMWARE_RELEASE_PUBLIC_KEY_PROVISIONED 1u\n"
        "static const uint8_t hbox_firmware_release_public_key[65] = {\n"
        + "\n".join(rows)
        + "\n};\n\n"
        "#endif\n"
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Export the P-256 release public key for a provisioned build"
    )
    parser.add_argument("--signing-key", help="PEM/PKCS#8 P-256 private key")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    public_key = export_uncompressed_public_key(args.signing_key)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(render_public_key_header(public_key), encoding="utf-8")
    print(f"Wrote release public key header: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
