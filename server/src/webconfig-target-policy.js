'use strict';

const fs = require('fs');
const path = require('path');

const DEFAULT_WEB_CONFIG_TARGET_POLICY = Object.freeze({
    schemaVersion: 1,
    // Product identity is still read from the signed device certificate. This
    // value is the allowlisted product for the configured trust domain.
    productId: 'HBOX',
    pcbRevisions: Object.freeze({
        '2.0.0': Object.freeze({
            profile: 'hbox-pcb-v2'
        })
    })
});

const PRODUCT_ID_PATTERN = /^[A-Z0-9]{4}$/;
const PROFILE_PATTERN = /^[a-z0-9](?:[a-z0-9-]{0,62}[a-z0-9])?$/;

class WebConfigTargetPolicyError extends Error {
    constructor(code, message, status = 403) {
        super(message);
        this.name = 'WebConfigTargetPolicyError';
        this.code = code;
        this.status = status;
    }
}

function isPcbRevision(value) {
    if (typeof value !== 'string') {
        return false;
    }
    const match = /^(\d+)\.(\d+)\.(\d+)$/.exec(value);
    return Boolean(match) && match.slice(1).every(part => Number(part) <= 255);
}

function validateTargetPolicy(value) {
    if (!value || typeof value !== 'object' || Array.isArray(value) ||
        Object.keys(value).sort().join(',') !==
            'pcbRevisions,productId,schemaVersion' ||
        value.schemaVersion !== 1 ||
        typeof value.productId !== 'string' ||
        !PRODUCT_ID_PATTERN.test(value.productId) ||
        !value.pcbRevisions || typeof value.pcbRevisions !== 'object' ||
        Array.isArray(value.pcbRevisions)) {
        throw new Error('WebConfig target policy header is invalid');
    }

    const entries = Object.entries(value.pcbRevisions);
    if (entries.length === 0 || entries.length > 64) {
        throw new Error(
            'WebConfig target policy must contain 1..64 PCB revisions'
        );
    }

    const pcbRevisions = {};
    for (const [pcbRevision, target] of entries) {
        if (!isPcbRevision(pcbRevision) ||
            !target || typeof target !== 'object' || Array.isArray(target) ||
            Object.keys(target).length !== 1 ||
            typeof target.profile !== 'string' ||
            !PROFILE_PATTERN.test(target.profile)) {
            throw new Error(
                `WebConfig target for PCB ${pcbRevision} is invalid`
            );
        }
        pcbRevisions[pcbRevision] = Object.freeze({
            profile: target.profile
        });
    }

    return Object.freeze({
        schemaVersion: 1,
        productId: value.productId,
        pcbRevisions: Object.freeze(pcbRevisions)
    });
}

function loadWebConfigTargetPolicy(
    environment = process.env,
    fsModule = fs
) {
    const inline = String(
        environment.WEB_CONFIG_TARGET_POLICY_JSON || ''
    ).trim();
    const fileName = String(
        environment.WEB_CONFIG_TARGET_POLICY_FILE || ''
    ).trim();
    if (inline && fileName) {
        throw new Error(
            'configure only one of WEB_CONFIG_TARGET_POLICY_JSON or ' +
            'WEB_CONFIG_TARGET_POLICY_FILE'
        );
    }

    let source = DEFAULT_WEB_CONFIG_TARGET_POLICY;
    if (inline) {
        source = JSON.parse(inline);
    } else if (fileName) {
        if (environment.NODE_ENV === 'production' &&
            !path.isAbsolute(fileName)) {
            throw new Error(
                'WEB_CONFIG_TARGET_POLICY_FILE must be an absolute path in production'
            );
        }
        source = JSON.parse(fsModule.readFileSync(fileName, 'utf8'));
    }
    return validateTargetPolicy(source);
}

class WebConfigTargetPolicy {
    constructor(policy = DEFAULT_WEB_CONFIG_TARGET_POLICY) {
        this.policy = validateTargetPolicy(policy);
    }

    get productId() {
        return this.policy.productId;
    }

    get profileSlugs() {
        return Object.freeze(
            Object.values(this.policy.pcbRevisions)
                .map(target => target.profile)
        );
    }

    assertProductId(productId) {
        if (typeof productId !== 'string' ||
            productId !== this.productId) {
            throw new WebConfigTargetPolicyError(
                'PRODUCT_NOT_SUPPORTED',
                'device product does not belong to this WebConfig trust domain'
            );
        }
        return this.productId;
    }

    resolve(identity) {
        if (!identity || typeof identity.productId !== 'string' ||
            typeof identity.hardwareVersion !== 'string') {
            throw new WebConfigTargetPolicyError(
                'PCB_REVISION_REQUIRED',
                'authenticated device certificate has no PCB revision',
                401
            );
        }
        this.assertProductId(identity.productId);
        const target = this.policy.pcbRevisions[identity.hardwareVersion];
        if (!target) {
            throw new WebConfigTargetPolicyError(
                'PCB_REVISION_NOT_SUPPORTED',
                `PCB revision ${identity.hardwareVersion} is not supported`
            );
        }
        return Object.freeze({
            productId: identity.productId,
            pcbRevision: identity.hardwareVersion,
            profile: target.profile,
            // Generated from a validated local profile slug. Policy files
            // cannot inject an origin or arbitrary navigation target.
            basePath: `/webconfig/${target.profile}/`
        });
    }
}

function createWebConfigTargetPolicyFromEnvironment(
    environment = process.env,
    fsModule = fs
) {
    return new WebConfigTargetPolicy(
        loadWebConfigTargetPolicy(environment, fsModule)
    );
}

module.exports = {
    DEFAULT_WEB_CONFIG_TARGET_POLICY,
    WebConfigTargetPolicy,
    WebConfigTargetPolicyError,
    createWebConfigTargetPolicyFromEnvironment,
    isPcbRevision,
    loadWebConfigTargetPolicy,
    validateTargetPolicy
};
