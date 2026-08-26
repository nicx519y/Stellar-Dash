import hashlib
import json
import unittest
from pathlib import Path
from unittest import mock

from tools import hbox


ROOT = Path(__file__).resolve().parents[2]
CONTRACT = ROOT / "tools" / "frozen_flash_contract.json"


class FrozenFlashContractTests(unittest.TestCase):
    def test_accepted_flash_implementation_is_unchanged(self) -> None:
        contract = json.loads(CONTRACT.read_text(encoding="utf-8"))
        self.assertEqual(contract["version"], 4)
        self.assertTrue(contract["files"])
        for relative, expected in contract["files"].items():
            with self.subTest(path=relative):
                path = ROOT / relative
                self.assertTrue(path.is_file(), relative)
                actual = hashlib.sha256(path.read_bytes()).hexdigest()
                self.assertEqual(actual, expected, relative)

    def test_flash_tx_still_uses_the_accepted_stlink_iap_route(self) -> None:
        with mock.patch.object(
            hbox, "_run_python_tool", return_value=0
        ) as run_python:
            result = hbox.main(["flash", "tx"])

        self.assertEqual(result, 0)
        run_python.assert_called_once_with(
            "ch585_stlink_update.py", ["--execute"]
        )


if __name__ == "__main__":
    unittest.main()
