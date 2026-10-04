'use strict';

const { execFileSync } = require('node:child_process');
const path = require('node:path');
const fs = require('node:fs');
const crypto = require('node:crypto');

const SOURCE_PATHS = ['application/Src', 'application/Inc', 'RF_PHY_Hop/TX', 'RF_PHY_Hop/Common/include', 'common'];
const SOURCE_FILE = /\.(?:c|cpp|h|hpp|inc|s|S)$/;
const VERSION_TAG = /^(?:xora[-_]?v?|v)(\d+)\.(\d+)\.(\d+)$/i;
const TOPICS = [
    { name: 'firmware', pattern: /^(?:application\/(?:Src|Inc)\/firmware\/|common\/(?:firmware|release_install|ch585_iap|ch585_staging))/, note: '固件更新与恢复流程得到进一步完善。' },
    { name: 'input', pattern: /^application\/(?:Src|Inc)\/input\//, note: '按键与输入相关体验持续打磨。' },
    { name: 'wireless', pattern: /^(?:RF_PHY_Hop\/|application\/(?:Src|Inc)\/transport\/rf\/|common\/rf_)/, note: '无线连接相关细节得到改进。' },
    { name: 'configuration', pattern: /^application\/(?:Src|Inc)\/config\//, note: '设备设置与保存流程更加顺手。' },
    { name: 'display', pattern: /^application\/(?:Src|Inc)\/(?:display|leds)\//, note: '屏幕显示与视觉反馈得到优化。' },
    { name: 'connection', pattern: /^(?:application\/(?:Src|Inc)\/(?:transport|webconfig)\/|common\/(?:webhid|usb_board_link))/, note: '设备连接与配置交互继续得到打磨。' },
    { name: 'device', pattern: /^application\/(?:Src|Inc)\//, note: '设备运行中的使用细节得到完善。' },
    { name: 'compatibility', pattern: /^common\//, note: '设备兼容性相关细节得到完善。' },
];

function git(repoRoot, args, options = {}) {
    try {
        return execFileSync('git', args, {
            cwd: repoRoot,
            timeout: 10000,
            maxBuffer: 4 * 1024 * 1024,
            stdio: ['ignore', 'pipe', 'pipe'],
            ...options,
        });
    } catch (error) {
        throw new Error(`Git check failed: ${args[0]} ${args[1] || ''} (${String(error.stderr || error.message).trim()})`);
    }
}

function commitOf(repoRoot, ref) {
    if (typeof ref !== 'string' || !/^[A-Za-z0-9._/~^-]+$/.test(ref) || ref.startsWith('-')) {
        throw new Error('Invalid previous Git ref');
    }
    return String(git(repoRoot, ['rev-parse', '--verify', `${ref}^{commit}`])).trim();
}

function versionTuple(version) {
    const match = /^(\d+)\.(\d+)\.(\d+)$/.exec(version);
    if (!match) throw new Error('Invalid release version');
    return match.slice(1).map(Number);
}

function compareVersions(left, right) {
    const a = versionTuple(left), b = versionTuple(right);
    for (let i = 0; i < 3; i++) if (a[i] !== b[i]) return a[i] - b[i];
    return 0;
}

function previousReleaseRef(repoRoot, version, specifiedRef) {
    if (specifiedRef) return specifiedRef;
    const tags = String(git(repoRoot, ['tag', '--merged', 'HEAD'])).split(/\r?\n/);
    const candidates = tags.flatMap(tag => {
        const match = VERSION_TAG.exec(tag);
        if (!match) return [];
        const tagVersion = match.slice(1).join('.');
        return compareVersions(tagVersion, version) < 0 ? [{ tag, version: tagVersion }] : [];
    });
    candidates.sort((a, b) => compareVersions(b.version, a.version));
    if (!candidates.length) {
        throw new Error('No previous XORA version tag exists; provide --since <previous release commit or tag>');
    }
    return candidates[0].tag;
}

function sourceRevision(repoRoot, allowWorktree = false) {
    const root = path.resolve(repoRoot);
    const actualRoot = String(git(root, ['rev-parse', '--show-toplevel'])).trim();
    if (path.resolve(actualRoot).toLowerCase() !== root.toLowerCase()) {
        throw new Error('Git notes must use the repository root');
    }
    const head = commitOf(root, 'HEAD');
    const dirty = git(root, ['status', '--porcelain', '-z', '--untracked-files=all', '--', ...SOURCE_PATHS]);
    if (dirty.length && !allowWorktree) throw new Error('Commit device firmware/TX source changes before generating release notes');
    return { root, head, dirty: Boolean(dirty.length) };
}

function initialReleaseNotes({ repoRoot, version, allowWorktree = false }) {
    const { head, dirty } = sourceRevision(repoRoot, allowWorktree);
    const notes = `XORA ${version} 初版发布\n\n欢迎体验 XORA 的首个固件版本。这个版本为日常使用打下基础，带来设备输入、设置与后续更新所需的核心能力。\n\n感谢你与 XORA 一起迈出第一步。我们会继续倾听反馈，认真打磨每一次使用体验。`;
    return { notes, evidence: { kind: 'initial-release', currentCommit: head, dirty } };
}

function gitReleaseNotes({ repoRoot, version, since, allowWorktree = false }) {
    const { root, head, dirty } = sourceRevision(repoRoot, allowWorktree);
    const previousRef = previousReleaseRef(root, version, since);
    const previousCommit = commitOf(root, previousRef);
    if (previousCommit === head && !allowWorktree) throw new Error('Previous release ref is the current commit');
    try {
        execFileSync('git', ['merge-base', '--is-ancestor', previousCommit, head],
            { cwd: root, timeout: 10000, stdio: 'ignore' });
    } catch {
        throw new Error('Previous release ref must be an ancestor of HEAD');
    }
    const output = git(root, ['diff', '--name-only', '-z', '--diff-filter=ACDMRT', previousCommit, ...(allowWorktree ? [] : [head]), '--', ...SOURCE_PATHS]);
    const extras = allowWorktree ? git(root, ['ls-files', '--others', '--exclude-standard', '-z', '--', ...SOURCE_PATHS]).toString('utf8').split('\0') : [];
    const files = [...new Set([...output.toString('utf8').split('\0'), ...extras])].filter(name => SOURCE_FILE.test(name) && name !== 'common/release_build_identity.h');
    if (!files.length) throw new Error('No device firmware or TX source changes since the previous release');

    return summary(version, files, {kind: allowWorktree ? 'git-worktree' : 'git-diff', previousRef, previousCommit, currentCommit: head, dirty});
}

function summary(version, files, evidence) {
    const counts = new Map();
    for (const file of files) {
        const topic = TOPICS.find(candidate => candidate.pattern.test(file));
        if (topic) counts.set(topic.name, (counts.get(topic.name) || 0) + 1);
    }
    const selected = TOPICS.filter(topic => counts.has(topic.name))
        .sort((a, b) => counts.get(b.name) - counts.get(a.name)).slice(0, 3);
    const body = selected.length ? selected.map(topic => `• ${topic.note}`).join('\n')
        : '• 延续当前版本的使用体验，方便进行升级流程测试。';
    const notes = `XORA ${version} 更新说明\n\n这次更新，我们继续打磨日常使用体验：\n${body}\n\n感谢你使用 XORA。`;
    return { notes, evidence: {...evidence, changedSourceFiles: files, topics: selected.map(t => t.name)} };
}

function sourceHashes(root) {
    const files = git(root, ['ls-files', '--cached', '--others', '--exclude-standard', '-z', '--', ...SOURCE_PATHS])
        .toString('utf8').split('\0');
    return Object.fromEntries([...new Set(files)].filter(name => SOURCE_FILE.test(name) && name !== 'common/release_build_identity.h')
        .sort().flatMap(name => {
            const file = path.join(root, name);
            return fs.existsSync(file) ? [[name, crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex')]] : [];
        }));
}

// Local packages retain Git evidence and per-file hashes, including uncommitted
// source. A later package can compare the actual previous working tree snapshot.
function localReleaseNotes({ repoRoot, version, since, historyRoot, initialRelease = false }) {
    const {root, head, dirty} = sourceRevision(repoRoot, true);
    const hashes = sourceHashes(root);
    let result;
    if (initialRelease) result = initialReleaseNotes({repoRoot: root, version, allowWorktree: true});
    else if (since) result = gitReleaseNotes({repoRoot: root, version, since, allowWorktree: true});
    else {
        const candidates = [];
        for (const folder of fs.existsSync(historyRoot) ? fs.readdirSync(historyRoot, {withFileTypes: true}) : []) {
            if (!folder.isDirectory()) continue;
            const match = /^XORA-(\d+\.\d+\.\d+)-/.exec(folder.name);
            if (!match || compareVersions(match[1], version) >= 0) continue;
            const base = path.join(historyRoot, folder.name, 'package', `XORA-${match[1]}`);
            try {
                if (!fs.existsSync(`${base}-release.zip`)) continue;
                const data = JSON.parse(fs.readFileSync(`${base}-release-notes-source.json`, 'utf8'));
                if (/^[a-f0-9]{40}$/.test(data.currentCommit || '')) candidates.push({version: match[1], data, folder: folder.name});
            } catch { /* Legacy packaging-only outputs may not have Git evidence. */ }
        }
        candidates.sort((a,b) => compareVersions(b.version,a.version) || b.folder.localeCompare(a.folder));
        const previous = candidates[0];
        if (previous?.data.sourceHashes && typeof previous.data.sourceHashes === 'object') {
            const old = previous.data.sourceHashes;
            const files = [...new Set([...Object.keys(old), ...Object.keys(hashes)])].filter(name => old[name] !== hashes[name]).sort();
            result = summary(version, files, {kind:'local-source-diff', currentCommit:head, dirty,
                previousCommit:previous.data.currentCommit, baselineVersion:previous.version});
        } else {
            result = gitReleaseNotes({repoRoot:root, version, since:previous?.data.currentCommit, allowWorktree:true});
            if (previous) result.evidence.baselineVersion = previous.version;
        }
    }
    result.evidence.sourceHashes = hashes;
    return result;
}

module.exports = { gitReleaseNotes, initialReleaseNotes, previousReleaseRef, localReleaseNotes };
