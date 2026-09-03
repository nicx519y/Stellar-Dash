'use strict';

const crypto = require('crypto');
const fs = require('fs-extra');
const path = require('path');
const Database = require('better-sqlite3');
const multer = require('multer');

const USER_GALLERY_LIMIT = 10;
const MAX_SOURCE_BYTES = 20 * 1024 * 1024;
const MAX_PREVIEW_BYTES = 2 * 1024 * 1024;
const MAX_DEVICE_BYTES = 4096 + 320 * 172 * 2 * 6;
const SOURCE_TYPES = new Map([
    ['image/png', '.png'],
    ['image/jpeg', '.jpg'],
    ['image/gif', '.gif'],
]);
const PREVIEW_TYPES = new Map([
    ['image/png', '.png'],
    ['image/jpeg', '.jpg'],
    ['image/webp', '.webp'],
]);

class ImageGalleryError extends Error {
    constructor(code, message, status = 400) {
        super(message);
        this.name = 'ImageGalleryError';
        this.code = code;
        this.status = status;
    }
}

function crc32(buffer) {
    let value = 0xffffffff;
    for (const byte of buffer) {
        value ^= byte;
        for (let bit = 0; bit < 8; bit += 1) {
            value = (value >>> 1) ^ ((value & 1) ? 0xedb88320 : 0);
        }
    }
    return (value ^ 0xffffffff) >>> 0;
}

function sha256(buffer) {
    return crypto.createHash('sha256').update(buffer).digest('hex');
}

function assertImageBytes(file, types, maximum, label) {
    if (!file || !Buffer.isBuffer(file.buffer)) {
        throw new ImageGalleryError('GALLERY_FILE_REQUIRED', `${label} is required.`);
    }
    const mimeType = String(file.mimetype || '').toLowerCase();
    if (!types.has(mimeType) || file.buffer.length === 0 || file.buffer.length > maximum) {
        throw new ImageGalleryError('GALLERY_FILE_INVALID', `${label} has an unsupported type or size.`);
    }
    const data = file.buffer;
    const signatureOk =
        (mimeType === 'image/png' && data.length >= 8 && data.subarray(0, 8).equals(Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]))) ||
        (mimeType === 'image/jpeg' && data.length >= 3 && data[0] === 0xff && data[1] === 0xd8 && data[2] === 0xff) ||
        (mimeType === 'image/gif' && data.length >= 6 && ['GIF87a', 'GIF89a'].includes(data.subarray(0, 6).toString('ascii'))) ||
        (mimeType === 'image/webp' && data.length >= 12 && data.subarray(0, 4).toString('ascii') === 'RIFF' && data.subarray(8, 12).toString('ascii') === 'WEBP');
    if (!signatureOk) {
        throw new ImageGalleryError('GALLERY_FILE_INVALID', `${label} bytes do not match the declared media type.`);
    }
    return { data, mimeType, extension: types.get(mimeType) };
}

