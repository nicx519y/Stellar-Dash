#!/usr/bin/env node
'use strict';

// Package an already-built release, write public notes, and import a draft.
// This command never builds, flashes, provisions, or publishes firmware.
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const { createBundle } = require('./create-firmware-bundle');
const { gitReleaseNotes, initialReleaseNotes } = require('./git-release-notes');

const TOKEN_PATTERN = /^stsvc_[A-Za-z0-9_-]{43}$/;
const VERSION_PATTERN = /^(0|[1-9]\d{0,4})\.(0|[1-9]\d{0,4})\.(0|[1-9]\d{0,4})$/;

function usage() {
    return [
        'Usage (PowerShell):',
        '  node server/scripts/create-firmware-draft.js `',
        '    --source <existing-v2-release-source.json> `',
        '    --signing-key <matching-release-private-key.pem> `',
        '    --out-dir <new-output-directory> `',
        '    --service-token-file <firmware.manage-token-file> `',
        '    --initial-release',
        '',
        'Values in angle brackets are required existing local files/directories, not literal paths.',
        'The default admin service is http://localhost:3001; use --server for another loopback port.',
        'Remote uploads are disabled.',
        'The source manifest and signed STM32 A/B and TX artifacts must be prepared first.',
        'For later versions, omit --initial-release to compare with the latest version tag,',
        'or pass --since <previous release Git ref>. Device sources must be committed.',
        'Add --dry-run to create and validate local files without uploading.',
        'Uploads remain drafts. Acceptance evidence and publishing are done in admin.',
    ].join('\n');
}

function parseArguments(argv) {
    const options = { initialRelease: false, dryRun: false };
    const names = new Map([
        ['--source', 'source'], ['--signing-key', 'signingKey'],
        ['--out-dir', 'outDir'], ['--server', 'server'],
        ['--service-token-file', 'serviceTokenFile'],
        ['--since', 'since'], ['--git-repo', 'gitRepo'],
    ]);
    for (let index = 0; index < argv.length; index += 1) {
        const argument = argv[index];
        if (argument === '--help') return { help: true };
        if (argument === '--dry-run') {
            if (options.dryRun) throw new Error('--dry-run was provided twice');
            options.dryRun = true;
            continue;
        }
        if (argument === '--initial-release') {
            if (options.initialRelease) throw new Error('--initial-release was provided twice');
            options.initialRelease = true;
            continue;
        }
        if (names.has(argument)) {
            const value = argv[++index];
            if (!value || value.startsWith('--')) throw new Error(`${argument} requires a value`);
            const name = names.get(argument);
            if (options[name]) throw new Error(`${argument} was provided twice`);
            options[name] = value;
            continue;
        }
        throw new Error(`Unknown argument: ${argument}`);
    }
    for (const name of ['source', 'signingKey', 'outDir']) {
        if (!options[name]) throw new Error(`Missing --${name === 'signingKey' ? 'signing-key' : name === 'outDir' ? 'out-dir' : name}`);
    }
    if (!options.server) options.server = 'http://localhost:3001';
    if (!options.dryRun && !options.serviceTokenFile) {
        throw new Error('Uploading to local admin requires --service-token-file');
    }
    if (options.initialRelease && options.since) throw new Error('--initial-release cannot be used with --since');
    return options;
}

function serverOrigin(input) {
    const url = new URL(input);
    const loopback = ['localhost', '127.0.0.1', '[::1]'].includes(url.hostname);
    if (!loopback) throw new Error('--server must be a local loopback admin service (for example http://localhost:3001); remote uploads are disabled');
    if (!['http:', 'https:'].includes(url.protocol)) throw new Error('--server must use HTTP or HTTPS');
    if (url.username || url.password || url.pathname !== '/' || url.search || url.hash) {
        throw new Error('--server must be an origin without credentials, path, query or fragment');
    }
    return url.origin;
}

function requireExistingFile(file, option, purpose) {
    if (!file || /^(?:path[\\/]to[\\/]|<[^>]+>$)/i.test(file)) {
        throw new Error(`${option} is an example path. Provide an existing ${purpose} file; see docs/firmware-release-catalog.md`);
    }
    const resolved = path.resolve(file);
    let stat;
    try { stat = fs.statSync(resolved); }
    catch (error) {
        if (error.code === 'ENOENT') throw new Error(`${option} file does not exist: ${resolved}. Prepare ${purpose} first; see docs/firmware-release-catalog.md`);
        throw error;
    }
    if (!stat.isFile()) throw new Error(`${option} must name a file: ${resolved}`);
    return resolved;
}

function readServiceToken(file) {
    const token = fs.readFileSync(file, 'utf8').trim();
    if (!TOKEN_PATTERN.test(token)) throw new Error('Invalid firmware.manage service token file');
    return token;
}

