/*
 * Localhost-only WebHID Network-panel mirror. A WebConfig page registers this
 * worker when its same-origin trace viewer is open, or when an explicit local
 * webhidNetworkTrace query is present. It never forwards trace records to the
 * WebConfig host or the device.
 */

const TRACE_ENDPOINT_PREFIX = '/__hbox_webhid_trace__/';

self.addEventListener('install', () => {
  self.skipWaiting();
});

self.addEventListener('activate', (event) => {
  event.waitUntil(self.clients.claim());
});

self.addEventListener('fetch', (event) => {
  const request = event.request;
  const url = new URL(request.url);
  if (
    request.method !== 'POST' ||
    url.origin !== self.location.origin ||
    !url.pathname.startsWith(TRACE_ENDPOINT_PREFIX)
  ) {
    return;
  }

  event.respondWith((async () => {
    const body = await request.clone().text();
    return new Response(body, {
      status: 200,
      headers: {
        'Content-Type': 'application/json; charset=utf-8',
        'Cache-Control': 'no-store',
        'X-HBox-WebHID-Trace': 'local-service-worker',
      },
    });
  })());
});