function parseUimgV3(buffer) {
    if (!Buffer.isBuffer(buffer) || buffer.length < 4096 || buffer.length > MAX_DEVICE_BYTES) {
        throw new ImageGalleryError('GALLERY_UIMG_INVALID', 'Device image has an invalid size.');
    }
    const magic = buffer.readUInt32LE(0);
    const version = buffer.readUInt16LE(4);
    const valid = buffer.readUInt8(6);
    const format = buffer.readUInt8(7);
    const width = buffer.readUInt16LE(8);
    const height = buffer.readUInt16LE(10);
    const frameCount = buffer.readUInt8(12);
    const fps = buffer.readUInt8(13);
    const reserved = buffer.readUInt16LE(14);
    const frameSize = buffer.readUInt32LE(16);
    const framesOffset = buffer.readUInt32LE(20);
    const payloadBytes = buffer.readUInt32LE(24);
    const expectedId = Buffer.alloc(16);
    expectedId.write('USER_IMAGE', 'ascii');
    const payloadCrc32 = buffer.readUInt32LE(84);
    const headerCrc32 = buffer.readUInt32LE(88);
    const expectedFrameSize = width * height * 2;
    const sequence = frameCount > 1;
    if (magic !== 0x474d4955 || version !== 3 || valid !== 1 || reserved !== 0 ||
        !buffer.subarray(68, 84).equals(expectedId) || width !== 320 || height !== 172 ||
        frameCount < 1 || frameCount > 6 ||
        format !== (sequence ? 2 : 1) || (sequence ? fps !== 3 : fps !== 0) ||
        frameSize !== expectedFrameSize || framesOffset !== 4096 ||
        payloadBytes !== expectedFrameSize * frameCount || buffer.length !== framesOffset + payloadBytes) {
        throw new ImageGalleryError('GALLERY_UIMG_INVALID', 'Device image metadata is invalid.');
    }
    for (let index = 0; index < 10; index += 1) {
        const expected = index < frameCount ? 4096 + index * frameSize : 0;
        if (buffer.readUInt32LE(28 + index * 4) !== expected) {
            throw new ImageGalleryError('GALLERY_UIMG_INVALID', 'Device image frame offsets are invalid.');
        }
    }
    if (crc32(buffer.subarray(0, 88)) !== headerCrc32 ||
        crc32(buffer.subarray(framesOffset)) !== payloadCrc32) {
        throw new ImageGalleryError('GALLERY_UIMG_INVALID', 'Device image CRC32 is invalid.');
    }
    return { width, height, frameCount, fps, payloadBytes, payloadCrc32 };
}

function normalizeTitle(value, fallback = 'Image') {
    const title = String(value || fallback).trim();
    if (!title || title.length > 120) {
        throw new ImageGalleryError('GALLERY_TITLE_INVALID', 'Image title must contain 1-120 characters.');
    }
    return title;
}

function normalizeSortOrder(value, fallback = 0) {
    const result = value === undefined ? fallback : Number(value);
    if (!Number.isInteger(result) || Math.abs(result) > 0x7fffffff) {
        throw new ImageGalleryError('GALLERY_SORT_INVALID', 'sortOrder must be an integer.');
    }
    return result;
}

class GalleryStorage {
    putSet() { throw new Error('GalleryStorage.putSet is not implemented'); }
    removeSet() { throw new Error('GalleryStorage.removeSet is not implemented'); }
    resolve() { throw new Error('GalleryStorage.resolve is not implemented'); }
}

class LocalGalleryStorage extends GalleryStorage {
    constructor(options) {
        super();
        if (!options || typeof options.root !== 'string') throw new TypeError('gallery storage root is required');
        this.root = path.resolve(options.root);
        fs.ensureDirSync(this.root);
    }

    resolve(key) {
        if (!/^[a-f0-9-]{36}\/(source\.(png|jpg|gif)|preview\.(png|jpg|webp)|device\.uimg)$/.test(key)) {
            throw new ImageGalleryError('GALLERY_STORAGE_KEY_INVALID', 'Invalid gallery storage key.', 404);
        }
        const result = path.resolve(this.root, key);
        if (path.dirname(path.dirname(result)) !== this.root) {
            throw new ImageGalleryError('GALLERY_STORAGE_KEY_INVALID', 'Invalid gallery storage key.', 404);
        }
        return result;
    }

    putSet(id, files) {
        const destination = path.resolve(this.root, id);
        const temporary = path.resolve(this.root, `.${id}.${crypto.randomBytes(6).toString('hex')}.tmp`);
        fs.ensureDirSync(temporary);
        try {
            for (const [name, value] of Object.entries(files)) {
                fs.writeFileSync(path.join(temporary, name), value);
            }
            fs.renameSync(temporary, destination);
        } catch (error) {
            fs.removeSync(temporary);
            throw error;
        }
    }

    removeSet(id) {
        fs.removeSync(path.resolve(this.root, id));
    }
}

class ImageGalleryStore {
    constructor(options) {
        if (!options || typeof options.databasePath !== 'string') throw new TypeError('gallery database path is required');
        this.databasePath = path.resolve(options.databasePath);
        this.limit = Number.isSafeInteger(options.userLimit) && options.userLimit > 0
            ? options.userLimit
            : USER_GALLERY_LIMIT;
        fs.ensureDirSync(path.dirname(this.databasePath));
        this.database = new Database(this.databasePath);
        this.database.pragma('journal_mode = WAL');
        this.database.pragma('foreign_keys = ON');
        this.database.pragma('busy_timeout = 5000');
        this.migrate();
    }