async function requestJson(url, token, init = {}, timeoutMs = 15000) {
    const response = await fetch(url, {
        ...init,
        redirect: 'manual',
        cache: 'no-store',
        signal: AbortSignal.timeout(timeoutMs),
        headers: { Authorization: `Bearer ${token}`, ...init.headers },
    });
    const body = await response.json().catch(() => ({}));
    if (!response.ok || body.success !== true) {
        throw new Error(`${body.message || body.error || 'Administrator request failed'} (HTTP ${response.status})`);
    }
    return body.data;
}

async function createFirmwareDraft(options) {
    const origin = options.dryRun ? null : serverOrigin(options.server || 'http://localhost:3001');
    const source = requireExistingFile(options.source, '--source', 'v2 release-source.json');
    const signingKey = requireExistingFile(options.signingKey, '--signing-key', 'matching release private key');
    const manifest = JSON.parse(fs.readFileSync(source, 'utf8'));
    if (manifest.schemaVersion !== 2 || !VERSION_PATTERN.test(manifest.version || '')) {
        throw new Error('A signed v2 release source with a valid version is required');
    }
    if (options.initialRelease && options.since) throw new Error('--initial-release cannot be used with --since');
    const repoRoot = path.resolve(options.gitRepo || path.join(__dirname, '../..'));
    const generated = options.initialRelease
        ? initialReleaseNotes({ repoRoot, version: manifest.version })
        : gitReleaseNotes({ repoRoot, version: manifest.version, since: options.since });
    const { notes, evidence } = generated;
    const token = options.dryRun ? null : readServiceToken(requireExistingFile(options.serviceTokenFile,
        '--service-token-file', 'firmware.manage service token'));
    const outDir = path.resolve(options.outDir);
    fs.mkdirSync(outDir, { recursive: true });
    const bundlePath = path.join(outDir, `XORA-${manifest.version}-release.zip`);
    const notesPath = path.join(outDir, `XORA-${manifest.version}-release-notes.md`);
    const evidencePath = path.join(outDir, `XORA-${manifest.version}-release-notes-source.json`);
    if (fs.existsSync(bundlePath) || fs.existsSync(notesPath) || fs.existsSync(evidencePath)) {
        throw new Error('Output already exists; choose another directory or review the existing draft');
    }

    createBundle(source, signingKey, bundlePath);
    fs.writeFileSync(notesPath, `${notes}\n`, { flag: 'wx' });
    fs.writeFileSync(evidencePath, `${JSON.stringify(evidence, null, 2)}\n`, { flag: 'wx' });
    const bundle = fs.readFileSync(bundlePath);
    const bundleSha256 = crypto.createHash('sha256').update(bundle).digest('hex');
    const result = { bundlePath, notesPath, evidencePath, bundleSha256, version: manifest.version, releaseId: null, adminUrl: null };
    if (options.dryRun) return result;

    const form = new FormData();
    form.append('bundle', new Blob([bundle], { type: 'application/zip' }), path.basename(bundlePath));
    let importResult;
    try {
        importResult = await requestJson(`${origin}/api/admin/firmware/imports`, token,
            { method: 'POST', body: form }, 120000);
    } catch (error) {
        throw new Error(`Upload result is uncertain; check admin for XORA ${manifest.version} before retrying. ${error.message}`);
    }
    if (importResult.status !== 'completed' || !importResult.releaseId) {
        throw new Error(`Server rejected the package: ${importResult.error || importResult.status || 'unknown error'}`);
    }
    result.releaseId = importResult.releaseId;
    result.adminUrl = `${origin}/admin/firmware/`;
    const detailUrl = `${origin}/api/admin/firmware/releases/${encodeURIComponent(result.releaseId)}`;
    try {
        const draft = await requestJson(detailUrl, token);
        if (draft.status !== 'draft' || draft.bundleSha256 !== bundleSha256) {
            throw new Error('Imported draft state or digest differs from the local package');
        }
        const saved = await requestJson(detailUrl, token, {
            method: 'PATCH',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ revision: draft.revision, notes, acceptance: '' }),
        });
        if (saved.status !== 'draft' || saved.notes !== notes || saved.bundleSha256 !== bundleSha256) {
            throw new Error('The server did not confirm the update notes on the draft');
        }
    } catch (error) {
        throw new Error(`Package imported as draft ${result.releaseId}, but notes need checking in admin: ${error.message}`);
    }
    return result;
}

if (require.main === module) {
    (async () => {
        const options = parseArguments(process.argv.slice(2));
        if (options.help) { console.log(usage()); return; }
        const result = await createFirmwareDraft(options);
        console.log(`XORA ${result.version}: signed package ${result.bundlePath}`);
        console.log(`Release notes: ${result.notesPath}`);
        console.log(`Git source: ${result.evidencePath}`);
        console.log(`Bundle SHA-256: ${result.bundleSha256}`);
        if (result.releaseId) {
            console.log(`Admin draft: ${result.releaseId}`);
            console.log(`Preview, edit, record acceptance and publish: ${result.adminUrl}`);
        } else console.log('Dry run complete; nothing was uploaded.');
    })().catch(error => { console.error(`Release draft failed: ${error.message}`); process.exitCode = 1; });
}

module.exports = { createFirmwareDraft, parseArguments, serverOrigin };
