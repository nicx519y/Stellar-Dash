export const ROUTES = [
    'global',
    'keys',
    'lighting',
    'buttons-performance',
    'switch-marking',
    'firmware',
    'view-logs',
] as const;

export type AppRoute = (typeof ROUTES)[number];
export type Route = '' | AppRoute;

const ROUTE_SET = new Set<string>(ROUTES);

export function normalizeRouteBasePath(basePath = '/'): string {
    if (typeof basePath !== 'string' ||
        !/^\/(?:webconfig\/[a-z0-9-]+\/)?$/.test(basePath)) {
        return '/';
    }
    return basePath;
}

/**
 * Resolve a browser pathname to a route understood by the settings UI.
 *
 * Both `/keys` and `/keys/` are accepted. Unknown paths intentionally fall
 * back to the global page so a static-host SPA fallback remains useful.
 */
export function routeFromPathname(
    pathname: string,
    basePath = '/',
): AppRoute {
    const pathOnly = pathname.split(/[?#]/, 1)[0] ?? '';
    const normalizedBase = normalizeRouteBasePath(basePath);
    const normalizedPath = `/${pathOnly.replace(/^\/+|\/+$/g, '')}/`;
    if (!normalizedPath.startsWith(normalizedBase)) {
        return 'global';
    }
    const candidate = normalizedPath
        .slice(normalizedBase.length)
        .replace(/^\/+|\/+$/g, '');

    return ROUTE_SET.has(candidate) ? candidate as AppRoute : 'global';
}

/** Return the canonical URL emitted by client-side history operations. */
export function pathnameForRoute(route: Route, basePath = '/'): string {
    return `${normalizeRouteBasePath(basePath)}${route || 'global'}/`;
}

export function isCanonicalRoutePath(
    pathname: string,
    route: Route,
    basePath = '/',
): boolean {
    return pathname === pathnameForRoute(route, basePath);
}

/**
 * Popstate must never push another history entry. Non-canonical or unknown
 * entries are replaced in place; canonical back/forward entries are untouched.
 */
export function popstateHistoryMode(
    pathname: string,
    basePath = '/',
): 'none' | 'replace' {
    const route = routeFromPathname(pathname, basePath);
    return isCanonicalRoutePath(pathname, route, basePath)
        ? 'none'
        : 'replace';
}
