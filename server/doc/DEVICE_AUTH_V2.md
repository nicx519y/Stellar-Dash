# Device authentication V2

The V2 WebConfig API is fail-closed. Challenge creation returns `503`, and
firmware downloads cannot obtain a valid bearer session, until the
manufacturing CA public key and the online authorization dependencies are
configured.

This document describes the wire/API contract. Factory enrollment, public-key
build injection and the non-negotiable production gates are documented in
[`docs/DEVICE_IDENTITY_PROVISIONING.md`](../../docs/DEVICE_IDENTITY_PROVISIONING.md)
and
[`docs/WEBCONFIG_V2_PRODUCTION_DEPLOYMENT.md`](../../docs/WEBCONFIG_V2_PRODUCTION_DEPLOYMENT.md).

## Production configuration

Production must use an exact origin, a loopback/private listener, an explicit
trusted-proxy hop count, and an absolute adapter module path:

```text
NODE_ENV=production
LISTEN_HOST=127.0.0.1
TRUST_PROXY_HOPS=1
WEB_CONFIG_ORIGINS=https://firmware.st-dash.com
DEVICE_CA_PUBLIC_KEY_FILE=/run/secrets/hbox-device-ca-public.pem
DEVICE_AUTH_V2_ADAPTER_MODULE=/opt/hbox/device-auth-v2-production-adapter.js
```

`TRUST_PROXY_HOPS` accepts only `0`, `1`, or `2` and must equal the number of
controlled reverse proxies in front of Node. It defaults to `0`.

When `NODE_ENV=production`, `WEB_CONFIG_AUTH_PRIVATE_KEY_PEM`,
`WEB_CONFIG_AUTH_PRIVATE_KEY_FILE`, and the built-in PEM signer are not
accepted as a fallback. The module must export a synchronous factory:

```js
function createDeviceAuthV2Dependencies({ storageManager, environment }) {
  return {
    permitSigner,      // sign(claims), backed by KMS/HSM
    challengeStore,    // issue(record), consume(challengeId)
    tokenStore,        // ttlMs, issue(record), resolve(token), revoke(token)
    devicePolicy,      // check(identity, attestation), get(deviceId)
    challengeLimiter,  // check(key)
    verifyLimiter      // check(key)
  };
}
```

All six dependencies are mandatory. The factory itself must return
synchronously; dependency methods may return values or Promises.

- `permitSigner` must use a non-exportable online KMS/HSM P-256 key and return
  the fixed 236-byte permit with the correct current/next key slot.
- Challenge consumption must be an atomic `GETDEL` or equivalent operation,
  with a 60-second TTL.
- The opaque token store must keep only a hash/HMAC index of the bearer token,
  expose `ttlMs <= 300000`, and be shared by every worker.
- Both rate limiters must be shared across the deployment, not process-local.
- `devicePolicy` must use a transactional database for enrollment, certificate
  uniqueness, firmware allowlists, minimum security version, monotonically
  increasing policy version, and revocation.

The in-process stores, limiter, local PEM signer, and JSON policy file are
development-only. The repository does not ship a production KMS/Redis/database
adapter. The production admin enrollment/policy/revoke path must update the
same transactional policy database read by `devicePolicy`; otherwise those
routes must be disabled in favor of an audited external administration plane.
Any unavailable KMS/store/policy dependency must fail closed.

## Browser protocol

Browser-facing binary values use standard base64; the server also accepts
unpadded base64url for non-browser integration clients. Structures, offsets
and signatures are defined by `common/device_security_protocol.h`; JSON is
transport-only and is never signed.

Create a challenge:

```http
POST /api/v2/device-auth/challenges
Origin: https://firmware.st-dash.com
Content-Type: application/json

{
  "protocol": "hbox-webhid-v1",
  "requestedScopes": ["config.read", "firmware.update"]
}
```

The response contains a 16-byte opaque `challengeId`, 32-byte base64 `nonce`,
absolute `expiresAt`, the requested scopes, and a 60-second lifetime. The
browser creates its P-256 key after this response and sends the challenge,
browser key and scopes to the device.

Verify the device proof:

```http
POST /api/v2/device-auth/verify
Origin: https://firmware.st-dash.com
Content-Type: application/json

{
  "challengeId": "<16 bytes>",
  "challengeNonce": "<32-byte base64>",
  "browserEphemeralPublicKey": "<65-byte SEC1 base64>",
  "requestedScopes": ["config.read", "firmware.update"],
  "deviceAttestation": {
    "deviceId": "<32 hexadecimal characters>",
    "certificate": "<hbox_device_certificate_v1_t base64>",
    "bootAttestation": "<hbox_boot_attestation_v1_t base64>",
    "bootNonce": "<32-byte base64>",
    "deviceEphemeralPublicKey": "<65-byte SEC1 base64>",
    "firmwareMeasurement": "<64 hexadecimal characters>",
    "hardwareVersion": "2.0.0",
    "firmwareVersion": "1.0.0",
    "signature": "<hbox_attestation_transcript_v1_t base64>"
  }
}
```

