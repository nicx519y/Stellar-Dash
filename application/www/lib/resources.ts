import { decode, digestInput } from "../../../common/xora-resource-codec.cjs";
import type { ResourceSource, MappingPayload } from "../../../common/xora-resource-codec.cjs";
export type { ResourceSource } from "../../../common/xora-resource-codec.cjs";
export type ResourceType =
  "switch-mapping" | "key-lighting" | "ambient-lighting";
export type ResourceRef = { resourceId: string; revision: number };
export type ResourceItem = ResourceRef & {
  catalogId: string;
  type: ResourceType;
  name: string;
  description: string;
  status: "draft" | "published" | "withdrawn";
  size: number;
  sha256: string;
  source: ResourceSource;
  validationError?: string;
  hasImage?: boolean;
  mapping?: MappingPayload;
  mappingSha256?: string;
  previousResourceIds?: string[];
};
export type InstalledResource = ResourceRef & {
  type: ResourceType;
  name: string;
  factorySupplied?: boolean;
};
export type ResourceInventory = {
  schemaVersion: number;
  engineVersion: number;
  storageReady: boolean;
  capacity: number;
  used: number;
  maxEntries: number;
  limits?: Partial<Record<ResourceType, number>>;
  counts?: Partial<Record<ResourceType, number>>;
  removeWithFallback?: boolean;
  items: InstalledResource[];
  profiles: { profileId: string; keys: ResourceRef; ambient: ResourceRef }[];
};
export type ResourceSender = (
  command: string,
  params?: Record<string, unknown>,
) => Promise<Record<string, unknown>>;
export const resourceKey = (r: ResourceRef) => `${r.resourceId}:${r.revision}`;
export const staticResource = (type: ResourceType): ResourceRef => ({resourceId: type === 'ambient-lighting' ? 'ambient-static' : 'key-static', revision: 1});
export const protectedResource = (r: ResourceRef) => r.revision === 1 && ['key-static', 'ambient-static'].includes(r.resourceId);
export const toHex = (bytes: Uint8Array) =>
  Array.from(bytes, (b) => b.toString(16).padStart(2, "0")).join("");
export function fromHex(hex: unknown) {
  if (typeof hex !== 'string' || !/^(?:[a-f0-9]{2})+$/i.test(hex))
    throw new Error("Invalid resource bytes");
  return Uint8Array.from(hex.match(/../g)!, (s) => parseInt(s, 16));
}
export async function verifyResource(bytes: Uint8Array) {
  const source = decode(bytes);
  const hash = await crypto.subtle.digest(
    "SHA-256",
    digestInput(bytes) as BufferSource,
  );
  if (toHex(new Uint8Array(hash)) !== toHex(bytes.subarray(32, 64)))
    throw new Error("Resource SHA-256 mismatch");
  return source;
}
export async function resourceJSON<T>(response: Response): Promise<T> {
  const body = await response.json();
  if (!response.ok || !body.success)
    throw new Error(body.message || `HTTP ${response.status}`);
  return body.data;
}
export async function installLighting(
  send: ResourceSender,
  item: ResourceRef,
  bytes: Uint8Array,
  progress: (n: number) => void = () => {},
) {
  const source = await verifyResource(bytes);
  if (
    source.type === "switch-mapping" ||
    source.resourceId !== item.resourceId ||
    source.revision !== item.revision
  )
    throw new Error("Resource identity mismatch");
  const { transferId } = await send("resources_begin", { size: bytes.length });
  if (typeof transferId !== 'number' || !Number.isInteger(transferId) || transferId <= 0) throw new Error('Invalid resource transfer ID');
  try {
    for (let offset = 0; offset < bytes.length; offset += 256) {
      const chunk = bytes.subarray(offset, offset + 256);
      const result = await send("resources_chunk", {
        transferId,
        offset,
        hex: toHex(chunk),
      });
      if (result.received !== offset + chunk.length)
        throw new Error("Resource transfer offset mismatch");
      progress(Math.round(((offset + chunk.length) / bytes.length) * 100));
    }
    const result = await send("resources_commit", { transferId });
    if (
      !result.installed ||
      result.resourceId !== item.resourceId ||
      result.revision !== item.revision ||
      result.sha256 !== toHex(bytes.subarray(32, 64))
    )
      throw new Error("Device did not confirm installation");
    const readback = await send("resources_get", item);
    const confirmed = fromHex(readback.hex);
    await verifyResource(confirmed);
    if (toHex(confirmed) !== toHex(bytes))
      throw new Error("Resource readback mismatch");
  } catch (error) {
    await send("resources_abort", { transferId }).catch(() => {});
    throw error;
  }
}
