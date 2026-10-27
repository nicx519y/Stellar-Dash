'use strict';

const fs = require('node:fs');
const path = require('node:path');

// Version manifests are installed by the deployment tool, outside current/.
// Only explicitly published build files can be retrieved from this namespace.
function installWebAssetRoutes(app, assetDir) {
    if (!assetDir) return;
    const root = path.resolve(assetDir);
    const manifests = new Map();
    app.use('/cdn/web', (_req, res, next) => {
        res.set('Cache-Control', 'no-store');
        next();
    });
    app.get('/cdn/web/:version/*', (req, res, next) => {
        const { version } = req.params;
        const relative = req.params[0];
        const missing = () => res.status(404).json({ success: false, error: 'WEB_ASSET_NOT_FOUND' });
        if (!/^[a-f0-9]{32}$/.test(version) || !/^[A-Za-z0-9_./@-]+$/.test(relative)
            || relative.split('/').some(part => !part || part === '.' || part === '..')) return missing();
        let manifest = manifests.get(version);
        if (!manifest) {
            const manifestPath = path.join(root, version, 'manifest.json');
            try {
                manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
                if (manifest.schema !== 1 || manifest.version !== version || !manifest.files
                    || typeof manifest.files !== 'object' || Array.isArray(manifest.files)) throw new Error('Invalid asset manifest');
                manifests.set(version, manifest);
            } catch (error) {
                if (error.code === 'ENOENT') return missing();
                return next(error);
            }
        }
        if (!Object.hasOwn(manifest.files, relative)) return missing();
        res.set({ 'Access-Control-Allow-Origin': '*', 'Cross-Origin-Resource-Policy': 'cross-origin',
                  'Timing-Allow-Origin': '*' });
        res.sendFile(path.join(root, version, 'files', relative), {
            headers: { 'Cache-Control': 'public, max-age=31536000, immutable' },
        }, error => {
            if (!error) return;
            if (!res.headersSent) {
                res.set('Cache-Control', 'no-store');
                if (error.status === 404 || error.code === 'ENOENT') return missing();
            }
            next(error);
        });
    });
    app.use('/cdn/web', (_req, res) => res.status(404).json({ success: false, error: 'WEB_ASSET_NOT_FOUND' }));
}

module.exports = { installWebAssetRoutes };
