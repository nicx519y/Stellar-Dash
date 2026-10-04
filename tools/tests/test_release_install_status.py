"""Offline journal decoding only; no device or RF runtime access."""
import ctypes
from pathlib import Path
import sys
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from release_install_status import BANK_BYTES, Snapshot, parse_journals


def bank(generation=1, legacy=False, v2=False, install_mode=2, recovery_mode=1):
    value = Snapshot()
    value.magic = 0x32524F58 if legacy else 0x33524F58
    value.generation = generation
    value.phase = 9 if legacy else 15
    value.attempts = 1
    value.restoreAttempts = 2
    value.backupReady = 1
    value.installError = b'TX write or startup verification failed'
    value.recoveryError = b'TX recovery write or startup verification failed'
    value.errorCode = b'TX_RESTORE_FAILED'
    value.txInstallMode = 0xffffffff if v2 else install_mode
    value.txRecoveryMode = 0xffffffff if v2 else recovery_mode
    value.crc3 = value.commit3 = 0xffffffff
    crc_field, commit_field = ('crc', 'commit') if legacy else ('crc2', 'commit2')
    setattr(value, crc_field, zlib.crc32(bytes(value)[:getattr(Snapshot, crc_field).offset]))
    setattr(value, commit_field, 0x54494D43)
    if not legacy and not v2:
        value.crc3 = zlib.crc32(bytes(value)[:Snapshot.crc3.offset])
        value.commit3 = 0x54494D43
    return bytes(value) + b'\xff' * (BANK_BYTES - ctypes.sizeof(value))


class ReleaseStatusTest(unittest.TestCase):
    def test_latest_valid_bank_and_structured_errors(self):
        result = parse_journals(bank(1) + bank(2))
        self.assertEqual((result['bank'], result['phase'], result['restoreAttempts']), (1, 'restore-failed', 2))
        self.assertEqual(result['errorCode'], 'TX_RESTORE_FAILED')
        self.assertIn('startup', result['installError'])
        self.assertEqual((result['txInstallMode'], result['txRecoveryMode']), ('dma', 'small-packet'))

    def test_torn_new_bank_keeps_previous_and_generation_wraps(self):
        torn = bytearray(bank(3))
        torn[Snapshot.commit3.offset] ^= 1
        self.assertEqual(parse_journals(bank(2) + torn)['bank'], 0)
        self.assertEqual(parse_journals(bank(0xFFFFFFFF) + bank(1))['bank'], 1)

    def test_legacy_record_does_not_claim_a_backup(self):
        result = parse_journals(bank(1, legacy=True) + b'\xff' * BANK_BYTES)
        self.assertTrue(result['legacy'])
        self.assertFalse(result['backupReady'])
        self.assertEqual(result['errorCode'], 'LEGACY_NO_BACKUP')
        self.assertEqual(result['txInstallMode'], 'unknown')

    def test_previous_backup_journal_has_unknown_modes(self):
        result = parse_journals(bank(v2=True) + b'\xff' * BANK_BYTES)
        self.assertTrue(result['backupReady'])
        self.assertEqual((result['txInstallMode'], result['txRecoveryMode']), ('unknown', 'unknown'))

    def test_mode_range_and_checksums_are_validated(self):
        for args in ({'install_mode': 3}, {'recovery_mode': 0xffffffff}):
            self.assertEqual(parse_journals(bank(**args) + b'\xff' * BANK_BYTES)['phase'], 'invalid-journal')
        bad = bytearray(bank())
        bad[Snapshot.txInstallMode.offset] ^= 1
        self.assertEqual(parse_journals(bad + b'\xff' * BANK_BYTES)['phase'], 'invalid-journal')

    def test_invalid_or_missing_data_is_not_success(self):
        self.assertEqual(parse_journals(b'\xff' * (2 * BANK_BYTES))['phase'], 'idle')
        bad = bytearray(bank())
        bad[Snapshot.errorCode.offset] ^= 1
        self.assertEqual(parse_journals(bad + b'\xff' * BANK_BYTES)['phase'], 'invalid-journal')
        with self.assertRaises(ValueError):
            parse_journals(bytes(10))


if __name__ == '__main__':
    unittest.main()
