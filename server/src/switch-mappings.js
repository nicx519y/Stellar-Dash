'use strict';

const crypto = require('crypto');
const Database = require('better-sqlite3');
const express = require('express');

const MAX_MAPPING_LENGTH = 40;
const MAX_DEVICE_TEXT_BYTES = 15;
const MAX_SWITCH_IMAGE_BYTES = 2 * 1024 * 1024;
const SWITCH_IMAGE_TYPES = new Set(['image/jpeg', 'image/png', 'image/webp']);

class SwitchMappingError extends Error {
    constructor(code, message, status = 400) {
        super(message);
        this.name = 'SwitchMappingError';
        this.code = code;
        this.status = status;
    }
}

function utf8Length(value) {
    return Buffer.byteLength(value, 'utf8');
}

function deviceNameFromDisplayName(displayName) {
    let result = '';
    for (const character of String(displayName || '').trim()) {
        if (utf8Length(result + character) > MAX_DEVICE_TEXT_BYTES) break;
        result += character;
    }
    return result || 'mapping';
}

function finiteInteger(value, minimum, maximum, field) {
    if (!Number.isInteger(value) || value < minimum || value > maximum) {
        throw new SwitchMappingError(
            'INVALID_SWITCH_MAPPING',
            `${field} is outside the supported range.`
        );
    }
    return value;
}

function normalizeMappingInput(value, options = {}) {
    if (!value || typeof value !== 'object' || Array.isArray(value)) {
        throw new SwitchMappingError(
            'INVALID_SWITCH_MAPPING',
            'Switch mapping must be an object.'
        );
    }
    const name = String(value.name || '').trim();
    if (!name || utf8Length(name) > MAX_DEVICE_TEXT_BYTES) {
        throw new SwitchMappingError(
            'INVALID_SWITCH_MAPPING',
            'Device mapping name must be 1-15 UTF-8 bytes.'
        );
    }
    const length = finiteInteger(
        Number(value.length), 2, MAX_MAPPING_LENGTH, 'length'
    );
    const step = Number(value.step);
    if (!Number.isFinite(step) || step < 0.1 || step > 10) {
        throw new SwitchMappingError(
            'INVALID_SWITCH_MAPPING',
            'step is outside the supported range.'
        );
    }
    const samplingNoise = finiteInteger(
        Number(value.samplingNoise), 0, 0xffff, 'samplingNoise'
    );
    const samplingFrequency = finiteInteger(
        Number(value.samplingFrequency), 1, 0xffff, 'samplingFrequency'
    );
    if (!Array.isArray(value.originalValues) ||
        value.originalValues.length !== length) {
        throw new SwitchMappingError(
            'INVALID_SWITCH_MAPPING',
            'originalValues must contain exactly length samples.'
        );
    }
    const allowBlank = options.allowBlank === true;
    const allowIncomplete = allowBlank || options.allowIncomplete === true;
    const originalValues = value.originalValues.map((sample, index) =>
        finiteInteger(Number(sample), allowIncomplete ? 0 : 1, 0xffff, `originalValues[${index}]`)
    );
    const isBlank = originalValues.every(sample => sample === 0);
    if (allowBlank && !isBlank) {
        throw new SwitchMappingError(
            'INVALID_SWITCH_MAPPING',
            'Blank switch mappings must contain only zero samples.'
        );
    }
    if (!isBlank && originalValues[0] === originalValues[originalValues.length - 1]) {
        throw new SwitchMappingError(
            'INVALID_SWITCH_MAPPING',
            'Switch mapping endpoints must be different.'
        );
    }
    return {
        name,
        length,
        step: Math.fround(step),
        samplingNoise,
        samplingFrequency,
        originalValues,
    };
}

function canonicalMapping(mapping) {
    const result = Buffer.alloc(220);
    result.write('HBOX-ADC-MAP-V1\0', 0, 16, 'ascii');
    result.write(mapping.id, 16, 15, 'utf8');
    result.write(mapping.name, 32, 15, 'utf8');
    result.writeUInt32LE(mapping.length, 48);
    result.writeFloatLE(Math.fround(mapping.step), 52);
    result.writeUInt16LE(mapping.samplingNoise, 56);
    result.writeUInt16LE(mapping.samplingFrequency, 58);
    mapping.originalValues.forEach((value, index) => {
        result.writeUInt32LE(value, 60 + index * 4);
    });
    return result;
}

function mappingSha256(mapping) {
    return crypto.createHash('sha256')
        .update(canonicalMapping(mapping))
        .digest('hex');
}

