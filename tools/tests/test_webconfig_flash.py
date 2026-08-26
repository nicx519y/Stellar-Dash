import json
import tempfile
import unittest
from contextlib import contextmanager
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

from tools import webconfig_flash


class WebConfigFlashTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="hbox-local-flash-")
        self.state_dir = Path(self.temporary.name) / "state"
        self.artifacts = self.state_dir / "artifacts"
        self.artifacts.mkdir(parents=True)
        payloads = {
            "internal-flash-provisioning.bin": (
                b"I" * webconfig_flash.INTERNAL_FLASH_BYTES
            ),
            "application-slot-a.bin": b"A" * 128,
            "adc-mapping-slot-a.bin": b"D" * 64,
            webconfig_flash.local.SYSTEM_ASSETS_FILENAME: b"S" * 96,
            webconfig_flash.local.SYSTEM_BACKGROUND_FILENAME: b"B" * 160,
            "metadata.bin": b"M" * webconfig_flash.METADATA_STRUCT_SIZE,
        }
        for name, payload in payloads.items():
            (self.artifacts / name).write_bytes(payload)
        self.manifest = {
            "formatVersion": 3,
            "deviceId": "0123456789abcdef0123456789abcdef",
            "productId": "HBOX",
            "pcbRevision": "2.0.0",
            "firmwareMeasurement": "a" * 64,
            "siliconRevisionQualification": {
                "enabled": False,
                "stm32RevisionId": None,
            },
            "addresses": webconfig_flash._expected_addresses(),
            "files": {
                name: {
                    "bytes": path.stat().st_size,
                    "sha256": webconfig_flash._sha256(path),
                }
                for name, path in (
                    (name, self.artifacts / name) for name in payloads
                )
            },
        }
        self.serial = "00112233445566778899AABB"
        self.uid = "A1B2C3D4E5F60718293A4B5C"
        self.target = webconfig_flash.TargetIdentity(
            stlink_serial=self.serial,
            dbgmcu_idcode=0x20030450,
            device_id=0x450,
            revision_id=0x2003,
            uid=self.uid,
        )
        self.openocd = Path("C:/approved/openocd.exe")

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def _call(
        self,
        *,
        execute: bool,
        probe_target_only: bool = False,
        simple_execute: bool = False,
        confirmed_device_id: str | None = None,
        confirmed_target_uid: str | None = None,
        resume_token: str | None = None,
        resume_transaction: str | None = None,
        stlink_replacement: str | None = None,
        stlink_serial: str | None = None,
    ):
        return webconfig_flash.flash_stm32(
            self.state_dir,
            self.openocd,
            execute=execute,
            probe_target_only=probe_target_only,
            simple_execute=simple_execute,
            stlink_serial_argument=stlink_serial or self.serial,
            confirmed_device_id=confirmed_device_id,
            confirmed_target_uid=confirmed_target_uid,
            resume_token=resume_token,
            resume_transaction_argument=resume_transaction,
            stlink_replacement_argument=stlink_replacement,
        )

    def _common_patches(self):
        return (
            mock.patch.object(
                webconfig_flash.local,
                "load_verified_artifact_manifest",
                return_value=self.manifest,
            ),
            mock.patch.object(
                webconfig_flash,
                "validate_openocd_executable",
                return_value=self.openocd,
            ),
            mock.patch.object(
                webconfig_flash,
                "probe_target_identity",
                return_value=self.target,
            ),
        )

    @staticmethod
    def _write_backup(payload: bytes):
        def write(
            _openocd,
            _serial,
            _uid,
            destination,
            *,
            replace_existing=False,
        ):
            destination.parent.mkdir(parents=True, exist_ok=True)
            if destination.exists() and not replace_existing:
                raise AssertionError("unexpected backup replacement")
            destination.write_bytes(payload)
            return destination

        return write

    def _transaction_dir(self) -> Path:
        transactions = self.state_dir / "flash-transactions"
        directories = [path for path in transactions.iterdir() if path.is_dir()]
        self.assertEqual(len(directories), 1)
        return directories[0]

    def test_plan_is_address_locked_and_metadata_is_last(self) -> None:
        plan = webconfig_flash.build_flash_plan(self.state_dir, self.manifest)

        self.assertEqual(
            [item["stage"].filename for item in plan],
            [stage.filename for stage in webconfig_flash.FLASH_STAGES],
        )
        self.assertEqual(
            next(item for item in plan if not item["stage"].qspi)["stage"].address,
            0x08000000,
        )
        self.assertEqual(plan[-1]["stage"].address, webconfig_flash.METADATA_ADDR)
        self.assertTrue(plan[-1]["stage"].reset_after)
        self.assertFalse(any(item["stage"].reset_after for item in plan[:-1]))

        tampered = dict(self.manifest)
        tampered["addresses"] = dict(self.manifest["addresses"])
        tampered["addresses"]["application"] = "0x90010000"
        with self.assertRaisesRegex(
            webconfig_flash.local.LocalWebConfigError,
            "address contract",
        ):
            webconfig_flash.build_flash_plan(self.state_dir, tampered)

    def test_slot_b_plan_uses_slot_b_payloads_and_commits_metadata_last(self) -> None:
        (self.artifacts / "application-slot-b.bin").write_bytes(b"b" * 128)
        (self.artifacts / "adc-mapping-slot-b.bin").write_bytes(b"d" * 64)
        manifest = dict(self.manifest)
        manifest["targetSlot"] = "B"
        manifest["addresses"] = webconfig_flash._expected_addresses("B")
        manifest["files"] = dict(self.manifest["files"])
        for name in ("application-slot-b.bin", "adc-mapping-slot-b.bin"):
            path = self.artifacts / name
            manifest["files"][name] = {
                "bytes": path.stat().st_size,
                "sha256": webconfig_flash._sha256(path),
            }

        plan = webconfig_flash.build_flash_plan(self.state_dir, manifest)

        self.assertEqual(plan[0]["stage"].filename, "application-slot-b.bin")
        self.assertEqual(
            plan[0]["stage"].address,
            webconfig_flash.SLOT_B_APPLICATION_ADDR,
        )
        self.assertEqual(plan[1]["stage"].filename, "adc-mapping-slot-b.bin")
        self.assertEqual(plan[-1]["stage"].filename, "metadata.bin")
        self.assertTrue(plan[-1]["stage"].reset_after)

    def test_default_mode_is_dry_run_and_never_opens_target(self) -> None:
        common = self._common_patches()
        with common[0], common[1], common[2] as probe, mock.patch.object(
            webconfig_flash, "_program_internal_flash"
        ) as program:
            plan = self._call(execute=False)

        self.assertEqual(len(plan), len(webconfig_flash.FLASH_STAGES))
        probe.assert_not_called()
        program.assert_not_called()

    def test_simple_execute_discovers_uid_then_uses_protected_lock_order(
        self,
    ) -> None:
        events = []
        expected_result = [{"protected": True}]

        @contextmanager
        def openocd_lock():
            events.append("global-enter")
            try:
                yield
            finally:
                events.append("global-exit")

        @contextmanager
        def stlink_lock(serial):
            events.append(f"stlink-enter:{serial}")
            try:
                yield
            finally:
                events.append("stlink-exit")

        @contextmanager
        def uid_lock(uid):
            events.append(f"uid-enter:{uid}")
            try:
                yield
            finally:
                events.append("uid-exit")

        def probe(_openocd, serial, *, run_after):
            self.assertEqual(serial, self.serial)
            events.append(f"probe:{run_after}")
            return self.target

        def execute_protected(*_args, **_kwargs):
            events.append("execute-protected")
            return expected_result

        with mock.patch.object(
            webconfig_flash.local,
            "load_verified_artifact_manifest",
            return_value=self.manifest,
        ), mock.patch.object(
            webconfig_flash,
            "validate_openocd_executable",
            return_value=self.openocd,
        ), mock.patch.object(
            webconfig_flash,
            "_openocd_lock",
            openocd_lock,
        ), mock.patch.object(
            webconfig_flash,
            "_stlink_lock",
            stlink_lock,
        ), mock.patch.object(
            webconfig_flash,
            "_uid_lock",
            uid_lock,
        ), mock.patch.object(
            webconfig_flash,
            "probe_target_identity",
            side_effect=probe,
        ), mock.patch.object(
            webconfig_flash,
            "_create_and_execute_transaction",
            side_effect=execute_protected,
        ):
            result = self._call(
                execute=True,
                simple_execute=True,
            )

        self.assertEqual(result, expected_result)
        self.assertEqual(
            events,
            [
                "global-enter",
                f"stlink-enter:{self.serial}",
                "probe:True",
                "stlink-exit",
                "global-exit",
                f"uid-enter:{self.uid}",
                "global-enter",
                f"stlink-enter:{self.serial}",
                "probe:False",
                "execute-protected",
                "stlink-exit",
                "global-exit",
                "uid-exit",
            ],
        )

    def test_simple_execute_automatically_resumes_exact_transaction(self) -> None:
        plan = webconfig_flash.build_flash_plan(self.state_dir, self.manifest)
        bundle = webconfig_flash._artifact_bundle_digest(self.manifest, plan)
        transaction, _state, _staged, token = (
            webconfig_flash._prepare_transaction(
                self.state_dir,
                self.manifest,
                plan,
                self.target,
                bundle,
            )
        )
        expected_result = [{"automatically_resumed": True}]
        common = self._common_patches()
        with common[0], common[1], common[2] as probe, mock.patch.object(
            webconfig_flash,
            "_execute_prepared_transaction",
            return_value=expected_result,
        ) as execute_prepared:
            result = self._call(
                execute=True,
                simple_execute=True,
            )

        self.assertEqual(result, expected_result)
        self.assertEqual(probe.call_count, 2)
        execute_prepared.assert_called_once()
        self.assertEqual(execute_prepared.call_args.args[0], transaction)
        self.assertEqual(execute_prepared.call_args.args[5], token)
        self.assertIsNone(
            execute_prepared.call_args.kwargs["issued_resume_token"]
        )

    def test_probe_reads_uid_through_exact_stlink_serial(self) -> None:
        output = "\n".join(
            (
                "0x5c001000: 20030450",
                "0x1ff1e800: a1b2c3d4 e5f60718 293a4b5c",
            )
        )
        with mock.patch.object(
            webconfig_flash.local,
            "_run",
            return_value=SimpleNamespace(returncode=0, stdout=output),
        ) as run:
            identity = webconfig_flash.probe_target_identity(
                self.openocd,
                self.serial,
                run_after=False,
            )

        self.assertEqual(identity.uid, self.uid)
        self.assertEqual(identity.device_id, 0x450)
        command = run.call_args.args[0]
        self.assertIn(f"adapter serial {self.serial}", command)
        self.assertIn(
            f"adapter speed {webconfig_flash.STM32_PROBE_SWD_KHZ}",
            command,
        )
        self.assertIn("mdw 0x1FF1E800 3", command)
        self.assertLess(
            command.index(f"adapter serial {self.serial}"),
            command.index("init"),
        )
        self.assertNotIn("reset run", command)

    def test_probe_retries_connect_under_reset(self) -> None:
        output = "\n".join(
            (
                "0x5c001000: 20030450",
                "0x1ff1e800: a1b2c3d4 e5f60718 293a4b5c",
            )
        )
        with mock.patch.object(
            webconfig_flash.local,
            "_run",
            side_effect=(
                webconfig_flash.local.LocalWebConfigError("no target"),
                SimpleNamespace(returncode=0, stdout=output),
            ),
        ) as run:
            identity = webconfig_flash.probe_target_identity(
                self.openocd,
                self.serial,
                run_after=False,
            )

        self.assertEqual(identity.uid, self.uid)
        self.assertEqual(run.call_count, 2)
        normal_command = run.call_args_list[0].args[0]
        fallback_command = run.call_args_list[1].args[0]
        self.assertNotIn("reset_config connect_assert_srst", normal_command)
        self.assertIn("reset_config connect_assert_srst", fallback_command)
        self.assertLess(
            fallback_command.index("reset_config connect_assert_srst"),
            fallback_command.index("init"),
        )

    def test_execute_requires_artifact_and_physical_uid_confirmations(self) -> None:
        common = self._common_patches()
        with common[0], common[1], common[2], mock.patch.object(
            webconfig_flash, "_backup_internal_flash"
        ) as backup:
            with self.assertRaisesRegex(
                webconfig_flash.local.LocalWebConfigError,
                "confirm-device-id",
            ):
                self._call(
                    execute=True,
                    confirmed_device_id="wrong-device",
                    confirmed_target_uid=self.uid,
                )
            with self.assertRaisesRegex(
                webconfig_flash.local.LocalWebConfigError,
                "confirmed STM32 UID",
            ):
                self._call(
                    execute=True,
                    confirmed_device_id=self.manifest["deviceId"],
                    confirmed_target_uid="0" * 23 + "1",
                )

        backup.assert_not_called()

    def test_existing_exact_transaction_requires_resume_before_probe(self) -> None:
        plan = webconfig_flash.build_flash_plan(self.state_dir, self.manifest)
        bundle = webconfig_flash._artifact_bundle_digest(self.manifest, plan)
        transaction_id = f"{self.uid}-{bundle}"
        transaction = (
            self.state_dir / "flash-transactions" / transaction_id
        )
        transaction.mkdir(parents=True)

        common = self._common_patches()
        with common[0], common[1], common[2] as probe:
            with self.assertRaisesRegex(
                webconfig_flash.local.LocalWebConfigError,
                "resume it explicitly",
            ):
                self._call(
                    execute=True,
                    confirmed_device_id=self.manifest["deviceId"],
                    confirmed_target_uid=self.uid,
                )

        probe.assert_not_called()

    def test_execute_locks_confirmed_uid_before_probe_and_rejects_other_target(self) -> None:
        swapped = webconfig_flash.TargetIdentity(
            stlink_serial=self.serial,
            dbgmcu_idcode=self.target.dbgmcu_idcode,
            device_id=self.target.device_id,
            revision_id=self.target.revision_id,
            uid="00112233445566778899AABB",
        )
        with mock.patch.object(
            webconfig_flash.local,
            "load_verified_artifact_manifest",
            return_value=self.manifest,
        ), mock.patch.object(
            webconfig_flash,
            "validate_openocd_executable",
            return_value=self.openocd,
        ), mock.patch.object(
            webconfig_flash,
            "probe_target_identity",
            return_value=swapped,
        ) as probe, mock.patch.object(
            webconfig_flash,
            "_backup_internal_flash",
        ) as backup:
            with self.assertRaisesRegex(
                webconfig_flash.local.LocalWebConfigError,
                "confirmed STM32 UID",
            ):
                self._call(
                    execute=True,
                    confirmed_device_id=self.manifest["deviceId"],
                    confirmed_target_uid=self.uid,
                )

        self.assertEqual(probe.call_count, 1)
        self.assertTrue(all(call.kwargs["run_after"] is False for call in probe.call_args_list))
        backup.assert_not_called()

    def test_execute_uses_immutable_snapshot_serial_lock_and_metadata_last(self) -> None:
        events = []

        def create_blank_and_mutate_source(
            _openocd,
            serial,
            uid,
            destination,
            *,
            replace_existing=False,
        ):
            self.assertEqual(serial, self.serial)
            self.assertEqual(uid, self.uid)
            self.assertFalse(replace_existing)
            events.append("backup")
            destination.write_bytes(b"\xFF" * webconfig_flash.INTERNAL_FLASH_BYTES)
            (self.artifacts / "application-slot-a.bin").write_bytes(b"tampered")
            return destination

        qspi_tool = mock.Mock()

        def record_qspi(path, *_args, **kwargs):
            self.assertEqual(kwargs["stlink_serial"], self.serial)
            self.assertEqual(kwargs["expected_target_uid"], self.uid)
            self.assertEqual(path.parent.name, "staging")
            events.append(path.name)
            return True

        qspi_tool._flash_qspi_file_in_chunks.side_effect = record_qspi
        common = self._common_patches()
        with common[0], common[1], common[2], mock.patch.object(
            webconfig_flash,
            "_backup_internal_flash",
            side_effect=create_blank_and_mutate_source,
        ), mock.patch.object(
            webconfig_flash,
            "_program_internal_flash",
            side_effect=lambda _openocd, serial, uid, path: (
                self.assertEqual(serial, self.serial),
                self.assertEqual(uid, self.uid),
                self.assertEqual(path.parent.name, "staging"),
                events.append(path.name),
            ),
        ) as internal_flash, mock.patch.object(
            webconfig_flash,
            "_new_build_tool",
            return_value=qspi_tool,
        ):
            self._call(
                execute=True,
                confirmed_device_id=self.manifest["deviceId"],
                confirmed_target_uid=self.uid,
            )

        internal_flash.assert_called_once()
        calls = qspi_tool._flash_qspi_file_in_chunks.call_args_list
        self.assertEqual(
            [call.args[0].name for call in calls],
            [stage.filename for stage in webconfig_flash.FLASH_STAGES if stage.qspi],
        )
        self.assertTrue(calls[-1].kwargs["reset_after"])
        self.assertTrue(all(not call.kwargs["reset_after"] for call in calls[:-1]))
        self.assertTrue(
            all(
                call.kwargs["adapter_speed_khz"]
                == webconfig_flash.STM32_QSPI_SWD_KHZ
                for call in calls
            )
        )
        self.assertTrue(
            all(call.kwargs["connect_under_reset_fallback"] for call in calls)
        )
        self.assertEqual(
            events,
            [
                "backup",
                "application-slot-a.bin",
                "adc-mapping-slot-a.bin",
                webconfig_flash.local.SYSTEM_ASSETS_FILENAME,
                webconfig_flash.local.SYSTEM_BACKGROUND_FILENAME,
                "internal-flash-provisioning.bin",
                "metadata.bin",
            ],
        )
        transaction = self._transaction_dir()
        state = json.loads(
            (transaction / "transaction.json").read_text(encoding="utf-8")
        )
        self.assertEqual(state["status"], "completed")
        self.assertTrue((transaction / "internal-before.bin").is_file())
        self.assertEqual(
            webconfig_flash._sha256(transaction / "staging" / "application-slot-a.bin"),
            self.manifest["files"]["application-slot-a.bin"]["sha256"],
        )

    def test_qspi_failure_leaves_recoverable_state_before_internal_write(self) -> None:
        qspi_tool = mock.Mock()
        qspi_tool._flash_qspi_file_in_chunks.return_value = False
        common = self._common_patches()
        with common[0], common[1], common[2], mock.patch.object(
            webconfig_flash,
            "_backup_internal_flash",
            side_effect=self._write_backup(
                b"\xFF" * webconfig_flash.INTERNAL_FLASH_BYTES
            ),
        ), mock.patch.object(
            webconfig_flash, "_program_internal_flash"
        ) as internal_flash, mock.patch.object(
            webconfig_flash, "_new_build_tool", return_value=qspi_tool
        ):
            with self.assertRaisesRegex(
                webconfig_flash.local.LocalWebConfigError,
                "stopped before completing",
            ):
                self._call(
                    execute=True,
                    confirmed_device_id=self.manifest["deviceId"],
                    confirmed_target_uid=self.uid,
                )

        internal_flash.assert_not_called()
        transaction = self._transaction_dir()
        state = json.loads(
            (transaction / "transaction.json").read_text(encoding="utf-8")
        )
        self.assertEqual(state["status"], "payloads-programming")
        self.assertTrue((transaction / "resume-token.txt").is_file())

    def test_offline_resume_requires_and_audits_probe_replacement(self) -> None:
        blank = b"\xFF" * webconfig_flash.INTERNAL_FLASH_BYTES
        qspi_first = mock.Mock()
        qspi_first._flash_qspi_file_in_chunks.return_value = False
        common = self._common_patches()
        with common[0], common[1], common[2], mock.patch.object(
            webconfig_flash,
            "_backup_internal_flash",
            side_effect=self._write_backup(blank),
        ), mock.patch.object(
            webconfig_flash,
            "_new_build_tool",
            return_value=qspi_first,
        ):
            with self.assertRaisesRegex(
                webconfig_flash.local.LocalWebConfigError,
                "stopped before completing",
            ):
                self._call(
                    execute=True,
                    confirmed_device_id=self.manifest["deviceId"],
                    confirmed_target_uid=self.uid,
                )

        transaction = self._transaction_dir()
        token = (transaction / "resume-token.txt").read_text().strip()
        new_serial = "FFEEDDCCBBAA998877665544"
        new_target = webconfig_flash.TargetIdentity(
            stlink_serial=new_serial,
            dbgmcu_idcode=self.target.dbgmcu_idcode,
            device_id=self.target.device_id,
            revision_id=self.target.revision_id,
            uid=self.uid,
        )

        with mock.patch.object(
            webconfig_flash.local,
            "load_verified_artifact_manifest",
            side_effect=AssertionError("resume read current artifacts"),
        ), mock.patch.object(
            webconfig_flash,
            "validate_openocd_executable",
            return_value=self.openocd,
        ), mock.patch.object(
            webconfig_flash,
            "probe_target_identity",
            return_value=new_target,
        ) as probe:
            with self.assertRaisesRegex(
                webconfig_flash.local.LocalWebConfigError,
                "different ST-Link",
            ):
                self._call(
                    execute=True,
                    confirmed_device_id=self.manifest["deviceId"],
                    confirmed_target_uid=self.uid,
                    resume_token=token,
                    resume_transaction=transaction.name,
                    stlink_serial=new_serial,
                )
        probe.assert_not_called()

        qspi_resume = mock.Mock()
        qspi_resume._flash_qspi_file_in_chunks.return_value = True

        def replacement_backup(
            _openocd,
            serial,
            uid,
            destination,
            *,
            replace_existing=False,
        ):
            self.assertEqual(serial, new_serial)
            self.assertEqual(uid, self.uid)
            self.assertFalse(replace_existing)
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(blank)
            return destination

        with mock.patch.object(
            webconfig_flash.local,
            "load_verified_artifact_manifest",
            side_effect=AssertionError("resume read current artifacts"),
        ), mock.patch.object(
            webconfig_flash,
            "validate_openocd_executable",
            return_value=self.openocd,
        ), mock.patch.object(
            webconfig_flash,
            "probe_target_identity",
            return_value=new_target,
        ), mock.patch.object(
            webconfig_flash,
            "_backup_internal_flash",
            side_effect=replacement_backup,
        ), mock.patch.object(
            webconfig_flash,
            "_program_internal_flash",
        ) as internal_flash, mock.patch.object(
            webconfig_flash,
            "_new_build_tool",
            return_value=qspi_resume,
        ):
            self._call(
                execute=True,
                confirmed_device_id=self.manifest["deviceId"],
                confirmed_target_uid=self.uid,
                resume_token=token,
                resume_transaction=transaction.name,
                stlink_replacement=f"{self.serial}:{new_serial}",
                stlink_serial=new_serial,
            )

        internal_flash.assert_called_once()
        for call in qspi_resume._flash_qspi_file_in_chunks.call_args_list:
            self.assertEqual(call.kwargs["stlink_serial"], new_serial)
        state = json.loads(
            (transaction / "transaction.json").read_text(encoding="utf-8")
        )
        self.assertEqual(state["status"], "completed")
        self.assertEqual(state["stlinkSerial"], new_serial)
        self.assertEqual(len(state["stlinkSerialHistory"]), 2)
        replacement = state["stlinkSerialHistory"][1]
        self.assertEqual(replacement["oldSerial"], self.serial)
        self.assertEqual(replacement["newSerial"], new_serial)

    def test_probe_replacement_requires_recorded_original_backup(self) -> None:
        plan = webconfig_flash.build_flash_plan(self.state_dir, self.manifest)
        bundle = webconfig_flash._artifact_bundle_digest(self.manifest, plan)
        transaction, state, _staged, _token = (
            webconfig_flash._prepare_transaction(
                self.state_dir,
                self.manifest,
                plan,
                self.target,
                bundle,
            )
        )
        new_serial = "FFEEDDCCBBAA998877665544"
        with self.assertRaisesRegex(
            webconfig_flash.local.LocalWebConfigError,
            "recorded original",
        ):
            webconfig_flash._replacement_required(
                transaction,
                state,
                new_serial,
                (self.serial, new_serial),
            )

    def test_interrupted_internal_write_requires_bound_resume_token(self) -> None:
        original = bytearray(b"\xFF" * webconfig_flash.INTERNAL_FLASH_BYTES)
        original[:4096] = b"O" * 4096
        qspi_first = mock.Mock()
        qspi_first._flash_qspi_file_in_chunks.return_value = True
        common = self._common_patches()
        with common[0], common[1], common[2], mock.patch.object(
            webconfig_flash,
            "_backup_internal_flash",
            side_effect=self._write_backup(bytes(original)),
        ), mock.patch.object(
            webconfig_flash,
            "_program_internal_flash",
            side_effect=webconfig_flash.local.LocalWebConfigError(
                "simulated power loss"
            ),
        ), mock.patch.object(
            webconfig_flash, "_new_build_tool", return_value=qspi_first
        ):
            with self.assertRaisesRegex(
                webconfig_flash.local.LocalWebConfigError,
                "simulated power loss",
            ):
                self._call(
                    execute=True,
                    confirmed_device_id=self.manifest["deviceId"],
                    confirmed_target_uid=self.uid,
                )

        transaction = self._transaction_dir()
        state_path = transaction / "transaction.json"
        state = json.loads(state_path.read_text(encoding="utf-8"))
        self.assertEqual(state["status"], "internal-programming")
        token = (transaction / "resume-token.txt").read_text().strip()

        common = self._common_patches()
        with common[0], common[1], common[2], mock.patch.object(
            webconfig_flash, "_backup_internal_flash"
        ) as backup:
            with self.assertRaisesRegex(
                webconfig_flash.local.LocalWebConfigError,
                "resume token does not match",
            ):
                self._call(
                    execute=True,
                    confirmed_device_id=self.manifest["deviceId"],
                    confirmed_target_uid=self.uid,
                    resume_token="0" * 32,
                    resume_transaction=transaction.name,
                )
        backup.assert_not_called()

        erased = b"\xFF" * webconfig_flash.INTERNAL_FLASH_BYTES
        qspi_resume = mock.Mock()
        qspi_resume._flash_qspi_file_in_chunks.return_value = True
        common = self._common_patches()
        with common[0], common[1], common[2], mock.patch.object(
            webconfig_flash,
            "_backup_internal_flash",
            side_effect=self._write_backup(erased),
        ), mock.patch.object(
            webconfig_flash, "_program_internal_flash"
        ) as internal_resume, mock.patch.object(
            webconfig_flash, "_new_build_tool", return_value=qspi_resume
        ):
            self._call(
                execute=True,
                confirmed_device_id=self.manifest["deviceId"],
                confirmed_target_uid=self.uid,
                resume_token=token,
                resume_transaction=transaction.name,
            )

        internal_resume.assert_called_once()
        self.assertEqual(
            [
                call.args[0].name
                for call in qspi_resume._flash_qspi_file_in_chunks.call_args_list
            ],
            [stage.filename for stage in webconfig_flash.FLASH_STAGES if stage.qspi],
        )
        completed = json.loads(state_path.read_text(encoding="utf-8"))
        self.assertEqual(completed["status"], "completed")
        self.assertEqual(completed["internalAttempts"], 2)
        self.assertTrue(completed["originalBackup"]["securityTailBlank"])

    def test_internal_recovery_stops_after_three_recorded_attempts(self) -> None:
        blank = b"\xFF" * webconfig_flash.INTERNAL_FLASH_BYTES
        qspi_tool = mock.Mock()
        qspi_tool._flash_qspi_file_in_chunks.return_value = True

        def run_failed_attempt(
            current: bytes,
            token: str | None,
            transaction_id: str | None,
        ) -> None:
            common = self._common_patches()
            with common[0], common[1], common[2], mock.patch.object(
                webconfig_flash,
                "_backup_internal_flash",
                side_effect=self._write_backup(current),
            ), mock.patch.object(
                webconfig_flash,
                "_program_internal_flash",
                side_effect=webconfig_flash.local.LocalWebConfigError(
                    "simulated interrupted internal write"
                ),
            ), mock.patch.object(
                webconfig_flash,
                "_new_build_tool",
                return_value=qspi_tool,
            ):
                with self.assertRaisesRegex(
                    webconfig_flash.local.LocalWebConfigError,
                    "simulated interrupted internal write",
                ):
                    self._call(
                        execute=True,
                        confirmed_device_id=self.manifest["deviceId"],
                        confirmed_target_uid=self.uid,
                        resume_token=token,
                        resume_transaction=transaction_id,
                    )

        run_failed_attempt(blank, None, None)
        transaction = self._transaction_dir()
        token = (transaction / "resume-token.txt").read_text().strip()
        run_failed_attempt(blank, token, transaction.name)
        run_failed_attempt(blank, token, transaction.name)

        state_path = transaction / "transaction.json"
        state = json.loads(state_path.read_text(encoding="utf-8"))
        self.assertEqual(state["status"], "manual-recovery-required")
        self.assertEqual(state["internalAttempts"], 3)
        self.assertEqual(
            state["lastError"],
            "simulated interrupted internal write",
        )

        common = self._common_patches()
        with common[0], common[1], common[2], mock.patch.object(
            webconfig_flash,
            "_backup_internal_flash",
        ) as backup:
            with self.assertRaisesRegex(
                webconfig_flash.local.LocalWebConfigError,
                "not recoverable",
            ):
                self._call(
                    execute=True,
                    confirmed_device_id=self.manifest["deviceId"],
                    confirmed_target_uid=self.uid,
                    resume_token=token,
                    resume_transaction=transaction.name,
                )
        backup.assert_not_called()

    def test_partial_desired_prefix_is_not_proof_of_interrupted_write(self) -> None:
        blank = b"\xFF" * webconfig_flash.INTERNAL_FLASH_BYTES
        qspi_tool = mock.Mock()
        qspi_tool._flash_qspi_file_in_chunks.return_value = True
        common = self._common_patches()
        with common[0], common[1], common[2], mock.patch.object(
            webconfig_flash,
            "_backup_internal_flash",
            side_effect=self._write_backup(blank),
        ), mock.patch.object(
            webconfig_flash,
            "_program_internal_flash",
            side_effect=webconfig_flash.local.LocalWebConfigError(
                "simulated power loss"
            ),
        ), mock.patch.object(
            webconfig_flash,
            "_new_build_tool",
            return_value=qspi_tool,
        ):
            with self.assertRaisesRegex(
                webconfig_flash.local.LocalWebConfigError,
                "simulated power loss",
            ):
                self._call(
                    execute=True,
                    confirmed_device_id=self.manifest["deviceId"],
                    confirmed_target_uid=self.uid,
                )

        transaction = self._transaction_dir()
        token = (transaction / "resume-token.txt").read_text().strip()
        desired = (
            transaction / "staging" / "internal-flash-provisioning.bin"
        ).read_bytes()
        partial = desired[:65536] + b"\xFF" * (
            webconfig_flash.INTERNAL_FLASH_BYTES - 65536
        )
        common = self._common_patches()
        with common[0], common[1], common[2], mock.patch.object(
            webconfig_flash,
            "_backup_internal_flash",
            side_effect=self._write_backup(partial),
        ), mock.patch.object(
            webconfig_flash,
            "_program_internal_flash",
        ) as internal_flash, mock.patch.object(
            webconfig_flash,
            "_new_build_tool",
            return_value=qspi_tool,
        ):
            with self.assertRaisesRegex(
                webconfig_flash.local.LocalWebConfigError,
                "manual recovery",
            ):
                self._call(
                    execute=True,
                    confirmed_device_id=self.manifest["deviceId"],
                    confirmed_target_uid=self.uid,
                    resume_token=token,
                    resume_transaction=transaction.name,
                )

        internal_flash.assert_not_called()
        state = json.loads(
            (transaction / "transaction.json").read_text(encoding="utf-8")
        )
        self.assertEqual(state["status"], "manual-recovery-required")
        self.assertIn("neither the authenticated original", state["lastError"])

    def test_nonblank_tail_without_bound_transaction_is_rejected(self) -> None:
        backup = self.state_dir / "provisioned.bin"
        content = bytearray(b"\xFF" * webconfig_flash.INTERNAL_FLASH_BYTES)
        content[webconfig_flash.INTERNAL_SECURITY_TAIL_OFFSET] = 0
        backup.write_bytes(content)

        with self.assertRaisesRegex(
            webconfig_flash.local.LocalWebConfigError,
            "differs from this artifact",
        ):
            webconfig_flash._internal_flash_needs_programming(
                backup,
                self.artifacts / "internal-flash-provisioning.bin",
            )

    def test_first_execute_never_replaces_existing_different_identity(self) -> None:
        existing = bytearray(
            b"\xFF" * webconfig_flash.INTERNAL_FLASH_BYTES
        )
        existing[webconfig_flash.INTERNAL_SECURITY_TAIL_OFFSET] = 0
        qspi_tool = mock.Mock()
        qspi_tool._flash_qspi_file_in_chunks.return_value = True
        common = self._common_patches()
        with common[0], common[1], common[2], mock.patch.object(
            webconfig_flash,
            "_backup_internal_flash",
            side_effect=self._write_backup(bytes(existing)),
        ), mock.patch.object(
            webconfig_flash,
            "_program_internal_flash",
        ) as internal_flash, mock.patch.object(
            webconfig_flash,
            "_new_build_tool",
            return_value=qspi_tool,
        ):
            with self.assertRaisesRegex(
                webconfig_flash.local.LocalWebConfigError,
                "existing identity/security tail",
            ):
                self._call(
                    execute=True,
                    confirmed_device_id=self.manifest["deviceId"],
                    confirmed_target_uid=self.uid,
                )

        internal_flash.assert_not_called()
        qspi_tool._flash_qspi_file_in_chunks.assert_not_called()
        state = json.loads(
            (
                self._transaction_dir() / "transaction.json"
            ).read_text(encoding="utf-8")
        )
        self.assertEqual(state["status"], "manual-recovery-required")

    def test_first_execute_updates_bootloader_when_security_tail_matches(self) -> None:
        desired = (
            self.artifacts / "internal-flash-provisioning.bin"
        ).read_bytes()
        existing = bytearray(desired)
        existing[:webconfig_flash.INTERNAL_SECURITY_TAIL_OFFSET] = (
            b"B" * webconfig_flash.INTERNAL_SECURITY_TAIL_OFFSET
        )
        qspi_tool = mock.Mock()
        qspi_tool._flash_qspi_file_in_chunks.return_value = True
        common = self._common_patches()
        with common[0], common[1], common[2], mock.patch.object(
            webconfig_flash,
            "_backup_internal_flash",
            side_effect=self._write_backup(bytes(existing)),
        ), mock.patch.object(
            webconfig_flash,
            "_program_internal_flash",
        ) as internal_flash, mock.patch.object(
            webconfig_flash,
            "_new_build_tool",
            return_value=qspi_tool,
        ):
            self._call(
                execute=True,
                confirmed_device_id=self.manifest["deviceId"],
                confirmed_target_uid=self.uid,
            )

        internal_flash.assert_called_once()
        state = json.loads(
            (
                self._transaction_dir() / "transaction.json"
            ).read_text(encoding="utf-8")
        )
        self.assertEqual(state["status"], "completed")

    def test_resume_recovers_pre_write_matching_tail_false_positive(self) -> None:
        desired = (
            self.artifacts / "internal-flash-provisioning.bin"
        ).read_bytes()
        existing = bytearray(desired)
        existing[:webconfig_flash.INTERNAL_SECURITY_TAIL_OFFSET] = (
            b"B" * webconfig_flash.INTERNAL_SECURITY_TAIL_OFFSET
        )

        common = self._common_patches()
        with common[0], common[1], common[2], mock.patch.object(
            webconfig_flash,
            "_backup_internal_flash",
            side_effect=self._write_backup(bytes(existing)),
        ), mock.patch.object(
            webconfig_flash,
            "_internal_flash_needs_programming",
            side_effect=webconfig_flash.local.LocalWebConfigError(
                webconfig_flash.MATCHING_SECURITY_TAIL_FALSE_POSITIVE_ERROR
            ),
        ):
            with self.assertRaisesRegex(
                webconfig_flash.local.LocalWebConfigError,
                "existing identity/security tail",
            ):
                self._call(
                    execute=True,
                    confirmed_device_id=self.manifest["deviceId"],
                    confirmed_target_uid=self.uid,
                )

        transaction = self._transaction_dir()
        state_path = transaction / "transaction.json"
        failed_state = json.loads(state_path.read_text(encoding="utf-8"))
        self.assertEqual(failed_state["status"], "manual-recovery-required")
        self.assertEqual(failed_state["internalAttempts"], 0)
        token = (transaction / "resume-token.txt").read_text().strip()

        qspi_tool = mock.Mock()
        qspi_tool._flash_qspi_file_in_chunks.return_value = True
        common = self._common_patches()
        with common[0], common[1], common[2], mock.patch.object(
            webconfig_flash,
            "_backup_internal_flash",
            side_effect=self._write_backup(bytes(existing)),
        ), mock.patch.object(
            webconfig_flash,
            "_program_internal_flash",
        ) as internal_flash, mock.patch.object(
            webconfig_flash,
            "_new_build_tool",
            return_value=qspi_tool,
        ):
            self._call(
                execute=True,
                confirmed_device_id=self.manifest["deviceId"],
                confirmed_target_uid=self.uid,
                resume_token=token,
                resume_transaction=transaction.name,
            )

        internal_flash.assert_called_once()
        completed = json.loads(state_path.read_text(encoding="utf-8"))
        self.assertEqual(completed["status"], "completed")
        self.assertEqual(completed["internalAttempts"], 1)

    def test_resume_rejects_tampered_authenticated_transaction_state(self) -> None:
        plan = webconfig_flash.build_flash_plan(self.state_dir, self.manifest)
        bundle = webconfig_flash._artifact_bundle_digest(self.manifest, plan)
        transaction, state, _staged, token = (
            webconfig_flash._prepare_transaction(
                self.state_dir,
                self.manifest,
                plan,
                self.target,
                bundle,
            )
        )
        state_path = transaction / "transaction.json"
        persisted = json.loads(state_path.read_text(encoding="utf-8"))
        persisted["artifactManifest"]["pcbRevision"] = "tampered"
        state_path.write_text(json.dumps(persisted), encoding="utf-8")

        with self.assertRaisesRegex(
            webconfig_flash.local.LocalWebConfigError,
            "authentication failed",
        ):
            webconfig_flash._load_resume_transaction(
                self.state_dir,
                state["transactionId"],
                token,
                self.uid,
            )

    def test_durable_write_ignores_fixed_name_crash_residue(self) -> None:
        destination = self.state_dir / "durable" / "transaction.json"
        destination.parent.mkdir(parents=True)
        destination.with_name("transaction.json.tmp").write_text(
            "stale",
            encoding="utf-8",
        )

        webconfig_flash._durable_write_text(destination, "first")
        webconfig_flash._durable_write_text(destination, "second")

        self.assertEqual(destination.read_text(encoding="utf-8"), "second")
        self.assertEqual(
            destination.with_name("transaction.json.tmp").read_text(
                encoding="utf-8"
            ),
            "stale",
        )

    def test_staged_backup_crash_residue_is_reread_and_atomically_replaced(
        self,
    ) -> None:
        plan = webconfig_flash.build_flash_plan(self.state_dir, self.manifest)
        bundle = webconfig_flash._artifact_bundle_digest(self.manifest, plan)
        transaction, state, _staged, token = (
            webconfig_flash._prepare_transaction(
                self.state_dir,
                self.manifest,
                plan,
                self.target,
                bundle,
            )
        )
        original = transaction / "internal-before.bin"
        fixed_partial = transaction / "internal-before.partial"
        stale = b"R" * webconfig_flash.INTERNAL_FLASH_BYTES
        fresh = b"\xFF" * webconfig_flash.INTERNAL_FLASH_BYTES
        original.write_bytes(stale)
        fixed_partial.write_bytes(b"stale partial from an earlier process")
        dump_paths = []

        def emulate_openocd(command, **_kwargs):
            dump = next(
                item
                for item in command
                if isinstance(item, str) and item.startswith("dump_image ")
            )
            start = dump.index("{") + 1
            end = dump.index("}", start)
            dump_path = Path(dump[start:end])
            dump_paths.append(dump_path)
            dump_path.write_bytes(fresh)
            return SimpleNamespace(returncode=0, stdout="")

        with mock.patch.object(
            webconfig_flash.local,
            "_run",
            side_effect=emulate_openocd,
        ):
            captured = webconfig_flash._capture_current_internal_flash(
                self.openocd,
                self.serial,
                self.uid,
                transaction,
                state,
                token,
            )

        self.assertEqual(captured, original)
        self.assertEqual(original.read_bytes(), fresh)
        self.assertEqual(
            fixed_partial.read_bytes(),
            b"stale partial from an earlier process",
        )
        self.assertEqual(len(dump_paths), 1)
        self.assertNotEqual(dump_paths[0], original)
        self.assertNotEqual(dump_paths[0], fixed_partial)
        self.assertTrue(dump_paths[0].name.startswith(".internal-before.bin."))
        self.assertFalse(dump_paths[0].exists())
        self.assertEqual(state["status"], "preflight-complete")
        self.assertEqual(state["originalBackup"]["sha256"], webconfig_flash._sha256(original))
        self.assertTrue(state["originalBackup"]["securityTailBlank"])
        persisted = json.loads(
            (transaction / "transaction.json").read_text(encoding="utf-8")
        )
        webconfig_flash._verify_resume_token_and_state_mac(persisted, token)

    def test_transaction_directory_is_published_only_after_complete_staging(self) -> None:
        plan = webconfig_flash.build_flash_plan(self.state_dir, self.manifest)
        bundle = webconfig_flash._artifact_bundle_digest(self.manifest, plan)
        transaction_id = f"{self.uid}-{bundle}"
        transactions = self.state_dir / "flash-transactions"

        with mock.patch.object(
            webconfig_flash,
            "_stage_flash_plan",
            side_effect=webconfig_flash.local.LocalWebConfigError(
                "simulated staging crash"
            ),
        ):
            with self.assertRaisesRegex(
                webconfig_flash.local.LocalWebConfigError,
                "staging crash",
            ):
                webconfig_flash._prepare_transaction(
                    self.state_dir,
                    self.manifest,
                    plan,
                    self.target,
                    bundle,
                )
        self.assertFalse((transactions / transaction_id).exists())

        orphan = transactions / f".creating-{transaction_id}-crash"
        orphan.mkdir()
        (orphan / "transaction.json").write_text(
            json.dumps(
                {
                    "formatVersion": webconfig_flash.TRANSACTION_FORMAT_VERSION,
                    "targetUid": self.uid,
                    "status": "staged",
                }
            ),
            encoding="utf-8",
        )
        webconfig_flash._reject_other_active_transactions(
            self.state_dir,
            self.uid,
            transaction_id,
        )

        transaction, _state, _staged, _token = (
            webconfig_flash._prepare_transaction(
                self.state_dir,
                self.manifest,
                plan,
                self.target,
                bundle,
            )
        )
        self.assertEqual(transaction.name, transaction_id)
        self.assertTrue((transaction / "transaction.json").is_file())
        self.assertTrue((transaction / "resume-token.txt").is_file())

    def test_global_stlink_and_uid_locks_are_non_reentrant(self) -> None:
        with webconfig_flash._stlink_lock(self.serial):
            with self.assertRaisesRegex(
                webconfig_flash.local.LocalWebConfigError,
                "another process",
            ):
                with webconfig_flash._stlink_lock(self.serial):
                    self.fail("second ST-Link lock unexpectedly succeeded")
        with webconfig_flash._uid_lock(self.uid):
            with self.assertRaisesRegex(
                webconfig_flash.local.LocalWebConfigError,
                "another process",
            ):
                with webconfig_flash._uid_lock(self.uid):
                    self.fail("second UID lock unexpectedly succeeded")
        with webconfig_flash._openocd_lock():
            with self.assertRaisesRegex(
                webconfig_flash.local.LocalWebConfigError,
                "another process",
            ):
                with webconfig_flash._openocd_lock():
                    self.fail("second global OpenOCD lock unexpectedly succeeded")

    def test_execute_lock_order_is_uid_then_global_then_stlink_before_probe(self) -> None:
        events = []

        def lock_factory(name):
            @contextmanager
            def lock(*_args):
                events.append(f"enter:{name}")
                try:
                    yield
                finally:
                    events.append(f"exit:{name}")

            return lock

        swapped = webconfig_flash.TargetIdentity(
            stlink_serial=self.serial,
            dbgmcu_idcode=self.target.dbgmcu_idcode,
            device_id=self.target.device_id,
            revision_id=self.target.revision_id,
            uid="00112233445566778899AABB",
        )

        def probe(*_args, **_kwargs):
            events.append("probe")
            return swapped

        with mock.patch.object(
            webconfig_flash.local,
            "load_verified_artifact_manifest",
            return_value=self.manifest,
        ), mock.patch.object(
            webconfig_flash,
            "validate_openocd_executable",
            return_value=self.openocd,
        ), mock.patch.object(
            webconfig_flash,
            "_uid_lock",
            side_effect=lock_factory("uid"),
        ), mock.patch.object(
            webconfig_flash,
            "_openocd_lock",
            side_effect=lock_factory("openocd"),
        ), mock.patch.object(
            webconfig_flash,
            "_stlink_lock",
            side_effect=lock_factory("stlink"),
        ), mock.patch.object(
            webconfig_flash,
            "probe_target_identity",
            side_effect=probe,
        ):
            with self.assertRaisesRegex(
                webconfig_flash.local.LocalWebConfigError,
                "confirmed STM32 UID",
            ):
                self._call(
                    execute=True,
                    confirmed_device_id=self.manifest["deviceId"],
                    confirmed_target_uid=self.uid,
                )

        self.assertEqual(
            events,
            [
                "enter:uid",
                "enter:openocd",
                "enter:stlink",
                "probe",
                "exit:stlink",
                "exit:openocd",
                "exit:uid",
            ],
        )

    def test_probe_only_uses_global_lock_and_restores_run(self) -> None:
        events = []

        def lock_factory(name):
            @contextmanager
            def lock(*_args):
                events.append(f"enter:{name}")
                try:
                    yield
                finally:
                    events.append(f"exit:{name}")

            return lock

        def probe(*_args, **kwargs):
            events.append(f"probe-run:{kwargs['run_after']}")
            return self.target

        common = self._common_patches()
        with common[0], common[1], mock.patch.object(
            webconfig_flash,
            "_openocd_lock",
            side_effect=lock_factory("openocd"),
        ), mock.patch.object(
            webconfig_flash,
            "_stlink_lock",
            side_effect=lock_factory("stlink"),
        ), mock.patch.object(
            webconfig_flash,
            "probe_target_identity",
            side_effect=probe,
        ):
            self._call(execute=False, probe_target_only=True)

        self.assertEqual(
            events,
            [
                "enter:openocd",
                "enter:stlink",
                "probe-run:True",
                "exit:stlink",
                "exit:openocd",
            ],
        )

    def test_active_transaction_blocks_same_uid_even_with_other_probe(self) -> None:
        transaction = self.state_dir / "flash-transactions" / "other-bundle"
        transaction.mkdir(parents=True)
        (transaction / "transaction.json").write_text(
            json.dumps(
                {
                    "formatVersion": webconfig_flash.TRANSACTION_FORMAT_VERSION,
                    "targetUid": self.uid,
                    "stlinkSerial": "FFFFFFFFFFFFFFFFFFFFFFFF",
                    "status": "payloads-programming",
                }
            ),
            encoding="utf-8",
        )

        with self.assertRaisesRegex(
            webconfig_flash.local.LocalWebConfigError,
            "different artifact bundle",
        ):
            webconfig_flash._reject_other_active_transactions(
                self.state_dir,
                self.uid,
                "expected-transaction",
            )

    def test_internal_program_is_whole_sector_serial_locked_and_no_option_bytes(self) -> None:
        image = self.artifacts / "internal-flash-provisioning.bin"
        with mock.patch.object(
            webconfig_flash.local,
            "_run",
            return_value=SimpleNamespace(returncode=0, stdout=""),
        ) as run:
            webconfig_flash._program_internal_flash(
                self.openocd,
                self.serial,
                self.uid,
                image,
            )

        command = " ".join(str(part) for part in run.call_args.args[0])
        self.assertIn(f"adapter serial {self.serial}", command)
        self.assertIn("flash erase_sector 0 0 0", command)
        self.assertIn("flash write_image", command)
        self.assertIn("verify_image", command)
        self.assertIn("0x08000000", command)
        dev_read = "set hbox_dbgmcu_idcode [mrw 0x5C001000]"
        dev_compare = "($hbox_dbgmcu_idcode & 0xFFF) != 0x450"
        self.assertIn(dev_read, command)
        self.assertIn(dev_compare, command)
        self.assertLess(command.index(dev_read), command.index("flash erase_sector"))
        for index, word in enumerate(("A1B2C3D4", "E5F60718", "293A4B5C")):
            read = f"set hbox_uid_{index} [mrw 0x{webconfig_flash.STM32_UID_ADDRESS + index * 4:08X}]"
            compare = f"$hbox_uid_{index} != 0x{word}"
            self.assertIn(read, command)
            self.assertIn(compare, command)
            self.assertLess(command.index(read), command.index("flash erase_sector"))
        for forbidden in ("option_write", "option_load", "read_unprotect", "RDP"):
            self.assertNotIn(forbidden, command)

    def test_openocd_and_target_identifiers_are_strictly_validated(self) -> None:
        with self.assertRaisesRegex(
            webconfig_flash.local.LocalWebConfigError,
            "absolute path",
        ):
            webconfig_flash.validate_openocd_executable(Path("openocd"))
        for invalid in ("short", "Z" * 24, "0" * 23 + "{"):
            with self.subTest(invalid=invalid), self.assertRaises(
                webconfig_flash.local.LocalWebConfigError
            ):
                webconfig_flash._normalize_stlink_serial(invalid)

        executable = Path(self.temporary.name) / "openocd"
        executable.write_bytes(b"fixture")
        executable.chmod(0o755)
        with mock.patch.object(
            webconfig_flash.local,
            "_run",
            return_value=SimpleNamespace(
                returncode=0,
                stdout="Open On-Chip Debugger 0.10.0",
            ),
        ):
            with self.assertRaisesRegex(
                webconfig_flash.local.LocalWebConfigError,
                "0.11 or newer",
            ):
                webconfig_flash.validate_openocd_executable(executable)

        with mock.patch.object(
            webconfig_flash.local,
            "_run",
            return_value=SimpleNamespace(
                returncode=0,
                stdout="Open On-Chip Debugger 0.11.0+dev-snapshot",
            ),
        ):
            self.assertEqual(
                webconfig_flash.validate_openocd_executable(executable),
                executable.resolve(),
            )

    def test_cli_requires_explicit_transaction_and_token_pair(self) -> None:
        base = [
            "--openocd",
            str(self.openocd),
            "--stlink-serial",
            self.serial,
            "--execute",
            "--confirm-device-id",
            self.manifest["deviceId"],
            "--confirm-target-uid",
            self.uid,
        ]
        with mock.patch.object(webconfig_flash, "flash_stm32") as flash:
            self.assertEqual(
                webconfig_flash.main(base + ["--resume-token", "a" * 32]),
                1,
            )
            self.assertEqual(
                webconfig_flash.main(
                    base
                    + [
                        "--resume-transaction",
                        f"{self.uid}-{'a' * 64}",
                    ]
                ),
                1,
            )
        flash.assert_not_called()

        with mock.patch.object(
            webconfig_flash,
            "flash_stm32",
            return_value=[],
        ) as flash:
            self.assertEqual(
                webconfig_flash.main(
                    base
                    + [
                        "--resume-token",
                        "a" * 32,
                        "--resume-transaction",
                        f"{self.uid}-{'a' * 64}",
                    ]
                ),
                0,
            )
        self.assertEqual(
            flash.call_args.kwargs["resume_transaction_argument"],
            f"{self.uid}-{'a' * 64}",
        )

    def test_cli_simple_execute_needs_no_manual_identity_copy(self) -> None:
        arguments = ["--simple-execute"]
        with mock.patch.object(
            webconfig_flash,
            "flash_stm32",
            return_value=[],
        ) as flash:
            self.assertEqual(webconfig_flash.main(arguments), 0)

        self.assertTrue(flash.call_args.kwargs["execute"])
        self.assertTrue(flash.call_args.kwargs["simple_execute"])
        self.assertIsNone(flash.call_args.args[1])
        self.assertIsNone(flash.call_args.kwargs["stlink_serial_argument"])
        self.assertIsNone(flash.call_args.kwargs["confirmed_device_id"])
        self.assertIsNone(flash.call_args.kwargs["confirmed_target_uid"])

    def test_simple_execute_auto_selects_probe_and_rechecks_uid(self) -> None:
        automatic_target = webconfig_flash.TargetIdentity(
            stlink_serial=webconfig_flash.AUTOMATIC_STLINK_BINDING,
            dbgmcu_idcode=self.target.dbgmcu_idcode,
            device_id=self.target.device_id,
            revision_id=self.target.revision_id,
            uid=self.target.uid,
        )
        plan = webconfig_flash.build_flash_plan(self.state_dir, self.manifest)

        @contextmanager
        def unlocked(*_args, **_kwargs):
            yield

        with mock.patch.object(
            webconfig_flash.local,
            "load_verified_artifact_manifest",
            return_value=self.manifest,
        ), mock.patch.object(
            webconfig_flash,
            "resolve_openocd_executable",
            return_value=self.openocd,
        ) as resolve_openocd, mock.patch.object(
            webconfig_flash,
            "probe_target_identity",
            return_value=automatic_target,
        ) as probe, mock.patch.object(
            webconfig_flash,
            "_create_and_execute_transaction",
            return_value=plan,
        ) as execute, mock.patch.object(
            webconfig_flash,
            "_openocd_lock",
            side_effect=unlocked,
        ), mock.patch.object(
            webconfig_flash,
            "_stlink_lock",
            side_effect=unlocked,
        ), mock.patch.object(
            webconfig_flash,
            "_uid_lock",
            side_effect=unlocked,
        ):
            result = webconfig_flash.flash_stm32(
                self.state_dir,
                None,
                execute=True,
                probe_target_only=False,
                simple_execute=True,
                stlink_serial_argument=None,
                confirmed_device_id=None,
                confirmed_target_uid=None,
                resume_token=None,
                resume_transaction_argument=None,
                stlink_replacement_argument=None,
            )

        self.assertEqual(result, plan)
        self.assertEqual(resolve_openocd.call_count, 2)
        self.assertEqual(probe.call_count, 2)
        self.assertTrue(
            all(
                call.args[1] == webconfig_flash.AUTOMATIC_STLINK_BINDING
                for call in probe.call_args_list
            )
        )
        self.assertEqual(
            execute.call_args.args[-1].stlink_serial,
            webconfig_flash.AUTOMATIC_STLINK_BINDING,
        )

    def test_automatic_stlink_prefix_omits_adapter_serial(self) -> None:
        command = webconfig_flash._internal_openocd_prefix(
            self.openocd,
            webconfig_flash.AUTOMATIC_STLINK_BINDING,
        )
        self.assertIn(
            f"adapter speed {webconfig_flash.STM32_TRANSACTION_SWD_KHZ}",
            command,
        )
        self.assertFalse(
            any(
                isinstance(argument, str)
                and argument.startswith("adapter serial ")
                for argument in command
            )
        )


if __name__ == "__main__":
    unittest.main()