    migrate() {
        this.database.exec(`
            CREATE TABLE IF NOT EXISTS galleries (
                id TEXT PRIMARY KEY,
                kind TEXT NOT NULL CHECK(kind IN ('system','user')),
                owner_uid TEXT,
                image_limit INTEGER,
                created_at INTEGER NOT NULL,
                UNIQUE(kind, owner_uid)
            );
            CREATE TABLE IF NOT EXISTS gallery_images (
                id TEXT PRIMARY KEY,
                gallery_id TEXT NOT NULL,
                owner_uid TEXT,
                title TEXT NOT NULL,
                source_key TEXT NOT NULL,
                preview_key TEXT NOT NULL,
                device_key TEXT NOT NULL,
                source_mime TEXT NOT NULL,
                width INTEGER NOT NULL,
                height INTEGER NOT NULL,
                frame_count INTEGER NOT NULL,
                fps INTEGER NOT NULL,
                payload_bytes INTEGER NOT NULL,
                payload_crc32 INTEGER NOT NULL,
                device_sha256 TEXT NOT NULL,
                sort_order INTEGER NOT NULL DEFAULT 0,
                published INTEGER NOT NULL DEFAULT 0,
                created_at INTEGER NOT NULL,
                updated_at INTEGER NOT NULL,
                FOREIGN KEY(gallery_id) REFERENCES galleries(id) ON DELETE CASCADE
            );
            CREATE INDEX IF NOT EXISTS gallery_images_list_idx
                ON gallery_images(gallery_id, published, sort_order, created_at, id);
            CREATE INDEX IF NOT EXISTS gallery_images_fingerprint_idx
                ON gallery_images(width, height, frame_count, fps, payload_bytes, payload_crc32, published);
        `);
        this.database.prepare(`
            INSERT OR IGNORE INTO galleries(id,kind,owner_uid,image_limit,created_at)
            VALUES('system','system',NULL,NULL,?)
        `).run(Date.now());
    }

    close() {
        this.database.close();
    }

    ensureUserGallery(uid) {
        const id = `user:${uid}`;
        this.database.prepare(`
            INSERT OR IGNORE INTO galleries(id,kind,owner_uid,image_limit,created_at)
            VALUES(?, 'user', ?, ?, ?)
        `).run(id, uid, this.limit, Date.now());
        return id;
    }

    decode(row) {
        if (!row) return null;
        return {
            id: row.id,
            scope: row.gallery_id === 'system' ? 'system' : 'user',
            title: row.title,
            sourceMime: row.source_mime,
            width: row.width,
            height: row.height,
            frameCount: row.frame_count,
            fps: row.fps,
            payloadBytes: row.payload_bytes,
            payloadCrc32: row.payload_crc32 >>> 0,
            deviceSha256: row.device_sha256,
            sortOrder: row.sort_order,
            published: Boolean(row.published),
            createdAt: row.created_at,
            updatedAt: row.updated_at,
        };
    }

    listSystem({ offset = 0, limit = 30, includeDrafts = false } = {}) {
        const where = includeDrafts ? '' : 'AND published=1';
        const rows = this.database.prepare(`
            SELECT * FROM gallery_images WHERE gallery_id='system' ${where}
            ORDER BY sort_order ASC, created_at DESC, id ASC LIMIT ? OFFSET ?
        `).all(limit + 1, offset);
        return {
            items: rows.slice(0, limit).map(row => this.decode(row)),
            nextOffset: rows.length > limit ? offset + limit : null,
        };
    }

    listMine(uid) {
        const galleryId = this.ensureUserGallery(uid);
        const rows = this.database.prepare(`
            SELECT * FROM gallery_images WHERE gallery_id=? ORDER BY created_at DESC, id ASC
        `).all(galleryId);
        return { limit: this.limit, count: rows.length, items: rows.map(row => this.decode(row)) };
    }