function normalizeCatalogMetadata(body) {
    const displayName = String(body?.displayName || '').trim();
    const description = String(body?.description || '').trim();
    if (!displayName || displayName.length > 80) {
        throw new SwitchMappingError(
            'INVALID_SWITCH_MAPPING_CATALOG',
            'Display name must be 1-80 characters.'
        );
    }
    if (description.length > 500) {
        throw new SwitchMappingError(
            'INVALID_SWITCH_MAPPING_CATALOG',
            'Description cannot exceed 500 characters.'
        );
    }
    return { displayName, description };
}

function normalizeSwitchImage(contentType, value) {
    const mimeType = String(contentType || '').split(';', 1)[0].trim().toLowerCase();
    const data = Buffer.isBuffer(value) ? value : Buffer.from(value || []);
    if (!SWITCH_IMAGE_TYPES.has(mimeType)) {
        throw new SwitchMappingError(
            'INVALID_SWITCH_MAPPING_IMAGE',
            'Switch images must be JPEG, PNG, or WebP.'
        );
    }
    if (data.length < 12 || data.length > MAX_SWITCH_IMAGE_BYTES) {
        throw new SwitchMappingError(
            'INVALID_SWITCH_MAPPING_IMAGE',
            'Switch images must contain 12 bytes to 2 MiB.'
        );
    }
    const isPng = mimeType === 'image/png' &&
        data.subarray(0, 8).equals(Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]));
    const isJpeg = mimeType === 'image/jpeg' &&
        data[0] === 0xff && data[1] === 0xd8 && data[2] === 0xff;
    const isWebp = mimeType === 'image/webp' &&
        data.subarray(0, 4).toString('ascii') === 'RIFF' &&
        data.subarray(8, 12).toString('ascii') === 'WEBP';
    if (!isPng && !isJpeg && !isWebp) {
        throw new SwitchMappingError(
            'INVALID_SWITCH_MAPPING_IMAGE',
            'Switch image bytes do not match the declared media type.'
        );
    }
    return { mimeType, data };
}

function decodeRevision(row) {
    if (!row) return null;
    const mapping = {
        id: row.revision_id,
        name: row.device_name,
        length: row.length,
        step: row.step,
        samplingNoise: row.sampling_noise,
        samplingFrequency: row.sampling_frequency,
        originalValues: JSON.parse(row.original_values),
    };
    return {
        catalogId: row.catalog_id,
        revisionId: row.revision_id,
        revision: row.revision,
        sha256: row.sha256,
        createdAt: row.revision_created_at || row.created_at,
        createdBy: row.created_by,
        publishedAt: row.published_at || null,
        publishedBy: row.published_by || null,
        mapping,
    };
}

function decodeCatalog(row, revision = null) {
    return {
        catalogId: row.catalog_id,
        displayName: row.display_name,
        description: row.description,
        productId: row.product_id,
        pcbRevision: row.pcb_revision,
        hardwareVersion: row.hardware_version,
        publishedRevisionId: row.published_revision_id,
        hasImage: Boolean(row.has_image ?? row.image_data),
        imageUpdatedAt: row.image_updated_at || null,
        archived: Boolean(row.archived),
        createdAt: row.catalog_created_at || row.created_at,
        updatedAt: row.updated_at,
        ...(revision ? { revision } : {}),
    };
}

