# HBox Web Config

The V2 application is a server-hosted static Next.js site. It talks to the
CH585 Maintenance HID collection through WebHID; the browser is only a relay
for STM32 attestation and the server-signed session permit.

## V2 hosted development

```bash
npm install
cp .env.example .env.local
npm run dev:hosted
```

WebHID requires a Chromium-based browser, HTTPS (localhost is allowed for
development), and a user click before the browser device chooser can open.
Opening the page never silently falls back to the legacy virtual network
adapter.

Production validation and static export:

```bash
npm test
npm run build:hosted
```

Serve the generated `build/` directory from the same HTTPS origin as
`/api/v2/device-auth/*`. The server must apply HSTS, a strict CSP,
`Permissions-Policy: hid=(self)`, exact-origin CORS rules, and an SPA fallback
that serves `index.html` for the configuration routes.

## Hardware-free V2 mock preview

The mock transport exercises the same UI/context/queue path as a V2 device,
and keeps its state in the current tab's `sessionStorage`. It includes fixtures for profiles,
global/screen/LED settings, hotkeys, calibration, ADC mappings, button and
performance monitoring, logs, config import/export, firmware metadata/checks,
and the background-image binary protocol.

```bash
npm run dev:mock
```

Open `http://127.0.0.1:4000`. A purple `MOCK DEVICE` badge confirms that no
hardware or production API is in use. Mock configuration is stored in the
current tab's `sessionStorage`, so it survives reloads but a new tab starts
with clean fixtures.
All configuration URLs are real statically generated routes, so refreshing
`/global`, `/keys`, `/lighting`, `/buttons-performance`, `/switch-marking`,
`/firmware`, or `/view-logs` does not return 404.

To validate/export the mock build:

```bash
npm test
npm run build:mock
npm run preview:mock
```

The mock export is written to `build-mock/`; genuine-device and legacy exports
continue to use `build/`. This separation prevents `makefsdata.js` from
accidentally packaging mock code into firmware. The local preview server binds
to `127.0.0.1:4000` by default; pass another port with
`npm run preview:mock -- --port 4100`.

Mock mode requires both `NEXT_PUBLIC_DEVICE_TRANSPORT=mock` and
`NEXT_PUBLIC_OFFLINE_PREVIEW=true`; it is intended only for local QA and must
not be deployed as the genuine-device V2 site.

## Authentication behavior

- Before attestation, only the fixed bootstrap exchange is accepted.
- The page sends no protected RPC until STM32 accepts the server permit.
- The API bearer token is memory-only and expires in at most five minutes.
- Initial authorization requests only `config.read`, `config.write`, and
  `monitor.read`. Device control, asset writes, and firmware updates end the
  current session and request a new permit only when that UI action is invoked.
- Bearer tokens are sent only to the configured authentication-server origin;
  redirects or custom download URLs on another origin are rejected.
- A USB disconnect, sequence error, role change, or authentication error
  destroys the session. There is no offline configuration-write mode.

## Explicit V1 compatibility build

The old WebSocket/NCM implementation remains available for existing V1
firmware, but only via an explicit build:

```bash
npm run dev:legacy
npm run build:legacy-embedded
node makefsdata.js
```

The legacy build retains the original `ws://device:8081` command semantics and
weak legacy firmware-check authentication. It must not be presented as V2
genuine-device authentication.
