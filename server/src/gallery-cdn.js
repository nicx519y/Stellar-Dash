'use strict';

// Only published official assets may enter this cache namespace. Personal
// galleries and administrator previews always retain their authenticated URLs.
const PUBLIC_GALLERY_PREFIX = '/cdn/gallery';

function parseGalleryCdnOrigin(value) {
    if (value === undefined || value === '') return '';
    if (typeof value !== 'string' ||
        !/^https:\/\/[a-z0-9](?:[a-z0-9.-]*[a-z0-9])?$/.test(value)) {
        throw new TypeError('GALLERY_CDN_ORIGIN must be an exact HTTPS origin');
    }
    const url = new URL(value);
    if (url.origin !== value || !url.hostname.includes('.') ||
        url.hostname.split('.').some(label => !/^[a-z0-9](?:[a-z0-9-]*[a-z0-9])?$/.test(label))) {
        throw new TypeError('GALLERY_CDN_ORIGIN must be an exact HTTPS origin');
    }
    return value;
}

function galleryAssetVersion(item) {
    return `${item.updatedAt.toString(36)}-${item.deviceSha256.slice(0, 16)}`;
}

function galleryAssetUrls(item, origin = '') {
    const base = origin && item.scope === 'system' && item.published
        ? `${origin}${PUBLIC_GALLERY_PREFIX}/${encodeURIComponent(item.id)}/${galleryAssetVersion(item)}`
        : `/api/gallery/images/${encodeURIComponent(item.id)}`;
    return {
        sourceUrl: `${base}/source`,
        previewUrl: `${base}/preview`,
        deviceAssetUrl: `${base}/device`,
    };
}

function initPublicGalleryAssetRoutes(app, { store, storage }) {
    app.use(PUBLIC_GALLERY_PREFIX, (_req, res, next) => {
        // Failures, missing files and malformed paths must never enter a cache.
        res.set('Cache-Control', 'no-store');
        next();
    });
    app.get(`${PUBLIC_GALLERY_PREFIX}/:id/:version/:variant`, (req, res, next) => {
        const item = store.find(req.params.id);
        const keys = { source: 'sourceKey', preview: 'previewKey', device: 'deviceKey' };
        const key = Object.hasOwn(keys, req.params.variant) ? keys[req.params.variant] : null;
        if (!item || item.scope !== 'system' || !item.published || !key ||
            req.params.version !== galleryAssetVersion(item)) {
            return res.status(404).json({ success: false, error: 'GALLERY_IMAGE_NOT_FOUND' });
        }
        res.set({
            'Access-Control-Allow-Origin': '*',
            'Cross-Origin-Resource-Policy': 'cross-origin',
        });
        // sendFile sets these headers only after it has found the actual file.
        res.sendFile(storage.resolve(item[key]), {
            headers: { 'Cache-Control': 'public, max-age=300' },
        }, error => {
            if (error) {
                if (res.headersSent) return next(error);
                res.set('Cache-Control', 'no-store');
                next(error);
            }
        });
    });
    app.use(PUBLIC_GALLERY_PREFIX, (_req, res) => {
        res.status(404).json({ success: false, error: 'GALLERY_IMAGE_NOT_FOUND' });
    });
}

module.exports = {
    parseGalleryCdnOrigin,
    galleryAssetVersion,
    galleryAssetUrls,
    initPublicGalleryAssetRoutes,
};