class SwitchMappingStore {
    constructor(options) {
        this.now = options.now || (() => new Date().toISOString());
        this.database = new Database(options.databasePath);
        this.database.pragma('journal_mode = WAL');
        this.database.pragma('foreign_keys = ON');
        this.database.exec(`
            CREATE TABLE IF NOT EXISTS switch_mapping_catalogs (
                catalog_id TEXT PRIMARY KEY,
                display_name TEXT NOT NULL,
                description TEXT NOT NULL,
                product_id TEXT NOT NULL,
                pcb_revision TEXT NOT NULL,
                hardware_version TEXT NOT NULL,
                published_revision_id TEXT,
                image_mime_type TEXT,
                image_data BLOB,
                image_updated_at TEXT,
                archived INTEGER NOT NULL DEFAULT 0,
                created_at TEXT NOT NULL,
                updated_at TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS switch_mapping_revisions (
                revision_id TEXT PRIMARY KEY,
                catalog_id TEXT NOT NULL REFERENCES switch_mapping_catalogs(catalog_id),
                revision INTEGER NOT NULL,
                device_name TEXT NOT NULL,
                length INTEGER NOT NULL,
                step REAL NOT NULL,
                sampling_noise INTEGER NOT NULL,
                sampling_frequency INTEGER NOT NULL,
                original_values TEXT NOT NULL,
                sha256 TEXT NOT NULL,
                created_by TEXT NOT NULL,
                created_at TEXT NOT NULL,
                published_by TEXT,
                published_at TEXT,
                UNIQUE(catalog_id, revision)
            );
            CREATE INDEX IF NOT EXISTS switch_mapping_compatibility
            ON switch_mapping_catalogs(product_id, pcb_revision, hardware_version);
        `);
        const revisionColumns = new Set(
            this.database.pragma('table_info(switch_mapping_revisions)')
                .map(column => column.name)
        );
        if (!revisionColumns.has('published_by')) {
            this.database.exec(
                'ALTER TABLE switch_mapping_revisions ADD COLUMN published_by TEXT'
            );
        }
        if (!revisionColumns.has('published_at')) {
            this.database.exec(
                'ALTER TABLE switch_mapping_revisions ADD COLUMN published_at TEXT'
            );
        }
        const catalogColumns = new Set(
            this.database.pragma('table_info(switch_mapping_catalogs)')
                .map(column => column.name)
        );
        if (!catalogColumns.has('image_mime_type')) {
            this.database.exec(
                'ALTER TABLE switch_mapping_catalogs ADD COLUMN image_mime_type TEXT'
            );
        }
        if (!catalogColumns.has('image_data')) {
            this.database.exec(
                'ALTER TABLE switch_mapping_catalogs ADD COLUMN image_data BLOB'
            );
        }
        if (!catalogColumns.has('image_updated_at')) {
            this.database.exec(
                'ALTER TABLE switch_mapping_catalogs ADD COLUMN image_updated_at TEXT'
            );
        }

        // The catalog no longer has a hidden-draft state. Promote the latest
        // revision left by older server builds so existing zero-filled maps
        // remain visible and installable after this migration.
        const migrationNow = this.now();
        const latestRevisionSql = `
            SELECT latest.revision_id
            FROM switch_mapping_revisions latest
            WHERE latest.catalog_id = switch_mapping_catalogs.catalog_id
            ORDER BY latest.revision DESC LIMIT 1
        `;
        const migrate = this.database.transaction(() => {
            this.database.prepare(`
                UPDATE switch_mapping_revisions
                SET published_by = COALESCE(published_by, 'catalog-migration'),
                    published_at = COALESCE(published_at, ?)
                WHERE revision_id IN (
                    SELECT revision_id FROM switch_mapping_revisions candidate
                    WHERE candidate.revision = (
                        SELECT MAX(latest.revision)
                        FROM switch_mapping_revisions latest
                        WHERE latest.catalog_id = candidate.catalog_id
                    )
                )
            `).run(migrationNow);
            this.database.prepare(`
                UPDATE switch_mapping_catalogs
                SET published_revision_id = (${latestRevisionSql}),
                    updated_at = ?
                WHERE archived = 0
                  AND (${latestRevisionSql}) IS NOT NULL
                  AND COALESCE(published_revision_id, '') <> (${latestRevisionSql})
            `).run(migrationNow);
        });
        migrate();
    }

    close() {
        this.database.close();
    }

    catalog(catalogId) {
        return this.database.prepare(
            'SELECT * FROM switch_mapping_catalogs WHERE catalog_id = ?'
        ).get(catalogId) || null;
    }

    revision(revisionId) {
        return this.database.prepare(
            'SELECT * FROM switch_mapping_revisions WHERE revision_id = ?'
        ).get(revisionId) || null;
    }

    uniqueRevisionId() {
        for (let attempt = 0; attempt < 8; attempt += 1) {
            const id = crypto.randomBytes(9).toString('base64url');
            if (!this.revision(id)) return id;
        }
        throw new SwitchMappingError(
            'SWITCH_MAPPING_ID_EXHAUSTED',
            'Unable to allocate a switch mapping revision ID.',
            500
        );
    }

    assertDisplayNameAvailable(displayName, compatibility, excludedCatalogId = '') {
        const duplicate = this.database.prepare(`
            SELECT catalog_id FROM switch_mapping_catalogs
            WHERE product_id = ? AND pcb_revision = ? AND hardware_version = ?
              AND (TRIM(display_name) COLLATE NOCASE) = (TRIM(?) COLLATE NOCASE)
              AND catalog_id <> ?
            LIMIT 1
        `).get(
            compatibility.productId,
            compatibility.pcbRevision,
            compatibility.hardwareVersion,
            displayName,
            excludedCatalogId
        );
        if (duplicate) {
            throw new SwitchMappingError(
                'SWITCH_MAPPING_NAME_CONFLICT',
                'A switch mapping with this name already exists for this hardware.',
                409
            );
        }
    }