    find(id) {
        const row = this.database.prepare('SELECT * FROM gallery_images WHERE id=?').get(id);
        return row ? { ...this.decode(row), ownerUid: row.owner_uid, sourceKey: row.source_key, previewKey: row.preview_key, deviceKey: row.device_key } : null;
    }

    findByFingerprint(fingerprint, ownerUid = null) {
        const values = [
            fingerprint.width,
            fingerprint.height,
            fingerprint.frameCount,
            fingerprint.fps,
            fingerprint.payloadBytes,
            fingerprint.payloadCrc32,
        ];
        const row = ownerUid
            ? this.database.prepare(`
                SELECT * FROM gallery_images
                WHERE width=? AND height=? AND frame_count=? AND fps=?
                  AND payload_bytes=? AND payload_crc32=?
                  AND ((gallery_id='system' AND published=1) OR owner_uid=?)
                ORDER BY CASE WHEN owner_uid=? THEN 0 ELSE 1 END,
                         sort_order ASC, created_at DESC, id ASC
                LIMIT 1
            `).get(...values, ownerUid, ownerUid)
            : this.database.prepare(`
                SELECT * FROM gallery_images
                WHERE width=? AND height=? AND frame_count=? AND fps=?
                  AND payload_bytes=? AND payload_crc32=?
                  AND gallery_id='system' AND published=1
                ORDER BY sort_order ASC, created_at DESC, id ASC
                LIMIT 1
            `).get(...values);
        return row ? { ...this.decode(row), ownerUid: row.owner_uid, sourceKey: row.source_key, previewKey: row.preview_key, deviceKey: row.device_key } : null;
    }

    create(input) {
        const create = this.database.transaction(() => {
            const galleryId = input.scope === 'system' ? 'system' : this.ensureUserGallery(input.ownerUid);
            if (input.scope === 'user') {
                const count = this.database.prepare('SELECT COUNT(*) AS count FROM gallery_images WHERE gallery_id=?').get(galleryId).count;
                if (count >= this.limit) throw new ImageGalleryError('GALLERY_LIMIT_REACHED', `Personal gallery is limited to ${this.limit} images.`, 409);
            }
            const now = Date.now();
            this.database.prepare(`
                INSERT INTO gallery_images(
                    id,gallery_id,owner_uid,title,source_key,preview_key,device_key,source_mime,
                    width,height,frame_count,fps,payload_bytes,payload_crc32,device_sha256,
                    sort_order,published,created_at,updated_at
                ) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)
            `).run(
                input.id, galleryId, input.ownerUid || null, input.title,
                input.sourceKey, input.previewKey, input.deviceKey, input.sourceMime,
                input.width, input.height, input.frameCount, input.fps, input.payloadBytes,
                input.payloadCrc32, input.deviceSha256, input.sortOrder || 0,
                input.scope === 'system' ? Number(Boolean(input.published)) : 1, now, now
            );
        });
        create.immediate();
        return this.find(input.id);
    }

    updateSystem(id, patch) {
        const current = this.find(id);
        if (!current || current.scope !== 'system') throw new ImageGalleryError('GALLERY_IMAGE_NOT_FOUND', 'Image was not found.', 404);
        const title = patch.title === undefined ? current.title : normalizeTitle(patch.title);
        const sortOrder = normalizeSortOrder(patch.sortOrder, current.sortOrder);
        const published = patch.published === undefined ? current.published : Boolean(patch.published);
        this.database.prepare(`UPDATE gallery_images SET title=?,sort_order=?,published=?,updated_at=? WHERE id=? AND gallery_id='system'`)
            .run(title, sortOrder, Number(published), Date.now(), id);
        return this.find(id);
    }

    deleteOwned(ids, ownerUid) {
        const unique = [...new Set(ids)];
        const remove = this.database.transaction(() => {
            const rows = unique.map(id => this.find(id));
            if (rows.some(row => !row || row.scope !== 'user' || row.ownerUid !== ownerUid)) {
                throw new ImageGalleryError('GALLERY_IMAGE_NOT_FOUND', 'One or more images were not found.', 404);
            }
            const statement = this.database.prepare('DELETE FROM gallery_images WHERE id=? AND owner_uid=?');
            rows.forEach(row => statement.run(row.id, ownerUid));
            return rows;
        });
        return remove.immediate();
    }

