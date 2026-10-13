"""Exercise generated policy through the production dispatcher lookup."""
from copy import deepcopy
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch

from tools import generate_webhid_command_policy as generator
from tools.generate_webhid_command_policy import MANIFEST, OUTPUTS, ROOT, render
from tools.tests.application_paths import run_native


class GeneratedWebHidCommandPolicyTests(unittest.TestCase):
    def setUp(self):
        self.manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))

    def test_checked_in_outputs_are_current_and_check_does_not_write(self):
        before = [(path.read_bytes(), path.stat().st_mtime_ns) for path in OUTPUTS]
        result = run_native(
            [sys.executable, str(ROOT / "tools/generate_webhid_command_policy.py"), "--check"],
            capture_output=True, text=True, timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(before, [(path.read_bytes(), path.stat().st_mtime_ns) for path in OUTPUTS])
        self.assertEqual(render(self.manifest), render(deepcopy(self.manifest)))

    def test_check_rejects_missing_and_stale_outputs_without_repairing_them(self):
        with tempfile.TemporaryDirectory(prefix="xora-stale-policy-") as directory:
            root = Path(directory)
            outputs = (root / "policy.h", root / "policy.ts")
            expected = render(self.manifest)
            with patch.object(generator, "ROOT", root), patch.object(generator, "OUTPUTS", outputs):
                self.assertEqual(generator.main(["--check"]), 1)
                self.assertFalse(any(path.exists() for path in outputs))
                for path, content in zip(outputs, expected):
                    path.write_text(content, encoding="utf-8")
                outputs[1].write_text("stale\n", encoding="utf-8")
                before = [(path.read_bytes(), path.stat().st_mtime_ns) for path in outputs]
                self.assertEqual(generator.main(["--check"]), 1)
                self.assertEqual(before, [(path.read_bytes(), path.stat().st_mtime_ns) for path in outputs])

    def test_generator_rejects_ambiguous_or_incomplete_authorization(self):
        mutations = []
        duplicate = deepcopy(self.manifest)
        duplicate["groups"][1]["commands"][0]["name"] = duplicate["groups"][0]["commands"][0]["name"]
        mutations.append(duplicate)
        unknown = deepcopy(self.manifest)
        unknown["groups"][0]["scope"] = "config.anything"
        mutations.append(unknown)
        missing = deepcopy(self.manifest)
        missing["groups"].pop()
        mutations.append(missing)
        count = deepcopy(self.manifest)
        count["commandCount"] += 1
        mutations.append(count)
        service = deepcopy(self.manifest)
        service["serviceCommands"][0]["name"] = service["groups"][0]["commands"][0]["name"]
        mutations.append(service)
        inline = deepcopy(self.manifest)
        read = inline["groups"][0]["commands"]
        ping = next(item for item in read if item["name"] == "ping")
        read.remove(ping)
        inline["groups"][1]["commands"].append(ping)
        mutations.append(inline)
        for manifest in mutations:
            with self.subTest(manifest=manifest):
                with self.assertRaises(ValueError):
                    render(manifest)

    def test_actual_dispatcher_lookup_preserves_every_scope_and_rejects_unknown_commands(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler, "g++ is required for the production scope lookup test")
        source = (ROOT / "application/Src/webconfig/webhid_rpc_dispatcher.cpp").read_text(encoding="utf-8")
        self.assertIn('#include "webhid_command_policy.generated.h"', source)
        # Compile the actual lookup method, without MCU/RPC side effects.
        helper = source[source.index("bool commandIn("):source.index("WebHidRpcResult encodeRoot(")]
        method = source[source.index("uint32_t WebHidRpcDispatcher::requiredScope("):
                        source.index("WebHidRpcResult WebHidRpcDispatcher::dispatch(")]
        scopes = {
            "config.read": "HBOX_SCOPE_CONFIG_READ",
            "config.write": "HBOX_SCOPE_CONFIG_WRITE",
            "monitor.read": "HBOX_SCOPE_MONITOR_READ",
            "device.control": "HBOX_SCOPE_DEVICE_CONTROL",
            "firmware.update": "HBOX_SCOPE_FIRMWARE_UPDATE",
        }
        cases = [(name, scopes[group["scope"]]) for group in self.manifest["groups"]
                 for name in [command["name"] for command in group["commands"]]]
        cases.extend((command["name"], scopes[command["scope"]]) for command in self.manifest["serviceCommands"])
        cases.append(("session.end", "HBOX_SCOPE_CONFIG_READ"))
        cases.extend((name, "0u") for name in ["binary.exchange", "stream.begin", "stream.credit", "stream.complete", "stream.abort"])
        cases.extend((name, "UINT32_MAX") for name in ["", "missing", "reboot.extra", "REBOOT", " begin_release_install", "resources_commit/", "benchmark.start"])
        assertions = "\n".join(f'assert(WebHidRpcDispatcher::requiredScope({json.dumps(name)}) == {scope});' for name, scope in cases)
        program = ('#include <cassert>\n#include <string>\n#include <cstddef>\n'
                   '#include "device_security_protocol.h"\n#include "webhid_command_policy.generated.h"\n'
                   'class WebHidRpcDispatcher { public: static uint32_t requiredScope(const std::string &); };\n'
                   + helper + method + '\nint main() {\n' + assertions + '\n}\n')
        with tempfile.TemporaryDirectory(prefix="xora-command-policy-") as directory:
            path = Path(directory)
            (path / "policy.cpp").write_text(program, encoding="utf-8")
            exe = path / "policy.exe"
            result = run_native([compiler, "-std=c++17", f"-I{ROOT / 'common'}", str(path / "policy.cpp"), "-o", str(exe)], capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = run_native([str(exe)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