    createDraft({ catalogId, metadata, compatibility, mapping, actor, allowBlank = false }) {
        mapping = normalizeMappingInput(mapping, { allowBlank });
        metadata = normalizeCatalogMetadata(metadata);
        const transaction = this.database.transaction(() => {
            const now = this.now();
            let catalog = catalogId ? this.catalog(catalogId) : null;
            if (catalogId && !catalog) {
                throw new SwitchMappingError(
                    'SWITCH_MAPPING_NOT_FOUND',
                    'Switch mapping catalog was not found.',
                    404
                );
            }
            if (!catalog) {
                this.assertDisplayNameAvailable(metadata.displayName, compatibility);
                catalogId = crypto.randomUUID();
                this.database.prepare(`
                    INSERT INTO switch_mapping_catalogs (
                        catalog_id, display_name, description, product_id,
                        pcb_revision, hardware_version, created_at, updated_at
                    ) VALUES (?, ?, ?, ?, ?, ?, ?, ?)
                `).run(
                    catalogId, metadata.displayName, metadata.description,
                    compatibility.productId, compatibility.pcbRevision,
                    compatibility.hardwareVersion, now, now
                );
                catalog = this.catalog(catalogId);
            } else {
                if (catalog.product_id !== compatibility.productId ||
                    catalog.pcb_revision !== compatibility.pcbRevision ||
                    catalog.hardware_version !== compatibility.hardwareVersion) {
                    throw new SwitchMappingError(
                        'SWITCH_MAPPING_HARDWARE_MISMATCH',
                        'A catalog cannot be revised from different hardware.',
                        409
                    );
                }
                this.assertDisplayNameAvailable(
                    metadata.displayName,
                    compatibility,
                    catalogId
                );
                this.database.prepare(`
                    UPDATE switch_mapping_catalogs
                    SET display_name = ?, description = ?, updated_at = ?
                    WHERE catalog_id = ?
                `).run(metadata.displayName, metadata.description, now, catalogId);
            }

            const next = this.database.prepare(`
                SELECT COALESCE(MAX(revision), 0) + 1 AS revision
                FROM switch_mapping_revisions WHERE catalog_id = ?
            `).get(catalogId).revision;
            const revisionId = this.uniqueRevisionId();
            const canonical = { id: revisionId, ...mapping };
            const sha256 = mappingSha256(canonical);
            this.database.prepare(`
                INSERT INTO switch_mapping_revisions (
                    revision_id, catalog_id, revision, device_name, length,
                    step, sampling_noise, sampling_frequency, original_values,
                    sha256, created_by, created_at
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            `).run(
                revisionId, catalogId, next, canonical.name, canonical.length,
                canonical.step, canonical.samplingNoise,
                canonical.samplingFrequency,
                JSON.stringify(canonical.originalValues), sha256, actor, now
            );
            return this.getAdminCatalog(catalogId, revisionId);
        });
        return transaction();
    }

    getAdminCatalog(catalogId, revisionId = null) {
        const row = this.catalog(catalogId);
        if (!row) return null;
        const revisionRow = revisionId ? this.revision(revisionId) : null;
        return decodeCatalog(row, revisionRow ? decodeRevision(revisionRow) : null);
    }

    publish(catalogId, revisionId, actor = 'unknown') {
        const catalog = this.catalog(catalogId);
        const revision = this.revision(revisionId);
        if (!catalog || !revision || revision.catalog_id !== catalogId) {
            throw new SwitchMappingError(
                'SWITCH_MAPPING_NOT_FOUND',
                'Switch mapping revision was not found.',
                404
            );
        }
        if (catalog.archived) {
            throw new SwitchMappingError(
                'SWITCH_MAPPING_ARCHIVED',
                'Archived switch mappings cannot be published.',
                409
            );
        }
        const now = this.now();
        const transaction = this.database.transaction(() => {
            this.database.prepare(`
                UPDATE switch_mapping_revisions
                SET published_by = COALESCE(published_by, ?),
                    published_at = COALESCE(published_at, ?)
                WHERE revision_id = ?
            `).run(actor, now, revisionId);
            this.database.prepare(`
                UPDATE switch_mapping_catalogs
                SET published_revision_id = ?, updated_at = ? WHERE catalog_id = ?
            `).run(revisionId, now, catalogId);
        });
        transaction();
        return this.getAdminCatalog(catalogId, revisionId);
    }

