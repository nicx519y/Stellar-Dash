import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MANAGER = ROOT / "application/Cpp_Core/Src/adc_btns/adc_manager.cpp"
HANDLER = ROOT / "application/Cpp_Core/Src/configs/ms_mark_command_handler.cpp"
MARKER = ROOT / "application/Cpp_Core/Src/adc_btns/adc_btns_marker.cpp"
DISPATCHER = ROOT / "application/Cpp_Core/Src/webhid_rpc_dispatcher.cpp"
SERVER = ROOT / "server/src/switch-mappings.js"
WEB_TYPES = ROOT / "application/www/types/adc.ts"


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace:index + 1]
    raise AssertionError(f"unterminated function: {signature}")


class AdcMappingSingletonContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.manager = MANAGER.read_text(encoding="utf-8")
        cls.handler = HANDLER.read_text(encoding="utf-8")
        cls.marker = MARKER.read_text(encoding="utf-8")
        cls.dispatcher = DISPATCHER.read_text(encoding="utf-8")

    def test_shared_journal_uses_two_dedicated_4k_banks(self) -> None:
        self.assertIn(
            "SHARED_MAPPING_BANK_OFFSETS[2] = {0x1000u, 0x2000u}",
            self.manager,
        )
        self.assertIn("static_assert(sizeof(SharedMappingRecord) <= 4096u", self.manager)
        self.assertRegex(
            self.manager,
            r"struct SharedMappingRecord\s*\{[\s\S]*magic;[\s\S]*schemaVersion;"
            r"[\s\S]*payloadLength;[\s\S]*sequence;[\s\S]*mapping;[\s\S]*crc32;",
        )

    def test_boot_selects_only_crc_valid_newest_record_without_writing_fallback(self) -> None:
        load = function_body(self.manager, "void ADCManager::loadSharedSingleton()")
        fallback = function_body(self.manager, "void ADCManager::selectFactoryFallback()")
        self.assertIn("SharedMappingRecord records[2]", load)
        self.assertIn("isSharedRecordValid(records[bank])", load)
        self.assertIn("sequenceNewer(records[1].sequence, records[0].sequence)", load)
        self.assertIn("selectFactoryFallback();", load)
        self.assertNotIn("QSPI_W25Qxx_Write", fallback)

    def test_boot_resolves_effective_mapping_before_calibration_identity(self) -> None:
        constructor = function_body(self.manager, "ADCManager::ADCManager()")
        common_read = constructor.index(
            "QSPI_W25Qxx_ReadBuffer_WithXIPOrNot((uint8_t *)&common"
        )
        resolve = constructor.index("loadSharedSingleton();")
        self.assertLess(common_read, resolve)
        self.assertEqual(constructor.count("loadSharedSingleton();"), 1)

        # Boot must never repair a shared mapping mismatch by persistently
        # deleting calibration. Mapping installation/deletion owns that state
        # transition explicitly.
        after_common_read = constructor[common_read:]
        self.assertNotIn(
            "memset(common.manualCalibrationValues", after_common_read
        )
        self.assertNotIn(
            "memset(common.autoCalibrationValues", after_common_read
        )
        self.assertNotIn(
            "memset(common.calibratedMappingId", after_common_read
        )

    def test_write_commit_state_changes_only_after_readback_and_crc_validation(self) -> None:
        persist = function_body(self.manager, "bool ADCManager::persistSharedSingleton")
        write = persist.index("QSPI_W25Qxx_WriteBuffer_WithXIPOrNot")
        readback = persist.index("QSPI_W25Qxx_ReadBuffer_WithXIPOrNot")
        validate = persist.index("isSharedRecordValid(verify)")
        bank_commit = persist.index("sharedMappingBank =", validate)
        sequence_commit = persist.index("sharedMappingSequence =", validate)
        self.assertLess(write, readback)
        self.assertLess(readback, validate)
        self.assertLess(validate, bank_commit)
        self.assertLess(validate, sequence_commit)
        self.assertIn("sharedMappingBank == 0 ? 1u : 0u", persist)

    def test_install_validates_digest_before_commit_and_clears_calibration(self) -> None:
        install = function_body(self.manager, "ADCBtnsError ADCManager::installSharedMapping")
        self.assertLess(install.index("isMappingValid(source)"), install.index("persistSharedSingleton(mapping)"))
        self.assertLess(install.index("mappingDigestMatches"), install.index("persistSharedSingleton(mapping)"))
        self.assertIn("memset(common.manualCalibrationValues", install)
        self.assertIn("memset(common.autoCalibrationValues", install)
        self.assertIn("memset(common.calibratedMappingId", install)

    def test_zero_and_partial_mappings_are_installable_and_each_sample_is_persisted(self) -> None:
        validate = function_body(self.manager, "static bool isMappingValid")
        parser = function_body(self.handler, "bool parseInstallMapping")
        step_finish = function_body(self.marker, "void ADCBtnsMarker::stepFinish")
        persist = function_body(self.marker, "ADCBtnsError ADCBtnsMarker::persistProgress")
        self.assertNotIn("mapping.originalValues[i] == 0u", validate)
        self.assertIn("sample->valuedouble < 0.0", parser)
        self.assertNotIn("updateADCMapping", step_finish)
        self.assertIn("sendMarkingStatusNotification", step_finish)
        self.assertIn("ADC_MANAGER.updateADCMapping(step_info.id, progress)", persist)
        self.assertIn("memset(progress.originalValues, 0", persist)

    def test_all_three_layers_share_the_220_byte_sha_format(self) -> None:
        self.assertIn("uint8_t canonical[220]", self.manager)
        self.assertIn("Buffer.alloc(220)", SERVER.read_text(encoding="utf-8"))
        self.assertIn("new Uint8Array(220)", WEB_TYPES.read_text(encoding="utf-8"))

    def test_command_scopes_and_singleton_metadata_are_explicit(self) -> None:
        self.assertIn('"storageMode", "shared-singleton"', self.handler)
        self.assertIn('"installSchemaVersion", 1', self.handler)
        self.assertRegex(
            self.dispatcher,
            r"configRead\[\][\s\S]*ms_mapping_draft_get",
        )
        self.assertRegex(
            self.dispatcher,
            r"configWrite\[\][\s\S]*ms_install_mapping[\s\S]*ms_clear_installed_mapping[\s\S]*ms_mapping_draft_begin",
        )


if __name__ == "__main__":
    unittest.main()
