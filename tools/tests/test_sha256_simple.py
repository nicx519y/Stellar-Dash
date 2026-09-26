"""Compare the production SHA implementation at both optimization levels."""
import hashlib
import random
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
LIBRARY = ROOT / "application" / "Libs" / "sha256_simple"
HARNESS = r'''
#include <stdio.h>
#include <stdlib.h>
#include "sha256_simple.h"
int main(int argc, char **argv) {
    unsigned char buffer[4096], digest[32];
    sha256_simple_ctx_t ctx;
    if (argc != 3) return 2;
    size_t chunk = (size_t)strtoul(argv[2], NULL, 10);
    if (chunk == 0 || chunk > sizeof buffer) return 3;
    FILE *file = fopen(argv[1], "rb");
    if (!file) return 4;
    sha256_simple_init(&ctx);
    sha256_simple_update(&ctx, NULL, 0);
    size_t n;
    while ((n = fread(buffer, 1, chunk, file)) != 0)
        sha256_simple_update(&ctx, buffer, n);
    if (ferror(file)) return 5;
    fclose(file);
    sha256_simple_final(&ctx, digest);
    for (size_t i = 0; i < sizeof digest; ++i) printf("%02x", digest[i]);
    puts("");
    return 0;
}
'''


class Sha256SimpleTests(unittest.TestCase):
    def test_known_binary_and_streaming_vectors_at_og_and_o2(self):
        compiler = shutil.which("gcc")
        self.assertIsNotNone(compiler, "host gcc required")
        rng = random.Random(256)
        vectors = [b"", b"abc", b"a" * 1000000, bytes(range(256)) * 1511]
        vectors += [rng.randbytes(n) for n in (1, 55, 56, 63, 64, 65, 119, 120, 127, 128, 129)]
        vectors += [b"\xff" * 64, b"\x80\x00\x00\x00" * 33]
        with tempfile.TemporaryDirectory(prefix="hbox-sha-") as temp:
            folder = Path(temp)
            source = folder / "harness.c"
            source.write_text(HARNESS)
            for optimization in ("-Og", "-O2"):
                executable = folder / (optimization[1:] + ".exe")
                result = subprocess.run([
                    compiler, "-std=c11", optimization, "-Wall", "-Wextra", "-Werror",
                    "-I", str(LIBRARY), str(source), str(LIBRARY / "sha256_simple.c"),
                    "-o", str(executable),
                ], capture_output=True, text=True, timeout=60)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                for index, payload in enumerate(vectors):
                    file = folder / "input.bin"
                    file.write_bytes(payload)
                    expected = hashlib.sha256(payload).hexdigest()
                    for chunk in (1, 7, 63, 64, 65, 4096):
                        with self.subTest(optimization=optimization, vector=index, chunk=chunk):
                            run = subprocess.run([str(executable), str(file), str(chunk)],
                                                 capture_output=True, text=True, timeout=10)
                            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                            self.assertEqual(run.stdout.strip(), expected)


if __name__ == "__main__":
    unittest.main()