    updatePublishedMapping(catalogId, compatibility, input) {
        const catalog = this.catalog(catalogId);
        if (!catalog || catalog.archived ||
            catalog.product_id !== compatibility.productId ||
            catalog.pcb_revision !== compatibility.pcbRevision ||
            catalog.hardware_version !== compatibility.hardwareVersion ||
            !catalog.published_revision_id) {
            throw new SwitchMappingError(
                'SWITCH_MAPPING_NOT_FOUND',
                'Compatible published switch mapping was not found.',
                404
            );
        }
        const revision = this.revision(catalog.published_revision_id);
        if (!revision || String(input?.id || '') !== revision.revision_id ||
            Math.fround(Number(input?.step)) !== Math.fround(revision.step)) {
            throw new SwitchMappingError(
                'SWITCH_MAPPING_IDENTITY_MISMATCH',
                'Recorded mapping identity or step does not match the published mapping.',
                409
            );
        }
        const mapping = normalizeMappingInput({
            ...input,
            name: revision.device_name,
            step: revision.step,
        }, { allowIncomplete: true });
        const canonical = { id: revision.revision_id, ...mapping };
        const sha256 = mappingSha256(canonical);
        const now = this.now();
        const transaction = this.database.transaction(() => {
            this.database.prepare(`
                UPDATE switch_mapping_revisions
                SET length = ?, sampling_noise = ?, sampling_frequency = ?,
                    original_values = ?, sha256 = ?
                WHERE revision_id = ?
            `).run(
                canonical.length,
                canonical.samplingNoise,
                canonical.samplingFrequency,
                JSON.stringify(canonical.originalValues),
                sha256,
                revision.revision_id
            );
            this.database.prepare(`
                UPDATE switch_mapping_catalogs SET updated_at = ?
                WHERE catalog_id = ?
            `).run(now, catalogId);
        });
        transaction();
        return this.published(catalogId, compatibility);
    }

    unpublish(catalogId) {
        if (!this.catalog(catalogId)) {
            throw new SwitchMappingError(
                'SWITCH_MAPPING_NOT_FOUND',
                'Switch mapping catalog was not found.',
                404
            );
        }
        this.database.prepare(`
            UPDATE switch_mapping_catalogs
            SET published_revision_id = NULL, updated_at = ? WHERE catalog_id = ?
        `).run(this.now(), catalogId);
        return this.getAdminCatalog(catalogId);
    }

    updateMetadata(catalogId, metadata) {
        const catalog = this.catalog(catalogId);
        if (!catalog) {
            throw new SwitchMappingError(
                'SWITCH_MAPPING_NOT_FOUND',
                'Switch mapping catalog was not found.',
                404
            );
        }
        this.assertDisplayNameAvailable(
            metadata.displayName,
            {
                productId: catalog.product_id,
                pcbRevision: catalog.pcb_revision,
                hardwareVersion: catalog.hardware_version,
            },
            catalogId
        );
        this.database.prepare(`
            UPDATE switch_mapping_catalogs
            SET display_name = ?, description = ?, updated_at = ?
            WHERE catalog_id = ?
        `).run(metadata.displayName, metadata.description, this.now(), catalogId);
        return this.getAdminCatalog(catalogId);
    }

    updateImage(catalogId, contentType, value) {
        if (!this.catalog(catalogId)) {
            throw new SwitchMappingError(
                'SWITCH_MAPPING_NOT_FOUND',
                'Switch mapping catalog was not found.',
                404
            );
        }
        const image = normalizeSwitchImage(contentType, value);
        const now = this.now();
        this.database.prepare(`
            UPDATE switch_mapping_catalogs
            SET image_mime_type = ?, image_data = ?, image_updated_at = ?,
                updated_at = ?
            WHERE catalog_id = ?
        `).run(image.mimeType, image.data, now, now, catalogId);
        return this.getAdminCatalog(catalogId);
    }

    image(catalogId, compatibility) {
        const row = this.database.prepare(`
            SELECT image_mime_type, image_data, image_updated_at
            FROM switch_mapping_catalogs
            WHERE catalog_id = ? AND archived = 0
              AND published_revision_id IS NOT NULL
              AND product_id = ? AND pcb_revision = ? AND hardware_version = ?
        `).get(
            catalogId, compatibility.productId,
            compatibility.pcbRevision, compatibility.hardwareVersion
        );
        if (!row?.image_data || !row.image_mime_type) return null;
        return {
            mimeType: row.image_mime_type,
            data: row.image_data,
            updatedAt: row.image_updated_at,
        };
    }

