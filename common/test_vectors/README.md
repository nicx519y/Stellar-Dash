# WebHID security golden vectors

`webhid_security_v1.json` is the canonical cross-runtime fixture for the
HBox WebHID V1 security protocol. It locks down:

- the 236-byte `hbox_device_session_permit_v1_t` layout and raw P-256
  `r || s` signature;
- P-256 ECDH output;
- both HKDF-SHA256 keys and 8-byte nonce prefixes;
- the mixed-endian nonce rule (8-byte prefix plus big-endian sequence);
- both directions of the fixed 64-byte AES-256-GCM HID report;
- rejection after header, ciphertext, or tag modification.

The fixture contains no PEM and no stored private scalar. The generator
derives intentionally public, deterministic **TEST ONLY** scalar values from
the labels recorded in the JSON at runtime. Those values are not secrets and
must never be copied into production, manufacturing, firmware release, or
WebConfig authorization configuration.

Regenerate and check drift:

```text
node tools/generate_webhid_security_golden.js --write
node tools/generate_webhid_security_golden.js --check
```

The same JSON is consumed by:

- `server/tests/webhid-security-golden.test.js` (Node/OpenSSL);
- `application/www/tests/webhid-security-golden.test.cjs`
  (browser WebCrypto and the production TypeScript codec);
- `tools/tests/webhid_security_golden_test.c` through
  `tools/tests/test_webhid_security_golden.py` (the exact Mbed TLS wrappers
  used by STM32).

This fixture is protocol ABI. A deliberate protocol change must update the
generator, all three consumers, and the protocol version together.
