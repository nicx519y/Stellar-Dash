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

/**
 * Resolve a browser pathname to a route understood by the settings UI.
 *
 * Both `/keys` and `/keys/` are accepted. Unknown paths intentionally fall
 * back to the global page so a static-host SPA fallback remains useful.
 */
export function routeFromPathname(pathname: string): AppRoute {
    const pathOnly = pathname.split(/[?#]/, 1)[0] ?? '';
    const candidate = pathOnly.replace(/^\/+|\/+$/g, '');

    return ROUTE_SET.has(candidate) ? candidate as AppRoute : 'global';
}

/** Return the canonical URL emitted by client-side history operations. */
export function pathnameForRoute(route: Route): string {
    return `/${route || 'global'}/`;
}

export function isCanonicalRoutePath(pathname: string, route: Route): boolean {
    return pathname === pathnameForRoute(route);
}

/**
 * Popstate must never push another history entry. Non-canonical or unknown
 * entries are replaced in place; canonical back/forward entries are untouched.
 */
export function popstateHistoryMode(pathname: string): 'none' | 'replace' {
    const route = routeFromPathname(pathname);
    return isCanonicalRoutePath(pathname, route) ? 'none' : 'replace';
}