    deleteSystem(id) {
        const row = this.find(id);
        if (!row || row.scope !== 'system') throw new ImageGalleryError('GALLERY_IMAGE_NOT_FOUND', 'Image was not found.', 404);
        this.database.prepare("DELETE FROM gallery_images WHERE id=? AND gallery_id='system'").run(id);
        return row;
    }
}

function cursorOffset(value) {
    if (!value) return 0;
    try {
        const result = Number(Buffer.from(String(value), 'base64url').toString('ascii'));
        if (!Number.isSafeInteger(result) || result < 0) throw new Error('bad cursor');
        return result;
    } catch {
        throw new ImageGalleryError('GALLERY_CURSOR_INVALID', 'Invalid gallery cursor.');
    }
}

function deviceFingerprint(query) {
    const result = {
        width: Number(query.width),
        height: Number(query.height),
        frameCount: Number(query.frameCount),
        fps: Number(query.fps),
        payloadBytes: Number(query.payloadBytes),
        payloadCrc32: Number(query.payloadCrc32),
    };
    if (!Object.values(result).every(Number.isInteger) ||
        result.width !== 320 || result.height !== 172 ||
        result.frameCount < 1 || result.frameCount > 6 ||
        result.fps !== (result.frameCount === 1 ? 0 : 3) ||
        result.payloadBytes !== result.width * result.height * 2 * result.frameCount ||
        result.payloadCrc32 < 0 || result.payloadCrc32 > 0xffffffff) {
        throw new ImageGalleryError('GALLERY_FINGERPRINT_INVALID', 'Device image fingerprint is invalid.');
    }
    return result;
}

function publicItem(item) {
    return {
        id: item.id,
        scope: item.scope,
        title: item.title,
        sourceMime: item.sourceMime,
        width: item.width,
        height: item.height,
        frameCount: item.frameCount,
        fps: item.fps,
        payloadBytes: item.payloadBytes,
        payloadCrc32: item.payloadCrc32,
        deviceSha256: item.deviceSha256,
        sortOrder: item.sortOrder,
        published: item.published,
        createdAt: item.createdAt,
        updatedAt: item.updatedAt,
        sourceUrl: `/api/gallery/images/${encodeURIComponent(item.id)}/source`,
        previewUrl: `/api/gallery/images/${encodeURIComponent(item.id)}/preview`,
        deviceAssetUrl: `/api/gallery/images/${encodeURIComponent(item.id)}/device`,
    };
}

function adminItem(item) {
    return {
        ...publicItem(item),
        sourceUrl: `/api/admin/gallery/system/${encodeURIComponent(item.id)}/source`,
        previewUrl: `/api/admin/gallery/system/${encodeURIComponent(item.id)}/preview`,
        deviceAssetUrl: `/api/admin/gallery/system/${encodeURIComponent(item.id)}/device`,
    };
}

function readUpload(req) {
    let manifest = {};
    try { manifest = JSON.parse(String(req.body?.manifest || '{}')); } catch { throw new ImageGalleryError('GALLERY_MANIFEST_INVALID', 'Image manifest must be valid JSON.'); }
    const source = assertImageBytes(req.files?.source?.[0], SOURCE_TYPES, MAX_SOURCE_BYTES, 'source');
    const preview = assertImageBytes(req.files?.preview?.[0], PREVIEW_TYPES, MAX_PREVIEW_BYTES, 'preview');
    const device = req.files?.deviceAsset?.[0];
    if (!device || !Buffer.isBuffer(device.buffer)) throw new ImageGalleryError('GALLERY_FILE_REQUIRED', 'deviceAsset is required.');
    const metadata = parseUimgV3(device.buffer);
    if ((manifest.width !== undefined && Number(manifest.width) !== metadata.width) ||
        (manifest.height !== undefined && Number(manifest.height) !== metadata.height) ||
        (manifest.frameCount !== undefined && Number(manifest.frameCount) !== metadata.frameCount) ||
        (manifest.fps !== undefined && Number(manifest.fps) !== metadata.fps) ||
        (manifest.payloadCrc32 !== undefined && Number(manifest.payloadCrc32) !== metadata.payloadCrc32)) {
        throw new ImageGalleryError('GALLERY_MANIFEST_INVALID', 'Image manifest does not match the UIMG payload.');
    }
    return { manifest, source, preview, device: device.buffer, metadata };
}

