/** Remove snapshots left by older WebConfig versions. Current sessions never read/write them. */
export function discardPersistentConfigCache(): void {
  if (typeof indexedDB === 'undefined') return;
  try {
    // A blocked deletion stays pending until an older tab closes its connection.
    // It must not hold up the fresh device read or touch account/preferences storage.
    const request = indexedDB.deleteDatabase('xora-config-cache');
    request.onerror = () => { /* Storage may be unavailable; no cache is used. */ };
  } catch { /* Private browsing/storage restrictions do not block connection. */ }
}