The challenge is consumed before verification; failed proofs cannot reuse it.
For compatibility with the first browser client, `signature` carries the
complete 354-byte signed transcript, not only its trailing 64-byte signature.
On success the response contains:

- a five-minute opaque `apiToken`, kept only in page memory;
- a 16-byte `sessionId`;
- `scopes` and `expiresInMs`;
- signed `deviceSessionPermit` for the STM32;
- `sessionSalt = SHA-256(deviceSessionPermit)` for browser/device HKDF.

Both directions accept encrypted reports only when
`sequence == previous + 1`; a forward gap, repeat, backward value, zero, or
wrap destroys the device session. The browser clears fragment/checkpoint
assemblers, rejects every pending RPC, closes the transport, and requires a
fresh challenge/permit flow. V1 cannot prove whether a missing IN report was
discardable telemetry or a protected control response, so a checkpoint is
used only after reauthentication to rebuild telemetry state; it never repairs
the old authenticated transport generation.

Use the opaque token only as an HTTP header:

```http
Authorization: Bearer <apiToken>
```

Query-string tokens are intentionally unsupported. `/downloads/*` requires
the `firmware.update` scope and uses `Cache-Control: private, no-store`.

## Enrollment and revocation

V2 enrollment, policy changes and revocation are administrator-only:

- `POST /api/v2/devices`
- `PUT /api/v2/devices/:deviceId/policy`
- `POST /api/v2/devices/:deviceId/revoke`

Enrollment stores only certificate identity and fingerprint metadata, never a
device private key. The policy can set a minimum security version and an
allowlist of full firmware SHA-256 measurements. Revocation or any policy
version change invalidates an opaque API token on its next server request,
provided every node reads the shared current policy.

An already-installed STM32 permit is self-contained and the device does not
contact the server for each WebHID command. Consequently, server revocation
does not remotely terminate that local AES-GCM session immediately. The
maximum revocation latency is the five-minute permit TTL; USB disconnect,
USB suspend, PI10 power loss, role change, explicit `session.end`, timeout, or
a protocol error terminates it earlier. Suspend clears both CH585 WebHID
directions and resets BoardLink credit to zero; resume starts a new transport
generation and requires a fresh authorization instead of replaying queued
reports.

An explicit session reset is also a synchronized transport-generation reset:
CH585 quiesces the WebHID endpoints, consumes any pending endpoint DONE/DATA
toggle (OUT only when `TOG_MATCH` is set), clears both WebConfig queues, and
resets the STM32 BoardLink channel before returning the control ACK. STM32
then waits for a fresh post-reset credit and restarts at fragment zero; an old
partial report cannot resume. If the SIE does not become idle within the
bounded 1ms window, CH585 keeps the endpoints disabled, detaches USB, and
returns a failed CLEAR_FAULT response; recovery requires re-enumeration and
reauthorization. On the normal path EP2 remains NAK until both the CH585
BoardLink channel reset and STM32 WebHID session reset have completed; only
then is ACK restored inside the same IRQ-masked critical section.

Firmware update and reboot additionally require
`HBoxBoard_DangerousActionConfirmed()`. The board implementation reads raw
active-low PC6 (`GPIO1`) and PC9 (`FN`) only while the physical switch is in
USB and the CH585 is locked in maintenance role. Both inputs must first remain
released for 50 ms and then be held together for two seconds; the resulting
authorization expires after ten seconds and is consumed once. A role change,
switch change, timeout, or consumption clears it. Logical mappings, macros,
USB reports, and RF packets cannot assert this gesture. OTA remains an
engineering gate until the debounce, hold, disconnect, and accidental-press
cases pass on production hardware.

Legacy V1 authentication remains available only for already shipped hardware.
It is explicitly not accepted by V2 routes or `/downloads/*`. A successful
legacy firmware-check may instead return a two-minute opaque
`/legacy-downloads/<ticket>/<file>` URL so shipped clients can complete their
existing OTA flow without making the strong download endpoint public.

## Hosted page and release gates

Hosted HTML is served with per-file CSP hashes computed from the exact
generated inline bootstrap script bytes. The effective policy includes
`script-src 'self' 'sha256-...'` and `script-src-attr 'none'`; a reverse proxy
must preserve that response and must not rewrite HTML after hashing.

Formal V2 release creation requires both `HBOX_TRUST_HEADER` and the separately
approved `HBOX_TRUST_HEADER_SHA256`. The release tool rejects missing or
changed public-key bundles, private-key text, missing provisioned-key markers,
and an empty authorization key mask, and records the digest as
`trust_bundle_sha256` in the manifest. Bootloader and application must be
built from that same public-only header.

The STM32H750xB identity is not stored in OTP. The reserved internal Flash
layout is bootloader code through `0x0801BFFF`, a 4 KiB identity journal at
`0x0801C000`, and a 12 KiB security-version journal at `0x0801D000`; all share
one physical erase sector. Factory provisioning, option-byte ordering,
physical confirmation, FS/HS 100 Hz telemetry, disconnect/power-loss cleanup,
and RF spectrum/regression gates remain hardware validation requirements.
