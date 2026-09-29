"""Exercise the device's actual P-256 verifier with ephemeral Node signatures."""
import json
import pathlib
import shutil
import tempfile
import unittest
from .application_paths import run_native

ROOT = pathlib.Path(__file__).resolve().parents[2]


class ReleaseSignatureTests(unittest.TestCase):
    def test_device_verifier_accepts_raw_signature_and_rejects_tampering(self):
        self.assertIsNotNone(shutil.which('gcc'))
        script = """
          const c=require('node:crypto');
          const k=c.generateKeyPairSync('ec',{namedCurve:'prime256v1'});
          const raw=Buffer.from('{"schemaVersion":2,"product":"XORA"}');
          const pub=k.publicKey.export({format:'der',type:'spki'}).subarray(-65);
          const sig=c.sign('sha256',raw,{key:k.privateKey,dsaEncoding:'ieee-p1363'});
          console.log(JSON.stringify({pub:[...pub],raw:[...raw],sig:[...sig]}));
        """
        generated = run_native(['node', '-e', script], capture_output=True, text=True, timeout=30)
        self.assertEqual(generated.returncode, 0, generated.stderr)
        vector = json.loads(generated.stdout)
        with tempfile.TemporaryDirectory(prefix='xora-release-signature-') as tmp:
            directory = pathlib.Path(tmp)
            header = directory / 'test-trust.h'
            header.write_text('#include <stdint.h>\n#define HBOX_FIRMWARE_RELEASE_PUBLIC_KEY_PROVISIONED 1u\n'
                              'static const uint8_t hbox_firmware_release_public_key[65]={' +
                              ','.join(map(str, vector['pub'])) + '};\n')
            source = directory / 'test.c'
            source.write_text('#include "firmware_signature.h"\n#include <assert.h>\n#include <string.h>\n'
                              'int main(void){uint8_t raw[]={' + ','.join(map(str, vector['raw'])) +
                              '};uint8_t sig[]={' + ','.join(map(str, vector['sig'])) + '};\n'
                              'assert(firmware_release_verify_bytes(raw,sizeof(raw),sig));\n'
                              'raw[0]^=1;assert(!firmware_release_verify_bytes(raw,sizeof(raw),sig));raw[0]^=1;\n'
                              'sig[9]^=1;assert(!firmware_release_verify_bytes(raw,sizeof(raw),sig));\n'
                              'memset(sig,0,64);assert(!firmware_release_verify_bytes(raw,sizeof(raw),sig));\n'
                              'assert(!firmware_release_verify_bytes(raw,0,sig));return 0;}\n')
            library = ROOT / 'application/Libs/mbedtls'
            modules = ['bignum', 'base64', 'asn1parse', 'asn1write', 'aes', 'cipher', 'cipher_wrap',
                       'constant_time', 'ecp', 'ecp_curves', 'ecdh', 'ecdsa', 'gcm', 'hkdf', 'md', 'sha256', 'platform_util']
            executable = directory / 'test.exe'
            command = ['gcc', '-std=c11', '-O2', '-DMBEDTLS_CONFIG_FILE="mbedtls_boot_config.h"',
                       '-include', str(header), '-Icommon', f'-I{library / "include"}',
                       '-Iapplication/Libs/sha256_simple', str(source), 'common/firmware_signature.c',
                       'application/Libs/sha256_simple/sha256_simple.c',
                       *[str(library / 'library' / f'{name}.c') for name in modules], '-o', str(executable)]
            result = run_native(command, cwd=ROOT, capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = run_native([str(executable)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