    deleteCatalog(catalogId) {
        const catalog = this.catalog(catalogId);
        if (!catalog) {
            throw new SwitchMappingError(
                'SWITCH_MAPPING_NOT_FOUND',
                'Switch mapping catalog was not found.',
                404
            );
        }
        const revisionIds = this.database.prepare(`
            SELECT revision_id FROM switch_mapping_revisions
            WHERE catalog_id = ? ORDER BY revision
        `).all(catalogId).map(row => row.revision_id);
        const remove = this.database.transaction(() => {
            this.database.prepare(
                'DELETE FROM switch_mapping_revisions WHERE catalog_id = ?'
            ).run(catalogId);
            this.database.prepare(
                'DELETE FROM switch_mapping_catalogs WHERE catalog_id = ?'
            ).run(catalogId);
        });
        remove();
        return {
            catalogId,
            deletedRevisionIds: revisionIds,
            deleted: true,
        };
    }

    listPublished(compatibility) {
        return this.database.prepare(`
            SELECT c.catalog_id, c.display_name, c.description,
                   c.image_updated_at, c.updated_at,
                   (c.image_data IS NOT NULL) AS has_image,
                   r.revision, r.revision_id, r.sha256,
                   r.created_at AS revision_created_at
            FROM switch_mapping_catalogs c
            JOIN switch_mapping_revisions r
              ON r.revision_id = c.published_revision_id
            WHERE c.archived = 0 AND c.product_id = ?
              AND c.pcb_revision = ? AND c.hardware_version = ?
            ORDER BY c.display_name COLLATE NOCASE, c.catalog_id
        `).all(
            compatibility.productId,
            compatibility.pcbRevision,
            compatibility.hardwareVersion
        ).map(row => {
            return {
                catalogId: row.catalog_id,
                displayName: row.display_name,
                description: row.description,
                revisionId: row.revision_id,
                revision: row.revision,
                sha256: row.sha256,
                hasImage: Boolean(row.has_image),
                imageUpdatedAt: row.image_updated_at || null,
                updatedAt: row.updated_at,
            };
        });
    }

    published(catalogId, compatibility) {
        const row = this.database.prepare(`
            SELECT c.catalog_id, c.display_name, c.description,
                   c.product_id, c.pcb_revision, c.hardware_version,
                   c.published_revision_id, c.archived,
                   c.created_at AS catalog_created_at,
                   c.updated_at, c.image_updated_at,
                   (c.image_data IS NOT NULL) AS has_image,
                   r.revision_id, r.revision, r.device_name, r.length,
                   r.step, r.sampling_noise, r.sampling_frequency,
                   r.original_values, r.sha256, r.created_by,
                   r.created_at AS revision_created_at,
                   r.published_by, r.published_at
            FROM switch_mapping_catalogs c
            JOIN switch_mapping_revisions r
              ON r.revision_id = c.published_revision_id
            WHERE c.catalog_id = ? AND c.archived = 0
              AND c.product_id = ? AND c.pcb_revision = ?
              AND c.hardware_version = ?
        `).get(
            catalogId, compatibility.productId,
            compatibility.pcbRevision, compatibility.hardwareVersion
        );
        if (!row) return null;
        return decodeCatalog(row, decodeRevision(row));
    }

    adminCompatible(catalogId, compatibility) {
        const catalog = this.catalog(catalogId);
        if (!catalog || catalog.product_id !== compatibility.productId ||
            catalog.pcb_revision !== compatibility.pcbRevision ||
            catalog.hardware_version !== compatibility.hardwareVersion) {
            return null;
        }
        const revision = this.database.prepare(`
            SELECT * FROM switch_mapping_revisions
            WHERE catalog_id = ? ORDER BY revision DESC LIMIT 1
        `).get(catalogId);
        return decodeCatalog(catalog, revision ? decodeRevision(revision) : null);
    }

    listAdminCompatible(compatibility) {
        return this.database.prepare(`
            SELECT c.catalog_id, c.display_name, c.description,
                   c.published_revision_id, c.image_updated_at, c.updated_at,
                   (c.image_data IS NOT NULL) AS has_image,
                   r.revision, r.revision_id, r.sha256,
                   r.created_at AS revision_created_at
            FROM switch_mapping_catalogs c
            JOIN switch_mapping_revisions r ON r.catalog_id = c.catalog_id
            WHERE c.archived = 0 AND c.product_id = ?
              AND c.pcb_revision = ? AND c.hardware_version = ?
              AND r.revision = (
                  SELECT MAX(latest.revision) FROM switch_mapping_revisions latest
                  WHERE latest.catalog_id = c.catalog_id
              )
            ORDER BY c.created_at, c.catalog_id
        `).all(
            compatibility.productId,
            compatibility.pcbRevision,
            compatibility.hardwareVersion
        ).map(row => ({
            catalogId: row.catalog_id,
            displayName: row.display_name,
            description: row.description,
            revisionId: row.revision_id,
            revision: row.revision,
            sha256: row.sha256,
            hasImage: Boolean(row.has_image),
            imageUpdatedAt: row.image_updated_at || null,
            updatedAt: row.updated_at,
            isDraft: row.published_revision_id !== row.revision_id,
        }));
    }

