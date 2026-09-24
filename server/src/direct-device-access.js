'use strict';

const DIRECT_SCOPES = Object.freeze([
    'config.read',
    'config.write',
    'monitor.read',
    'device.control',
    'asset.write',
    'firmware.update'
]);

/** Compatibility context for public WebConfig resources; no device proof is required. */
function createDirectDeviceAccess(authService) {
    return {
        direct: true,
        webConfigTargetPolicy: authService.webConfigTargetPolicy,
        isReady: () => true,
        requireSession: () => (req, _res, next) => {
            req.deviceSession = {
                sessionId: 'direct-webhid',
                deviceId: null,
                accountUid: null,
                productId: 'HBOX',
                pcbRevision: '2.0.0',
                webConfigProfile: 'hbox-pcb-v2',
                hardwareVersion: '2.0.0',
                scopes: DIRECT_SCOPES
            };
            next();
        }
    };
}

module.exports = { createDirectDeviceAccess };
