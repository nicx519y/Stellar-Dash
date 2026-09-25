import type { GameProfile, GameProfileList } from '../../types/gamepad-config';
import type { ConfigResources } from '../session-config-store';
import { readConfigSnapshot, type ConfigRequester } from './config-snapshot';
import { ConfigSyncCache, cachedModuleValid, makeCachedModule, type CachedConfigModules } from './config-cache';
import { decodeConfigResource, isDigest, isRecord, parseConfigManifest, resourceRequest, type ConfigManifest } from './config-modules';
import { DeviceTransportError } from './types';

export interface ConfigSyncProgress { completed: number; total: number; phase?: 'checking' | 'reading' | 'complete' }
export interface ConfigSyncResult { resources: ConfigResources; manifest?: ConfigManifest; modules?: CachedConfigModules }
export async function readIncrementalConfigSnapshot(
  request: ConfigRequester,
  convert: (profile: GameProfile) => GameProfile | null | undefined,
  cache: ConfigSyncCache,
  progress: (value: ConfigSyncProgress) => void = () => {},
  isCurrent: () => boolean = () => true,
): Promise<ConfigSyncResult> {
  const check = () => { if (!isCurrent()) throw new Error('Configuration synchronization cancelled'); };
  const read: ConfigRequester = async (command, params) => { check(); const result = await request(command, params); check(); return result; };
  progress({ completed: 0, total: 0, phase: 'checking' });
  let manifest: ConfigManifest;
  try { manifest = parseConfigManifest(await read('get_config_manifest')); }
  catch (error) {
    // Only a correlated device "unknown command" reply permits legacy fallback.
    const cause = error instanceof DeviceTransportError ? error.cause : undefined;
    if (!(error instanceof DeviceTransportError) || error.code !== 'protocol' || !isRecord(cause) ||
        cause.command !== 'get_config_manifest' || ![404, -1].includes(cause.errNo as number) || error.message !== 'Unknown command') throw error;
    const resources = await readConfigSnapshot(read, convert, (completed, total) => progress({ completed, total, phase: 'reading' }));
    return { resources };
  }
  const initial = manifest;
  const loaded = await cache.load(manifest);
  check();
  const modules: CachedConfigModules = {};
  for (const key of Object.keys(manifest.modules)) {
    const value = loaded[key];
    if (value?.version !== manifest.modules[key] || !await cachedModuleValid(value)) continue;
    try { decodeConfigResource(key, value.data, convert); modules[key] = value; } catch { /* reread malformed cached data */ }
  }
  for (let round = 0; round < 3; round++) {
    check();
    const stale = Object.keys(manifest.modules).filter(key => modules[key]?.version !== manifest.modules[key]);
    const ordered = ['profile-list', ...stale.filter(key => key !== 'profile-list')].filter(key => stale.includes(key));
    let completed = 0;
    progress({ completed, total: ordered.length, phase: ordered.length ? 'reading' : 'checking' });
    for (const key of ordered) {
      const spec = resourceRequest(key);
      const response = await read(spec.command, spec.params);
      const versions = response?.configVersions;
      const version = isRecord(versions) ? versions[key] : undefined;
      if (!isDigest(version)) throw new Error(`Missing configuration response version: ${key}`);
      const body = response?.[spec.field];
      decodeConfigResource(key, body, convert);
      modules[key] = await makeCachedModule(version, body);
      check();
      progress({ completed: ++completed, total: ordered.length, phase: 'reading' });
    }
    progress({ completed, total: ordered.length, phase: 'checking' });
    const latest = parseConfigManifest(await read('get_config_manifest'));
    if (latest.deviceCacheKey !== initial.deviceCacheKey || latest.hardwareVersion !== initial.hardwareVersion || latest.schemaVersion !== initial.schemaVersion) throw new Error('Configuration device changed during synchronization');
    manifest = latest;
    for (const key of Object.keys(modules)) if (!(key in manifest.modules)) delete modules[key];
    if (!Object.entries(manifest.modules).every(([key, version]) => modules[key]?.version === version)) continue;
    const resources: ConfigResources = {};
    for (const [key, module] of Object.entries(modules)) resources[key] = decodeConfigResource(key, module.data, convert);
    const list = resources['profile-list'] as GameProfileList;
    const expected = ['global', 'screen-control', 'hotkeys', 'profile-list', ...list.items.flatMap(p => [`profile:${p.id}`, `macros:${p.id}`])];
    if (expected.length !== Object.keys(resources).length || expected.some(key => !(key in resources))) throw new Error('Configuration manifest does not match profile slots');
    resources['selected-profile'] = list.defaultId;
    check();
    progress({ completed: ordered.length, total: ordered.length, phase: 'complete' });
    return { resources, manifest, modules };
  }
  throw new Error('Device configuration kept changing during synchronization');
}