function initImageGalleryRoutes(app, options) {
    const { store, storage, emailAuth, deviceAuth, adminAccess } = options;
    const memoryUpload = multer({ storage: multer.memoryStorage(), limits: { fileSize: MAX_SOURCE_BYTES, files: 3, fields: 4 } });
    const uploadFields = memoryUpload.fields([{ name: 'source', maxCount: 1 }, { name: 'preview', maxCount: 1 }, { name: 'deviceAsset', maxCount: 1 }]);
    const humanAdmin = adminAccess.requireAdmin({ humanOnly: true });
    const deviceRead = deviceAuth.requireSession(['config.read']);
    const requireUser = (write = false) => (req, res, next) => {
        const session = emailAuth.resolveSession(emailAuth.readSessionToken(req));
        if (!session) return res.status(401).json({ success: false, error: 'AUTH_REQUIRED', message: 'Sign in is required.' });
        try { if (write) emailAuth.requireOrigin(req.get('Origin')); } catch (error) { return next(error); }
        req.galleryUser = session;
        next();
    };
    const parseUpload = (req, res, next) => uploadFields(req, res, error => {
        if (error) return next(new ImageGalleryError('GALLERY_UPLOAD_INVALID', error.message));
        next();
    });
    const createUploaded = (req, scope) => {
        const upload = readUpload(req);
        const id = crypto.randomUUID();
        const sourceName = `source${upload.source.extension}`;
        const previewName = `preview${upload.preview.extension}`;
        storage.putSet(id, { [sourceName]: upload.source.data, [previewName]: upload.preview.data, 'device.uimg': upload.device });
        try {
            return store.create({
                id, scope, ownerUid: scope === 'user' ? req.galleryUser.uid : null,
                title: normalizeTitle(upload.manifest.title, req.files.source[0].originalname),
                sourceKey: `${id}/${sourceName}`, previewKey: `${id}/${previewName}`, deviceKey: `${id}/device.uimg`,
                sourceMime: upload.source.mimeType, ...upload.metadata,
                deviceSha256: sha256(upload.device), sortOrder: normalizeSortOrder(upload.manifest.sortOrder),
                published: scope === 'system' ? upload.manifest.published === true : true,
            });
        } catch (error) { storage.removeSet(id); throw error; }
    };

    app.get('/api/gallery/system', deviceRead, (req, res, next) => {
        try {
            const limit = Math.max(1, Math.min(100, Number(req.query.limit) || 30));
            const result = store.listSystem({ offset: cursorOffset(req.query.cursor), limit });
            res.json({ success: true, data: { items: result.items.map(publicItem), nextCursor: result.nextOffset === null ? null : Buffer.from(String(result.nextOffset)).toString('base64url') } });
        } catch (error) { next(error); }
    });
    app.get('/api/gallery/match', deviceRead, (req, res, next) => {
        try {
            const token = emailAuth.readSessionToken(req);
            const account = token ? emailAuth.resolveSession(token) : null;
            const item = store.findByFingerprint(
                deviceFingerprint(req.query),
                account?.uid || null,
            );
            res.json({ success: true, data: { item: item ? publicItem(item) : null } });
        } catch (error) { next(error); }
    });
    app.get('/api/gallery/mine', requireUser(false), (req, res, next) => {
        try { const result = store.listMine(req.galleryUser.uid); res.json({ success: true, data: { ...result, items: result.items.map(publicItem) } }); } catch (error) { next(error); }
    });
    app.post('/api/gallery/mine/images', requireUser(true), parseUpload, (req, res, next) => {
        try { res.status(201).json({ success: true, data: publicItem(createUploaded(req, 'user')) }); } catch (error) { next(error); }
    });
    app.delete('/api/gallery/mine/images', requireUser(true), (req, res, next) => {
        try {
            const ids = req.body?.imageIds;
            if (!Array.isArray(ids) || ids.length < 1 || ids.length > USER_GALLERY_LIMIT || ids.some(id => typeof id !== 'string')) throw new ImageGalleryError('GALLERY_DELETE_INVALID', 'imageIds must contain 1-10 image IDs.');
            const removed = store.deleteOwned(ids, req.galleryUser.uid);
            removed.forEach(item => storage.removeSet(item.id));
            res.json({ success: true, data: { deletedIds: removed.map(item => item.id) } });
        } catch (error) { next(error); }
    });

    app.get('/api/gallery/images/:id/:variant', (req, res, next) => {
        try {
            const item = store.find(req.params.id);
            if (!item || !['preview', 'device', 'source'].includes(req.params.variant)) throw new ImageGalleryError('GALLERY_IMAGE_NOT_FOUND', 'Image was not found.', 404);
            const serve = () => {
                const key = req.params.variant === 'preview' ? item.previewKey : req.params.variant === 'device' ? item.deviceKey : item.sourceKey;
                const filePath = storage.resolve(key);
                res.set('Cache-Control', item.scope === 'system'
                    ? 'public, max-age=300'
                    : 'private, max-age=31536000, immutable');
                res.sendFile(filePath);
            };
            if (item.scope === 'system') {
                if (!item.published) throw new ImageGalleryError('GALLERY_IMAGE_NOT_FOUND', 'Image was not found.', 404);
                return deviceRead(req, res, serve);
            }
            return requireUser(false)(req, res, () => {
                if (req.galleryUser.uid !== item.ownerUid) return res.status(404).json({ success: false, error: 'GALLERY_IMAGE_NOT_FOUND', message: 'Image was not found.' });
                serve();
            });
        } catch (error) { next(error); }
    });

    app.get('/api/admin/gallery/system', humanAdmin, (req, res, next) => {
        try {
            const limit = Math.max(1, Math.min(100, Number(req.query.limit) || 50));
            const result = store.listSystem({ offset: cursorOffset(req.query.cursor), limit, includeDrafts: true });
            res.json({ success: true, data: { items: result.items.map(adminItem), nextCursor: result.nextOffset === null ? null : Buffer.from(String(result.nextOffset)).toString('base64url') } });
        } catch (error) { next(error); }
    });
    app.post('/api/admin/gallery/system', humanAdmin, parseUpload, (req, res, next) => {
        try { res.status(201).json({ success: true, data: adminItem(createUploaded(req, 'system')) }); } catch (error) { next(error); }
    });
    app.patch('/api/admin/gallery/system/:id', humanAdmin, (req, res, next) => {
        try { res.json({ success: true, data: adminItem(store.updateSystem(req.params.id, req.body || {})) }); } catch (error) { next(error); }
    });
    app.delete('/api/admin/gallery/system/:id', humanAdmin, (req, res, next) => {
        try { const item = store.deleteSystem(req.params.id); storage.removeSet(item.id); res.json({ success: true, data: { deletedId: item.id } }); } catch (error) { next(error); }
    });
    app.get('/api/admin/gallery/system/:id/:variant', humanAdmin, (req, res, next) => {
        try {
            const item = store.find(req.params.id);
            if (!item || item.scope !== 'system' || !['preview', 'device', 'source'].includes(req.params.variant)) throw new ImageGalleryError('GALLERY_IMAGE_NOT_FOUND', 'Image was not found.', 404);
            const key = req.params.variant === 'preview' ? item.previewKey : req.params.variant === 'device' ? item.deviceKey : item.sourceKey;
            res.set('Cache-Control', 'private, no-store');
            res.sendFile(storage.resolve(key));
        } catch (error) { next(error); }
    });
}

module.exports = {
    GalleryStorage,
    ImageGalleryError,
    ImageGalleryStore,
    LocalGalleryStorage,
    initImageGalleryRoutes,
    parseUimgV3,
    crc32,
    USER_GALLERY_LIMIT,
};
