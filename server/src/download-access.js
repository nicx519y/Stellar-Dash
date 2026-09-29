'use strict';

const crypto = require('crypto');
const path = require('path');

function encodeBase64Url(value) {
    return Buffer.from(value).toString('base64')
        .replace(/\+/g, '-')
        .replace(/\//g, '_')
        .replace(/=+$/g, '');
}

class LegacyDownloadTicketStore {
    constructor(options = {}) {
        this.now = options.now || Date.now;
        this.ttlMs = options.ttlMs || 2 * 60 * 1000;
        this.maxRecords = options.maxRecords || 10000;
        this.records = new Map();
    }

    issue({ filename, deviceId, hardwareVersion }) {
        this.cleanup();
        if (this.records.size >= this.maxRecords ||
            typeof filename !== 'string' ||
            filename !== path.basename(filename) ||
            !filename) {
            return null;
        }
        let ticket;
        do {
            ticket = encodeBase64Url(crypto.randomBytes(32));
        } while (this.records.has(ticket));
        this.records.set(ticket, {
            filename,
            deviceId,
            hardwareVersion,
            expiresAt: this.now() + this.ttlMs
        });
        return ticket;
    }

    resolve(ticket, filename) {
        const record = this.records.get(ticket);
        if (!record || record.expiresAt <= this.now() ||
            record.filename !== filename) {
            if (record && record.expiresAt <= this.now()) {
                this.records.delete(ticket);
            }
            return null;
        }
        return record;
    }

    cleanup() {
        const now = this.now();
        for (const [ticket, record] of this.records.entries()) {
            if (record.expiresAt <= now) {
                this.records.delete(ticket);
            }
        }
    }
}

function initLegacyDownloadRoute(
    app,
    ticketStore,
    storageManager,
    uploadDir
) {
    app.get('/legacy-downloads/:ticket/:filename', (req, res) => {
        const filename = req.params.filename;
        if (typeof filename !== 'string' ||
            filename !== path.basename(filename)) {
            return res.status(404).json({
                success: false,
                error: 'DOWNLOAD_NOT_FOUND'
            });
        }
        const record = ticketStore.resolve(req.params.ticket, filename);
        const device = record &&
            storageManager.findDevice(record.deviceId);
        if (!record || !device ||
            device.status !== 'active' ||
            (device.hardwareVersion &&
             device.hardwareVersion !== record.hardwareVersion)) {
            return res.status(401).json({
                success: false,
                error: 'LEGACY_DOWNLOAD_TICKET_INVALID',
                message: 'legacy download ticket is invalid or expired'
            });
        }
        res.set('Cache-Control', 'private, no-store');
        return res.sendFile(filename, {
            root: uploadDir,
            dotfiles: 'deny',
            acceptRanges: true
        }, error => {
            if (!error) {
                return;
            }
            if (!res.headersSent) {
                return res.status(error.statusCode || 404).json({
                    success: false,
                    error: 'DOWNLOAD_NOT_FOUND'
                });
            }
            return res.destroy(error);
        });
    });
}

function createLegacyDownloadUrl(
    ticketStore,
    serverUrl,
    slot,
    device
) {
    if (!slot || !device) {
        return null;
    }
    const candidate = slot.filename || slot.filePath;
    if (typeof candidate !== 'string') {
        return null;
    }
    const filename = path.basename(candidate);
    const ticket = ticketStore.issue({
        filename,
        deviceId: device.deviceId,
        hardwareVersion: device.hardwareVersion
    });
    if (!ticket) {
        return null;
    }
    return `${serverUrl}/legacy-downloads/${ticket}/` +
        encodeURIComponent(filename);
}

module.exports = {
    LegacyDownloadTicketStore,
    initLegacyDownloadRoute,
    createLegacyDownloadUrl
};
