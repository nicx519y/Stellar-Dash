"""Host-side model tests for the application Config A/B journal.

These tests deliberately model NOR flash programming (bits may only transition
from 1 to 0 between erases) and the on-device fixed header layout. They cover
the persistence invariants that do not require the STM32/QSPI HAL.
"""

from __future__ import annotations

import hashlib
import struct
import unittest


MAGIC = 0x47464348
FORMAT_VERSION = 1
COMMIT = 0x54494D43
ERASED_WORD = 0xFFFFFFFF
HEADER = struct.Struct("<IHHQI32s8sI")
COMMIT_OFFSET = 60
SUPPORTED_LEGACY_VERSIONS = {0x00001B, 0x00001C, 0x00001D, 0x00001E}


def make_header(payload: bytes, generation: int, committed: bool) -> bytes:
    return HEADER.pack(
        MAGIC,
        FORMAT_VERSION,
        HEADER.size,
        generation,
        len(payload),
        hashlib.sha256(payload).digest(),
        b"\xff" * 8,
        COMMIT if committed else ERASED_WORD,
    )


def is_valid(bank: bytes, expected_payload_length: int) -> bool:
    if len(bank) < HEADER.size + expected_payload_length:
        return False
    (
        magic,
        version,
        header_size,
        generation,
        payload_length,
        expected_sha,
        _reserved,
        commit,
    ) = HEADER.unpack_from(bank)
    if (
        magic != MAGIC
        or version != FORMAT_VERSION
        or header_size != HEADER.size
        or generation == 0
        or payload_length != expected_payload_length
        or commit != COMMIT
    ):
        return False
    payload = bank[HEADER.size : HEADER.size + payload_length]
    return hashlib.sha256(payload).digest() == expected_sha


def generation(bank: bytes) -> int:
    return HEADER.unpack_from(bank)[3]


def select_newest(bank_a: bytes, bank_b: bytes, payload_length: int) -> bytes | None:
    valid_a = is_valid(bank_a, payload_length)
    valid_b = is_valid(bank_b, payload_length)
    if valid_a and valid_b:
        return bank_b if generation(bank_b) > generation(bank_a) else bank_a
    if valid_a:
        return bank_a
    if valid_b:
        return bank_b
    return None


def can_load_legacy_app_config(first_word: int) -> bool:
    return first_word in SUPPORTED_LEGACY_VERSIONS


class ConfigJournalAtomicityTests(unittest.TestCase):
    def setUp(self) -> None:
        self.old_payload = bytes(range(64))
        self.new_payload = bytes(reversed(range(64)))
        self.old_bank = make_header(self.old_payload, 7, True) + self.old_payload

    def test_header_and_commit_are_fixed_size(self) -> None:
        self.assertEqual(HEADER.size, 64)
        self.assertEqual(COMMIT_OFFSET, HEADER.size - 4)

    def test_uncommitted_new_bank_never_replaces_old_bank(self) -> None:
        staged = make_header(self.new_payload, 8, False) + self.new_payload
        self.assertIs(
            select_newest(self.old_bank, staged, len(self.old_payload)),
            self.old_bank,
        )

    def test_partial_commit_word_never_becomes_visible(self) -> None:
        staged = bytearray(make_header(self.new_payload, 8, False) + self.new_payload)
        encoded_commit = struct.pack("<I", COMMIT)
        for written in range(4):
            interrupted = bytearray(staged)
            interrupted[COMMIT_OFFSET : COMMIT_OFFSET + written] = encoded_commit[:written]
            self.assertFalse(is_valid(interrupted, len(self.new_payload)))

        staged[COMMIT_OFFSET : COMMIT_OFFSET + 4] = encoded_commit
        self.assertTrue(is_valid(staged, len(self.new_payload)))
        self.assertIs(
            select_newest(self.old_bank, staged, len(self.old_payload)),
            staged,
        )

    def test_sha_mismatch_falls_back_to_previous_generation(self) -> None:
        corrupt = bytearray(
            make_header(self.new_payload, 8, True) + self.new_payload
        )
        corrupt[-1] ^= 0x01
        self.assertIs(
            select_newest(self.old_bank, corrupt, len(self.old_payload)),
            self.old_bank,
        )

    def test_structurally_damaged_header_is_rejected(self) -> None:
        damaged = bytearray(
            make_header(self.new_payload, 8, True) + self.new_payload
        )
        damaged[0] ^= 0x01
        self.assertFalse(is_valid(damaged, len(self.new_payload)))

    def test_only_known_raw_config_versions_use_legacy_fallback(self) -> None:
        for version in SUPPORTED_LEGACY_VERSIONS:
            self.assertTrue(can_load_legacy_app_config(version))
        self.assertFalse(can_load_legacy_app_config(MAGIC))
        self.assertFalse(can_load_legacy_app_config(ERASED_WORD))
        self.assertFalse(can_load_legacy_app_config(0xDEADBEEF))


if __name__ == "__main__":
    unittest.main()
