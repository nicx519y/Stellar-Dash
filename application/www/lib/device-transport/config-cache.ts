import { CONFIG_CACHE_FORMAT, contentChecksum, isDigest, isRecord, resourceBodies, type ConfigManifest } from './config-modules';

export interface CachedConfigModule { version: string; data: unknown; checksum: string }
export type CachedConfigModules = Record<string, CachedConfigModule>;
export interface ConfigCacheStorage {
  read(key: string): Promise<unknown>;
  write(key: string, modules: CachedConfigModules, signal?: AbortSignal): Promise<void>;
}

/** Each snapshot is one transaction; failures/timeouts are only cache misses. */
export class IndexedDbConfigCache implements ConfigCacheStorage {
  private operation<T>(mode: IDBTransactionMode, key: string, value?: CachedConfigModules, signal?: AbortSignal): Promise<T | undefined> {
    return new Promise(resolve => {
      if (typeof indexedDB === 'undefined' || signal?.aborted) { resolve(undefined); return; }
      let db: IDBDatabase | undefined;
      let transaction: IDBTransaction | undefined;
      let settled = false;
      const finish = (result?: T) => {
        if (settled) return;
        settled = true; clearTimeout(timer); signal?.removeEventListener('abort', abort);
        db?.close(); resolve(result);
      };
      const abort = () => { try { transaction?.abort(); } catch { /* already completed */ } finish(); };
      const timer = setTimeout(abort, 1_000);
      signal?.addEventListener('abort', abort, { once: true });
      try {
        const open = indexedDB.open('xora-config-cache', 1);
        open.onupgradeneeded = () => { open.result.createObjectStore('snapshots'); };
        open.onerror = () => finish();
        open.onblocked = () => finish();
        open.onsuccess = () => {
          db = open.result;
          if (settled || signal?.aborted) { db.close(); finish(); return; }
          db.onversionchange = () => db?.close();
          try {
            transaction = db.transaction('snapshots', mode);
            const store = transaction.objectStore('snapshots');
            const request = mode === 'readonly' ? store.get(key) : store.put(value, key);
            transaction.oncomplete = () => finish(request.result as T);
            transaction.onerror = transaction.onabort = () => finish();
          } catch { finish(); }
        };
      } catch { finish(); }
    });
  }
  read(key: string): Promise<unknown> { return this.operation('readonly', key); }
  async write(key: string, modules: CachedConfigModules, signal?: AbortSignal): Promise<void> { await this.operation('readwrite', key, modules, signal); }
}

export function configCacheKey(namespace: string, manifest: ConfigManifest): string {
  return JSON.stringify([namespace, manifest.deviceCacheKey, manifest.hardwareVersion, manifest.schemaVersion, CONFIG_CACHE_FORMAT]);
}
export async function cachedModuleValid(module: unknown): Promise<boolean> {
  try {
    return isRecord(module) && isDigest(module.version) && isDigest(module.checksum) &&
      module.data !== undefined && await contentChecksum(module.data) === module.checksum;
  } catch { return false; }
}
export async function makeCachedModule(version: string, data: unknown): Promise<CachedConfigModule> {
  // Preserve wire semantics and detach mutable responses/drafts before hashing.
  const body: unknown = JSON.parse(JSON.stringify(data));
  return { version, data: body, checksum: await contentChecksum(body) };
}

export class ConfigSyncCache {
  private active?: { key: string; modules: CachedConfigModules; controller: AbortController };
  private tail: Promise<void> = Promise.resolve();
  constructor(readonly namespace: string, readonly storage: ConfigCacheStorage = new IndexedDbConfigCache()) {}
  async load(manifest: ConfigManifest): Promise<CachedConfigModules> {
    try {
      const value = await this.storage.read(configCacheKey(this.namespace, manifest));
      return isRecord(value) ? value as CachedConfigModules : {};
    } catch { return {}; }
  }
  endSession(): void { this.active?.controller.abort(); this.active = undefined; }
  activate(manifest: ConfigManifest, modules: CachedConfigModules): void {
    this.endSession();
    this.active = { key: configCacheKey(this.namespace, manifest), modules: structuredClone(modules), controller: new AbortController() };
    this.persist();
  }
  private persist(): void {
    const active = this.active;
    if (!active) return;
    const snapshot = structuredClone(active.modules);
    this.tail = this.tail.catch(() => {}).then(async () => {
      if (!active.controller.signal.aborted) await this.storage.write(active.key, snapshot, active.controller.signal);
    }).catch(() => {});
  }
  invalidateForCommand(command: string, params: Record<string, unknown>): void {
    const active = this.active;
    if (!active) return;
    const id = String(params.profileId ?? params.pid ?? (isRecord(params.profileDetails) ? params.profileDetails.id : '') ?? '');
    const affected: Record<string, string[]> = {
      update_global_config: ['global', 'profile-list'], update_screen_control_config: ['screen-control'], update_hotkeys_config: ['hotkeys'],
      update_profile: [`profile:${id}`, `macros:${id}`, 'profile-list'], update_profile_macros: [`macros:${id}`], update_macro: [`macros:${id}`],
      switch_default_profile: ['global', 'profile-list'], start_manual_calibration: ['global'], stop_manual_calibration: ['global'],
    };
    const keys = affected[command] ?? ((command === 'import_all_config' || command === 'import_config_finish' || command === 'reset_config') ? Object.keys(active.modules) : []);
    if (!keys.length) return;
    for (const key of keys) delete active.modules[key];
    this.persist();
  }
  async observe(data: unknown, params: Record<string, unknown>): Promise<void> {
    const active = this.active;
    if (!active || !isRecord(data) || !isRecord(data.configVersions)) return;
    const versions = data.configVersions;
    const entries = await Promise.all(Object.entries(resourceBodies(data, params)).map(async ([key, body]) =>
      isDigest(versions[key]) ? [key, await makeCachedModule(versions[key], body)] as const : null));
    if (this.active !== active || active.controller.signal.aborted) return;
    for (const entry of entries) if (entry) active.modules[entry[0]] = entry[1];
    this.persist();
  }
  async settled(): Promise<void> { await this.tail; }
}
