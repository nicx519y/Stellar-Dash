/** In-memory only. Acknowledgements never replace newer local edits. */
export type ConfigResources = Record<string, unknown>;
export interface ConfigDraftBackup { deviceId: string; draft: ConfigResources; confirmed: ConfigResources; uncertain: ConfigResources }
export const cloneConfig = <T>(value: T): T => structuredClone(value);
const object = (v: unknown): v is Record<string, unknown> => v !== null && typeof v === 'object' && !Array.isArray(v);
export function configEqual(a: unknown, b: unknown): boolean {
  if (Object.is(a, b)) return true;
  if (Array.isArray(a) && Array.isArray(b)) return a.length === b.length && a.every((v, i) => configEqual(v, b[i]));
  if (!object(a) || !object(b)) return false;
  const keys = Object.keys(a).filter(k => a[k] !== undefined);
  return keys.length === Object.keys(b).filter(k => b[k] !== undefined).length && keys.every(k => configEqual(a[k], b[k]));
}
export function mergeConfig<T>(base: T, patch: Partial<T>): T {
  if (!object(base) || !object(patch)) return cloneConfig(patch as T);
  const result = cloneConfig(base) as Record<string, unknown>;
  for (const [key, value] of Object.entries(patch)) {
    if (value !== undefined) result[key] = object(value) && object(result[key]) ? mergeConfig(result[key], value) : cloneConfig(value);
  }
  return result as T;
}
/** Overlay only fields edited between base and local onto the fresh device value. */
export function rebaseConfig(base: unknown, local: unknown, remote: unknown): unknown {
  if (configEqual(base, local)) return cloneConfig(remote);
  if (!object(base) || !object(local) || !object(remote)) return cloneConfig(local);
  const result = cloneConfig(remote);
  for (const key of new Set([...Object.keys(base), ...Object.keys(local)])) {
    if (configEqual(base[key], local[key])) continue;
    if (!(key in local)) delete result[key];
    else result[key] = rebaseConfig(base[key], local[key], remote[key]);
  }
  return result;
}
export function configDifferences(device: unknown, local: unknown, path = ''): string[] {
  if (configEqual(device, local)) return [];
  if ((object(device) && object(local)) || (Array.isArray(device) && Array.isArray(local))) {
    const left = device as Record<string, unknown>;
    const right = local as Record<string, unknown>;
    return [...new Set([...Object.keys(left), ...Object.keys(right)])].flatMap(key =>
      configDifferences(left[key], right[key], path ? `${path}.${key}` : key));
  }
  return [`${path}: ${JSON.stringify(device) ?? '—'} → ${JSON.stringify(local) ?? '—'}`];
}
export function restoredConfigDraft(backup: ConfigDraftBackup, current: ConfigResources): ConfigResources {
  const restored = cloneConfig(current);
  for (const key of Object.keys(current)) {
    if (!(key in backup.draft)) continue;
    const base = key in backup.uncertain
      ? rebaseConfig(backup.uncertain[key], backup.draft[key], current[key]) : current[key];
    restored[key] = rebaseConfig(backup.confirmed[key], backup.draft[key], base);
  }
  return restored;
}
export class SessionConfigStore {
  deviceId = '';
  generation = 0;
  draft: ConfigResources = {};
  confirmed: ConfigResources = {};
  private active = new Map<string, unknown>();
  private versions = new Map<string, number>();
  constructor(private readonly changed: () => void = () => {}) {}
  hydrate(deviceId: string, resources: ConfigResources): void {
    this.generation++; this.deviceId = deviceId;
    this.draft = cloneConfig(resources); this.confirmed = cloneConfig(resources);
    this.active.clear(); this.versions.clear(); this.changed();
  }
  get<T>(key: string): T { return this.draft[key] as T; }
  refresh(key: string, remote: unknown): void {
    this.draft = { ...this.draft, [key]: rebaseConfig(this.confirmed[key], this.draft[key], remote) };
    this.confirmed = { ...this.confirmed, [key]: cloneConfig(remote) };
    this.changed();
  }
  dirty(key: string): boolean {
    return !configEqual(this.draft[key], this.confirmed[key]) || (this.active.has(key) && !configEqual(this.draft[key], this.active.get(key)));
  }
  get dirtyKeys(): string[] { return Object.keys(this.draft).filter(key => this.dirty(key)); }
  set(key: string, value: unknown): void {
    if (!(key in this.draft)) throw new Error(`Unknown configuration resource: ${key}`);
    if (configEqual(this.draft[key], value)) return;
    this.draft = { ...this.draft, [key]: cloneConfig(value) };
    this.versions.set(key, (this.versions.get(key) ?? 0) + 1); this.changed();
  }
  patch<T>(key: string, patch: Partial<T>): void { this.set(key, mergeConfig(this.get<T>(key), patch)); }
  begin(key: string) {
    const sent = cloneConfig(this.draft[key]); this.active.set(key, sent);
    return { key, sent, generation: this.generation, revision: this.versions.get(key) ?? 0 };
  }
  acknowledge(ticket: ReturnType<SessionConfigStore['begin']>, remote: unknown): void {
    if (ticket.generation !== this.generation) return;
    const { key, sent } = ticket;
    this.confirmed = { ...this.confirmed, [key]: cloneConfig(remote) };
    this.draft = { ...this.draft, [key]: rebaseConfig(sent, this.draft[key], remote) };
    this.active.delete(key); this.changed();
  }
  fail(_ticket: ReturnType<SessionConfigStore['begin']>): void {
    // Retain the sent snapshot until retry/hydration: a timeout is not proof
    // that the device did not commit, including edits reverted during flight.
  }
  detach(): ConfigDraftBackup | null {
    const backup = this.dirtyKeys.length || this.active.size
      ? { deviceId: this.deviceId, draft: cloneConfig(this.draft), confirmed: cloneConfig(this.confirmed), uncertain: cloneConfig(Object.fromEntries(this.active)) } : null;
    this.generation++; this.active.clear(); return backup;
  }
  restore(backup: ConfigDraftBackup): void {
    if (backup.deviceId !== this.deviceId) throw new Error('Configuration belongs to another device');
    const restored = restoredConfigDraft(backup, this.confirmed);
    for (const key of Object.keys(restored)) this.set(key, restored[key]);
  }
}