    adminImage(catalogId, compatibility) {
        const row = this.database.prepare(`
            SELECT image_mime_type, image_data, image_updated_at
            FROM switch_mapping_catalogs
            WHERE catalog_id = ? AND product_id = ?
              AND pcb_revision = ? AND hardware_version = ?
        `).get(
            catalogId, compatibility.productId,
            compatibility.pcbRevision, compatibility.hardwareVersion
        );
        if (!row?.image_data || !row.image_mime_type) return null;
        return {
            mimeType: row.image_mime_type,
            data: row.image_data,
            updatedAt: row.image_updated_at,
        };
    }

    listAdmin() {
        return this.database.prepare(`
            SELECT * FROM switch_mapping_catalogs
            ORDER BY updated_at DESC, catalog_id
        `).all().map(row => decodeCatalog(row));
    }
}

function compatibilityFromSession(session) {
    if (!session?.productId || !session?.pcbRevision || !session?.hardwareVersion) {
        throw new SwitchMappingError(
            'SWITCH_MAPPING_DEVICE_CONTEXT_REQUIRED',
            'An authenticated device hardware context is required.',
            401
        );
    }
    return {
        productId: session.productId,
        pcbRevision: session.pcbRevision,
        hardwareVersion: session.hardwareVersion,
    };
}

function sendError(response, error) {
    const known = error instanceof SwitchMappingError;
    if (!known) console.error('Switch mapping error:', error);
    response.status(known ? error.status : 500).json({
        success: false,
        error: known ? error.code : 'SWITCH_MAPPING_ERROR',
        message: known ? error.message : 'Switch mapping operation failed.',
    });
}

