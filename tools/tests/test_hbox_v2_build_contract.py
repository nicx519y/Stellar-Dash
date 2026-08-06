import unittest
from unittest import mock

from tools import hbox
from tools.build import BuildTool


class HboxV2BuildContractTests(unittest.TestCase):
    def test_app_all_does_not_generate_embedded_webresources(self) -> None:
        with mock.patch.object(
            hbox, "_run_python_tool", return_value=0
        ) as run_python, mock.patch.object(
            hbox, "_run_pack_assets", return_value=0
        ) as pack_assets, mock.patch.object(
            hbox, "_run_node_makefsdata", return_value=0
        ) as makefsdata:
            result = hbox.main(["build", "appAll", "A"])

        self.assertEqual(result, 0)
        run_python.assert_called_once_with(
            "build.py", ["build", "app", "A"]
        )
        pack_assets.assert_called_once_with()
        makefsdata.assert_not_called()

    def test_app_flash_builds_signs_and_commits_metadata(self) -> None:
        with mock.patch.object(
            hbox, "_run_python_tool", return_value=0
        ) as run_python, mock.patch.object(
            hbox, "_local_webconfig_state_is_initialized", return_value=True
        ):
            result = hbox.main(["flash", "app", "B"])

        self.assertEqual(result, 0)
        self.assertEqual(
            run_python.call_args_list,
            [
                mock.call(
                    "webconfig_local.py",
                    [
                        "build",
                        "--slot",
                        "B",
                        "--skip-web",
                        "--jobs",
                        "4",
                    ],
                ),
                mock.call("webconfig_flash.py", ["--simple-execute"]),
            ],
        )

    def test_first_app_flash_initializes_local_identity(self) -> None:
        with mock.patch.object(
            hbox, "_run_python_tool", return_value=0
        ) as run_python, mock.patch.object(
            hbox, "_local_webconfig_state_is_initialized", return_value=False
        ):
            result = hbox.main(["flash", "app", "A"])

        self.assertEqual(result, 0)
        self.assertEqual(
            run_python.call_args_list,
            [
                mock.call("webconfig_local.py", ["init"]),
                mock.call(
                    "webconfig_local.py",
                    [
                        "build",
                        "--slot",
                        "A",
                        "--skip-web",
                        "--jobs",
                        "4",
                    ],
                ),
                mock.call("webconfig_flash.py", ["--simple-execute"]),
            ],
        )

    def test_code_flash_keeps_low_level_code_only_semantics(self) -> None:
        with mock.patch.object(
            hbox, "_run_python_tool", return_value=0
        ) as run_python:
            result = hbox.main(["flash", "code", "B"])

        self.assertEqual(result, 0)
        run_python.assert_called_once_with(
            "build.py", ["flash", "app", "B"]
        )

    def test_hosted_and_legacy_web_builds_are_explicit(self) -> None:
        with mock.patch.object(
            hbox, "_run_hosted_web_build", return_value=0
        ) as hosted, mock.patch.object(
            hbox, "_run_legacy_embedded_web_build", return_value=0
        ) as legacy:
            self.assertEqual(hbox.main(["web", "build"]), 0)
            hosted.assert_called_once_with()
            legacy.assert_not_called()

        with mock.patch.object(
            hbox, "_run_hosted_web_build", return_value=0
        ) as hosted, mock.patch.object(
            hbox, "_run_legacy_embedded_web_build", return_value=0
        ) as legacy:
            self.assertEqual(hbox.main(["web", "build-legacy"]), 0)
            legacy.assert_called_once_with()
            hosted.assert_not_called()

    def test_v2_flash_transaction_never_flashes_webresources(self) -> None:
        tool = BuildTool.__new__(BuildTool)
        tool.flash_application = mock.Mock(return_value=True)
        tool.flash_system_assets = mock.Mock(return_value=True)
        tool.flash_sysbg = mock.Mock(return_value=True)
        tool.flash_web_resources = mock.Mock(return_value=True)

        self.assertTrue(tool.flash_v2_slot_contents("A"))
        tool.flash_application.assert_called_once_with("A")
        tool.flash_system_assets.assert_called_once_with(allow_missing=True)
        tool.flash_sysbg.assert_called_once_with()
        tool.flash_web_resources.assert_not_called()

    def test_local_webconfig_commands_delegate_without_flashing(self) -> None:
        cases = (
            ("local-init", "init", []),
            ("local-status", "status", []),
            ("probe-revision", "probe-revision", ["--openocd", "probe"]),
            ("local-build", "build", []),
            ("local-serve", "serve", ["--port", "3001"]),
        )
        for target, command, extra in cases:
            with self.subTest(target=target), mock.patch.object(
                hbox, "_run_python_tool", return_value=0
            ) as run_python:
                result = hbox.main(["web", target, *extra])

            self.assertEqual(result, 0)
            run_python.assert_called_once_with(
                "webconfig_local.py",
                [command, *extra],
            )

    def test_local_stm32_flash_uses_dedicated_fail_closed_tool(self) -> None:
        arguments = [
            "--openocd",
            "C:/approved/openocd.exe",
            "--stlink-serial",
            "00112233445566778899AABB",
            "--dry-run",
        ]
        with mock.patch.object(
            hbox, "_run_python_tool", return_value=0
        ) as run_python:
            result = hbox.main(
                ["web", "local-flash-stm32", *arguments]
            )

        self.assertEqual(result, 0)
        run_python.assert_called_once_with(
            "webconfig_flash.py",
            arguments,
        )


if __name__ == "__main__":
    unittest.main()
