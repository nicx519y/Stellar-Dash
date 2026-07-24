# HBox Web Config

The V2 application is a server-hosted static Next.js site. It talks to the
CH585 Maintenance HID collection through WebHID; the browser is only a relay
for STM32 attestation and the server-signed session permit.

## V2 hosted development

```bash
npm install
cp .env.example .env.local
npm run dev
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