function initSwitchMappingRoutes(app, options) {
    const store = options.store;
    const deviceRead = options.deviceAuth.requireSession(['config.read']);
    const humanAdmin = options.adminAccess.requireAdmin({ humanOnly: true });
    const dualAdmin = options.adminAccess.requireAdmin({
        humanOnly: true,
        allowDeviceBearer: true,
    });
    const route = handler => (req, res) => {
        res.set('Cache-Control', 'private, no-store');
        try {
            handler(req, res);
        } catch (error) {
            sendError(res, error);
        }
    };

    app.get('/api/switch-mappings', deviceRead, route((req, res) => {
        res.json({
            success: true,
            data: {
                items: store.listPublished(
                    compatibilityFromSession(req.deviceSession)
                ),
            },
        });
    }));

    app.get('/api/switch-mappings/:catalogId', deviceRead, route((req, res) => {
        const item = store.published(
            req.params.catalogId,
            compatibilityFromSession(req.deviceSession)
        );
        if (!item) {
            throw new SwitchMappingError(
                'SWITCH_MAPPING_NOT_FOUND',
                'Compatible published switch mapping was not found.',
                404
            );
        }
        res.json({ success: true, data: item });
    }));

    app.get('/api/switch-mappings/:catalogId/image', deviceRead, route((req, res) => {
        const image = store.image(
            req.params.catalogId,
            compatibilityFromSession(req.deviceSession)
        );
        if (!image) {
            throw new SwitchMappingError(
                'SWITCH_MAPPING_IMAGE_NOT_FOUND',
                'This switch mapping does not have an image.',
                404
            );
        }
        res.set('Content-Type', image.mimeType);
        res.set('Content-Length', String(image.data.length));
        res.send(image.data);
    }));

    app.get('/api/admin/switch-mappings', humanAdmin, route((req, res) => {
        res.json({ success: true, data: { items: store.listAdmin() } });
    }));

    app.get(
        '/api/admin/switch-mappings-compatible',
        options.deviceAuth.requireSession(['config.read']),
        dualAdmin,
        route((req, res) => {
            res.json({
                success: true,
                data: {
                    items: store.listAdminCompatible(
                        compatibilityFromSession(req.deviceSession)
                    ),
                },
            });
        })
    );

    app.post(
        '/api/admin/switch-mappings/blank',
        options.deviceAuth.requireSession(['config.read']),
        dualAdmin,
        route((req, res) => {
            const length = finiteInteger(
                Number(req.body?.length), 2, MAX_MAPPING_LENGTH, 'length'
            );
            const step = Number(req.body?.step);
            const metadata = normalizeCatalogMetadata(req.body);
            const item = store.createDraft({
                catalogId: null,
                metadata,
                compatibility: compatibilityFromSession(req.deviceSession),
                mapping: {
                    name: deviceNameFromDisplayName(metadata.displayName),
                    length,
                    step,
                    samplingNoise: 0,
                    samplingFrequency: 1,
                    originalValues: Array.from({ length }, () => 0),
                },
                actor: req.authenticatedAdmin.username,
                allowBlank: true,
            });
            const published = store.publish(
                item.catalogId,
                item.revision.revisionId,
                req.authenticatedAdmin.username
            );
            res.status(201).json({ success: true, data: published });
        })
    );

    app.patch(
        '/api/admin/switch-mappings/:catalogId/mapping',
        options.deviceAuth.requireSession(['config.read']),
        dualAdmin,
        route((req, res) => {
            res.json({
                success: true,
                data: store.updatePublishedMapping(
                    req.params.catalogId,
                    compatibilityFromSession(req.deviceSession),
                    req.body?.mapping
                ),
            });
        })
    );

    app.get(
        '/api/admin/switch-mappings/:catalogId',
        options.deviceAuth.requireSession(['config.read']),
        dualAdmin,
        route((req, res) => {
            const item = store.adminCompatible(
                req.params.catalogId,
                compatibilityFromSession(req.deviceSession)
            );
            if (!item) {
                throw new SwitchMappingError(
                    'SWITCH_MAPPING_NOT_FOUND',
                    'Compatible switch mapping catalog was not found.',
                    404
                );
            }
            res.json({ success: true, data: item });
        })
    );

    app.get(
        '/api/admin/switch-mappings/:catalogId/image',
        options.deviceAuth.requireSession(['config.read']),
        dualAdmin,
        route((req, res) => {
            const image = store.adminImage(
                req.params.catalogId,
                compatibilityFromSession(req.deviceSession)
            );
            if (!image) {
                throw new SwitchMappingError(
                    'SWITCH_MAPPING_IMAGE_NOT_FOUND',
                    'This switch mapping does not have an image.',
                    404
                );
            }
            res.set('Content-Type', image.mimeType);
            res.set('Content-Length', String(image.data.length));
            res.send(image.data);
        })
    );

    app.post(
        '/api/admin/switch-mappings/drafts',
        options.deviceAuth.requireSession(['config.read']),
        dualAdmin,
        route((req, res) => {
            const item = store.createDraft({
                catalogId: req.body?.catalogId || null,
                metadata: normalizeCatalogMetadata(req.body),
                compatibility: compatibilityFromSession(req.deviceSession),
                mapping: normalizeMappingInput(req.body?.mapping),
                actor: req.authenticatedAdmin.username,
            });
            res.status(201).json({ success: true, data: item });
        })
    );

    app.post(
        '/api/admin/switch-mappings/:catalogId/revisions/:revisionId/publish',
        options.deviceAuth.requireSession(['config.read']),
        dualAdmin,
        route((req, res) => {
            res.json({
                success: true,
                data: store.publish(
                    req.params.catalogId,
                    req.params.revisionId,
                    req.authenticatedAdmin.username
                ),
            });
        })
    );

    app.post(
        '/api/admin/switch-mappings/:catalogId/unpublish',
        humanAdmin,
        route((req, res) => {
            res.json({ success: true, data: store.unpublish(req.params.catalogId) });
        })
    );

    app.patch(
        '/api/admin/switch-mappings/:catalogId',
        options.deviceAuth.requireSession(['config.read']),
        dualAdmin,
        route((req, res) => {
            res.json({
                success: true,
                data: store.updateMetadata(
                    req.params.catalogId,
                    normalizeCatalogMetadata(req.body)
                ),
            });
        })
    );

    app.put(
        '/api/admin/switch-mappings/:catalogId/image',
        options.deviceAuth.requireSession(['config.read']),
        dualAdmin,
        express.raw({
            type: ['image/jpeg', 'image/png', 'image/webp'],
            limit: MAX_SWITCH_IMAGE_BYTES,
        }),
        route((req, res) => {
            res.json({
                success: true,
                data: store.updateImage(
                    req.params.catalogId,
                    req.get('Content-Type'),
                    req.body
                ),
            });
        })
    );

    app.delete(
        '/api/admin/switch-mappings/:catalogId',
        options.deviceAuth.requireSession(['config.read']),
        dualAdmin,
        route((req, res) => {
            res.json({ success: true, data: store.deleteCatalog(req.params.catalogId) });
        })
    );
}

module.exports = {
    SwitchMappingError,
    SwitchMappingStore,
    canonicalMapping,
    initSwitchMappingRoutes,
    mappingSha256,
    normalizeMappingInput,
    normalizeSwitchImage,
};
