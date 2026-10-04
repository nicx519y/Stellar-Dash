export type LocalTokenScriptPlatform = 'windows' | 'macos';

export function localFirmwareTokenScript(secret: string, platform: LocalTokenScriptPlatform = 'windows'): string {
  if (!/^stsvc_[A-Za-z0-9_-]{43}$/.test(secret)) throw new Error('Invalid service token');
  if (platform === 'macos') return [
    '(',
    "  if [ ! -f 'tools/local_firmware_draft.py' ]; then",
    "    printf '%s\\n' 'Run this script in the XORA repository root.' >&2",
    '    exit 1',
    '  fi',
    '  if ! command -v python3 >/dev/null 2>&1; then',
    "    printf '%s\\n' 'Python 3 is required for local token setup.' >&2",
    '    exit 1',
    '  fi',
    `  xora_firmware_token='${secret}'`,
    '  printf \'%s\\n\' "$xora_firmware_token" | python3 tools/local_firmware_draft.py --save-token',
    ')',
  ].join('\n');
  if (platform !== 'windows') throw new Error('Unsupported script platform');
  return [
    '& {',
    "  $ErrorActionPreference = 'Stop'",
    "  if (!(Test-Path -LiteralPath 'tools/local_firmware_draft.py' -PathType Leaf)) {",
    "    throw 'Run this script in the XORA repository root.'",
    '  }',
    `  $xoraFirmwareToken = '${secret}'`,
    '  $xoraFirmwareToken | python tools/local_firmware_draft.py --save-token',
    "  if ($LASTEXITCODE -ne 0) { throw 'Local token setup failed.' }",
    '}',
  ].join('\n');
}
