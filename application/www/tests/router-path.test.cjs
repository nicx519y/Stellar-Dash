const test = require('node:test');
const assert = require('node:assert/strict');

const {
    ROUTES,
    isCanonicalRoutePath,
    normalizeRouteBasePath,
    pathnameForRoute,
    popstateHistoryMode,
    routeFromPathname,
} = require('../lib/router-path.ts');

test('all exported settings routes resolve with or without a trailing slash', () => {
    for (const route of ROUTES) {
        assert.equal(routeFromPathname(`/${route}`), route);
        assert.equal(routeFromPathname(`/${route}/`), route);
        assert.equal(routeFromPathname(`///${route}///`), route);
    }
});

test('route URLs written to browser history always have a trailing slash', () => {
    for (const route of ROUTES) {
        assert.equal(pathnameForRoute(route), `/${route}/`);
        assert.equal(isCanonicalRoutePath(`/${route}/`, route), true);
        assert.equal(isCanonicalRoutePath(`/${route}`, route), false);
    }

    assert.equal(pathnameForRoute(''), '/global/');
});

test('root and unknown browser paths safely fall back to global', () => {
    for (const pathname of ['/', '', '/unknown', '/keys/extra/', '/?from=test']) {
        assert.equal(routeFromPathname(pathname), 'global');
    }
});

test('back and forward navigation never push another history entry', () => {
    assert.equal(popstateHistoryMode('/keys/'), 'none');
    assert.equal(popstateHistoryMode('/firmware/'), 'none');

    // A refresh on an old un-slashed URL is canonicalized in place.
    assert.equal(popstateHistoryMode('/keys'), 'replace');
    // An SPA fallback for an unknown path is also replaced in place.
    assert.equal(popstateHistoryMode('/unknown'), 'replace');
});

test('route parsing ignores URL query and hash fragments in test inputs', () => {
    assert.equal(routeFromPathname('/keys/?tab=primary'), 'keys');
    assert.equal(routeFromPathname('/firmware/#update'), 'firmware');
});

test('authenticated profiles use a local namespaced route only', () => {
    const basePath = '/webconfig/hbox-pcb-v2/';
    assert.equal(
        routeFromPathname('/webconfig/hbox-pcb-v2/keys/', basePath),
        'keys'
    );
    assert.equal(pathnameForRoute('firmware', basePath),
        '/webconfig/hbox-pcb-v2/firmware/');
    assert.equal(
        popstateHistoryMode('/webconfig/hbox-pcb-v2/keys/', basePath),
        'none'
    );
    assert.equal(
        routeFromPathname('/webconfig/another-profile/keys/', basePath),
        'global'
    );
    assert.equal(normalizeRouteBasePath('https://evil.example/'), '/');
    assert.equal(normalizeRouteBasePath('/webconfig/../keys/'), '/');
});
