'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');

const {
    WebConfigTargetPolicy,
    createWebConfigTargetPolicyFromEnvironment,
    loadWebConfigTargetPolicy
} = require('../src/webconfig-target-policy');

test('signed product and PCB identity selects a local profile path', () => {
    const policy = new WebConfigTargetPolicy();
    const target = policy.resolve({
        productId: 'HBOX',
        hardwareVersion: '2.0.0'
    });
    assert.deepEqual(target, {
        productId: 'HBOX',
        pcbRevision: '2.0.0',
        profile: 'hbox-pcb-v2',
        basePath: '/webconfig/hbox-pcb-v2/'
    });
    assert.deepEqual(policy.profileSlugs, ['hbox-pcb-v2']);
});

test('unknown product and PCB revisions fail closed', () => {
    const policy = new WebConfigTargetPolicy();
    assert.throws(
        () => policy.resolve({
            productId: 'FAKE',
            hardwareVersion: '2.0.0'
        }),
        error => error.code === 'PRODUCT_NOT_SUPPORTED' &&
            error.status === 403
    );
    assert.throws(
        () => policy.resolve({
            productId: 'HBOX',
            hardwareVersion: '3.0.0'
        }),
        error => error.code === 'PCB_REVISION_NOT_SUPPORTED' &&
            error.status === 403
    );
});

test('profile paths are derived from strictly validated local slugs', () => {
    assert.throws(
        () => loadWebConfigTargetPolicy({
            WEB_CONFIG_TARGET_POLICY_JSON: JSON.stringify({
                schemaVersion: 1,
                productId: 'HBOX',
                pcbRevisions: {
                    '3.0.0': { profile: 'https://evil.example' }
                }
            })
        }),
        /target.*invalid/i
    );

    const policy = createWebConfigTargetPolicyFromEnvironment({
        WEB_CONFIG_TARGET_POLICY_JSON: JSON.stringify({
            schemaVersion: 1,
            productId: 'HBOX',
            pcbRevisions: {
                '3.0.0': { profile: 'hbox-pcb-v3' }
            }
        })
    });
    assert.equal(
        policy.resolve({
            productId: 'HBOX',
            hardwareVersion: '3.0.0'
        }).basePath,
        '/webconfig/hbox-pcb-v3/'
    );
});

test('production policy files require an absolute path', () => {
    assert.throws(
        () => loadWebConfigTargetPolicy({
            NODE_ENV: 'production',
            WEB_CONFIG_TARGET_POLICY_FILE: 'relative-policy.json'
        }),
        /absolute path/
    );
});
