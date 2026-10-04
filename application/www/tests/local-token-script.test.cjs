const test = require('node:test');
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const { localFirmwareTokenScript } = require('../lib/admin/local-token-script.ts');

test('local script rejects malformed secrets before generating shell code', () => {
  for (const invalid of ['', 'invalid', "stsvc_'; Write-Output injected", `stsvc_${'x'.repeat(42)}`, `stsvc_${'x'.repeat(44)}`]) {
    for (const platform of ['windows', 'macos']) assert.throws(() => localFirmwareTokenScript(invalid, platform), /Invalid service token/);
  }
});

const gitExec = process.platform === 'win32' ? spawnSync('git', ['--exec-path'], { encoding: 'utf8', timeout: 10000 }) : null;
const posixShell = process.platform === 'win32'
  ? (gitExec.status === 0 ? path.resolve(gitExec.stdout.trim(), '../../../bin/bash.exe') : '')
  : '/bin/bash';

test('macOS script saves and replaces tokens in Bash, and propagates setup failures', { skip: !fs.existsSync(posixShell), timeout: 60000 }, () => {
  const repo = path.resolve(__dirname, '../../..');
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'xora-macos-token-'));
  try {
    fs.mkdirSync(path.join(root, 'tools'));
    fs.copyFileSync(path.join(repo, 'tools/local_firmware_draft.py'), path.join(root, 'tools/local_firmware_draft.py'));
    const tokenPath = path.join(root, '.hbox/webconfig-local/firmware-manage-token.txt');
    const secrets = [crypto.randomBytes(32), crypto.randomBytes(32)].map(bytes => `stsvc_${bytes.toString('base64url')}`);
    const bridge = process.platform === 'win32' ? 'python3() { python "$@"; }\n' : '';
    const run = (script, options = {}) => spawnSync(posixShell, ['--noprofile', '--norc', '-c', script], {
      cwd: root, env: { ...process.env, PYTHONPATH: path.join(repo, 'tools') }, encoding: 'utf8', timeout: 20000, ...options,
    });
    for (const secret of secrets) {
      const result = run(bridge + localFirmwareTokenScript(secret, 'macos'));
      assert.equal(result.status, 0, result.stderr);
      assert.equal(fs.readFileSync(tokenPath, 'utf8').trim(), secret);
      assert.ok(secrets.every(value => !`${result.stdout}${result.stderr}`.includes(value)));
      assert.deepEqual(fs.readdirSync(path.dirname(tokenPath)), ['firmware-manage-token.txt']);
    }
    const failure = run('python3() { return 7; }\n' + localFirmwareTokenScript(secrets[0], 'macos'));
    assert.equal(failure.status, 7);
    assert.equal(fs.readFileSync(tokenPath, 'utf8').trim(), secrets[1]);
    const missingPython = run(localFirmwareTokenScript(secrets[0], 'macos'), {
      env: { ...process.env, PATH: '' },
    });
    assert.notEqual(missingPython.status, 0);
    assert.match(missingPython.stderr, /Python 3 is required/);
    const outside = fs.mkdtempSync(path.join(root, 'outside-'));
    const refused = run(bridge + localFirmwareTokenScript(secrets[0], 'macos'), { cwd: outside });
    assert.notEqual(refused.status, 0);
    assert.equal(fs.existsSync(path.join(outside, '.hbox')), false);
  } finally { fs.rmSync(root, { recursive: true, force: true }); }
});

test('copied PowerShell script saves and replaces a token through the real CLI without leaking secrets', { skip: process.platform !== 'win32', timeout: 60000 }, () => {
  const repo = path.resolve(__dirname, '../../..');
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'xora-local-token-'));
  try {
    fs.mkdirSync(path.join(root, 'tools'));
    fs.copyFileSync(path.join(repo, 'tools/local_firmware_draft.py'), path.join(root, 'tools/local_firmware_draft.py'));
    const tokenPath = path.join(root, '.hbox/webconfig-local/firmware-manage-token.txt');
    const secrets = [crypto.randomBytes(32), crypto.randomBytes(32)].map(bytes => `stsvc_${bytes.toString('base64url')}`);
    for (const secret of secrets) {
      const script = localFirmwareTokenScript(secret);
      const result = spawnSync('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command', script], {
        cwd: root, env: { ...process.env, PYTHONPATH: path.join(repo, 'tools') }, encoding: 'utf8', timeout: 20000,
      });
      assert.equal(result.status, 0, result.stderr);
      assert.equal(fs.readFileSync(tokenPath, 'utf8').trim(), secret);
      assert.ok(secrets.every(value => !`${result.stdout}${result.stderr}`.includes(value)));
      assert.deepEqual(fs.readdirSync(path.dirname(tokenPath)), ['firmware-manage-token.txt']);
      assert.deepEqual(fs.readdirSync(path.join(root, '.hbox')), ['webconfig-local']);
    }
    const outside = fs.mkdtempSync(path.join(root, 'outside-'));
    const refused = spawnSync('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command', localFirmwareTokenScript(secrets[1])], {
      cwd: outside, encoding: 'utf8', timeout: 10000,
    });
    assert.notEqual(refused.status, 0);
    assert.equal(fs.existsSync(path.join(outside, '.hbox')), false);
  } finally { fs.rmSync(root, { recursive: true, force: true }); }
});
