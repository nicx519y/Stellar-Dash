import { lighting as sources, mappings } from "../../../common/xora-factory-catalog.cjs";
import type { LightingSource, ResourceSource } from "../../../common/xora-resource-codec.cjs";
import { encode, digestInput } from "../../../common/xora-resource-codec.cjs";
import {
  verifyResource,
  fromHex,
  toHex,
  resourceKey,
  protectedResource,
  staticResource,
  type ResourceRef,
} from "./resources";

export async function compileResource(source: unknown) {
  const bytes = encode(source);
  bytes.set(
    new Uint8Array(
      await crypto.subtle.digest("SHA-256", digestInput(bytes) as BufferSource),
    ),
    32,
  );
  return bytes;
}
type CatalogEntry = {
  source: ResourceSource;
  catalogId?: string;
  cover?: { mime: string; hex: string };
  status: string;
  name: string;
  description: string;
};
const key = "xora.mock.resources.catalog.v1";
let fallback: CatalogEntry[] = [
  ...sources,
  ...mappings,
].map((source) => ({
  source,
  status: "published",
  name: source.name,
  description: source.description,
}));
function read(): CatalogEntry[] {
  try {
    return (
      JSON.parse(sessionStorage.getItem(key) || "null") ||
      structuredClone(fallback)
    );
  } catch {
    return structuredClone(fallback);
  }
}
function save(entries: CatalogEntry[]) {
  fallback = entries;
  try {
    sessionStorage.setItem(key, JSON.stringify(entries));
  } catch {}
}
const json = (data: unknown, status = 200) =>
  Response.json(
    status === 200
      ? { success: true, data }
      : { success: false, message: data },
    { status },
  );
export async function resourceFetch(
  input: RequestInfo | URL,
  init?: RequestInit,
): Promise<Response> {
  try {
    const url = new URL(
      typeof input === "string"
        ? input
        : input instanceof URL
          ? input.href
          : input.url,
      "http://localhost",
    );
    const admin = url.pathname.startsWith("/api/admin/");
    if (admin && typeof sessionStorage !== "undefined") {
      const auth = JSON.parse(
        sessionStorage.getItem("st-dash-ui-preview-user-session") || "null",
      );
      if (auth?.user?.role !== "admin")
        return json("Administrator required", 403);
    }
    const tail = url.pathname
        .replace(/^\/api\/(admin\/)?resources\/?/, "")
        .split("/")
        .map(decodeURIComponent),
      method = init?.method || "GET",
      entries: CatalogEntry[] = read();
    const rows = await Promise.all(
      entries.map(async (e) => {
        const bytes = await compileResource(e.source);
        return {
          catalogId: e.catalogId || e.source.resourceId,
          resourceId: e.source.resourceId,
          revision: e.source.revision,
          type: e.source.type,
          name: e.name,
          description: e.description,
          status: e.status,
          size: bytes.length,
          sha256: toHex(bytes.slice(32, 64)),
          source: e.source,
          hasImage: !!e.cover,
          previousResourceIds: entries
            .filter(
              (r) =>
                (r.catalogId || r.source.resourceId) ===
                (e.catalogId || e.source.resourceId),
            )
            .map((r) => r.source.resourceId),
        };
      }),
    );
    if (method === "GET") {
      const available = rows.filter((r) => admin || r.status === "published");
      if (!tail[0])
        return json({
          items: available.filter(
            (r) =>
              !url.searchParams.get("type") ||
              r.type === url.searchParams.get("type"),
          ),
        });
      if (tail[1] === "image") {
        const entry = entries.find(
          (e) =>
            (e.catalogId || e.source.resourceId) === tail[0] &&
            (admin || e.status === "published"),
        );
        return entry?.cover
          ? new Response(fromHex(entry.cover.hex) as BodyInit, {
              headers: { "Content-Type": entry.cover.mime },
            })
          : json("Cover not found", 404);
      }
      const item = available.find(
        (r) => r.catalogId === tail[0] && r.revision === Number(tail[2]),
      );
      if (!item) return json("Resource not found", 404);
      if (tail[3] === "download")
        return new Response((await compileResource(item.source)) as BodyInit);
      if (tail[3] === "source") return Response.json(item.source);
      return json(item);
    }
    if (!admin) return json("Administrator required", 403);
    if (tail[1] === "image" && method === "PUT") {
      const mime = new Headers(init?.headers).get("Content-Type") || "";
      const bytes = new Uint8Array(
        await new Response(init?.body).arrayBuffer(),
      );
      if (
        !["image/png", "image/jpeg", "image/webp"].includes(mime) ||
        bytes.length < 12 ||
        bytes.length > 2 * 1024 * 1024
      )
        return json("Invalid cover", 400);
      const matches = entries.filter(
        (e) => (e.catalogId || e.source.resourceId) === tail[0],
      );
      if (!matches.length) return json("Resource not found", 404);
      matches.forEach((e) => {
        e.cover = { mime, hex: toHex(bytes) };
      });
      save(entries);
      return json({ updated: true });
    }
    const body = typeof init?.body === "string" ? JSON.parse(init.body) : {};
    if (!tail[0] && method === "POST") {
      await compileResource(body.source);
      if (
        entries.some(
          (e) =>
            (e.source.resourceId === body.source.resourceId &&
              (body.source.type === "switch-mapping" ||
                e.source.revision >= body.source.revision)) ||
            (body.catalogId &&
              (e.catalogId || e.source.resourceId) === body.catalogId &&
              e.source.revision >= body.source.revision),
        )
      )
        return json("Revision must increase", 409);
      entries.push({
        source: body.source,
        catalogId: body.catalogId || body.source.resourceId,
        status: "draft",
        name: body.source.name,
        description: body.source.description,
      });
    } else if (tail[3] === "publish") {
      const selected = entries.find(
        (e) =>
          (e.catalogId || e.source.resourceId) === tail[0] &&
          e.source.revision === Number(tail[2]),
      );
      if (!selected) return json("Resource not found", 404);
      entries.forEach((e) => {
        if (
          (e.catalogId || e.source.resourceId) === tail[0] &&
          e.status === "published"
        )
          e.status = "withdrawn";
      });
      selected.status = "published";
    } else if (tail[1] === "unpublish")
      entries.forEach((e) => {
        if (
          (e.catalogId || e.source.resourceId) === tail[0] &&
          e.status === "published"
        )
          e.status = "withdrawn";
      });
    else if (method === "PATCH")
      entries.forEach((e) => {
        if ((e.catalogId || e.source.resourceId) === tail[0]) {
          e.name = body.name;
          e.description = body.description;
        }
      });
    else return json("Unsupported mock resource operation", 400);
    save(entries);
    return json({ updated: true });
  } catch (e) {
    return json(String(e), 400);
  }
}

type ResourceProfile = { id: string; lightingResources?: {keys: ResourceRef; ambient: ResourceRef}; ledsConfigs?: {ledsEffectStyle?: number; aroundLedEffectStyle?: number} };
export class MockResourceDevice {
  // Fault injection for the same partial-commit boundaries as the device.
  failConfigSave = false;
  failRemoval = false;
  state: {
    sources: LightingSource[];
    profiles: Record<string, { keys: ResourceRef; ambient: ResourceRef }>;
  } = { sources: structuredClone(sources), profiles: {} };
  private transfer = 0;
  private size = 0;
  private bytes: number[] = [];
  importProfile(profile: ResourceProfile) {
    const refs = profile.lightingResources || {
      keys: {
        resourceId:
          [
            "key-static",
            "key-breath",
            "key-star",
            "key-flow",
            "key-ripple",
            "key-transform",
          ][profile.ledsConfigs?.ledsEffectStyle ?? 0] || "key-static",
        revision: 1,
      },
      ambient: {
        resourceId:
          [
            "ambient-static",
            "ambient-breath",
            "ambient-quake",
            "ambient-meteor",
          ][profile.ledsConfigs?.aroundLedEffectStyle ?? 0] || "ambient-static",
        revision: 1,
      },
    };
    for (const [zone, type] of [
      ["keys", "key-lighting"],
      ["ambient", "ambient-lighting"],
    ] as const) {
      if (
        !refs[zone] ||
        !this.state.sources.some(
          (s) => s.type === type && resourceKey(s) === resourceKey(refs[zone]),
        )
      )
        throw Error(
          "Install the referenced lighting resource revision before importing this profile",
        );
    }
    this.state.profiles[profile.id] = structuredClone(refs);
    profile.lightingResources = structuredClone(refs);
  }
  async command(command: string, p: Record<string, unknown>, profiles: ResourceProfile[], active: string) {
    for (const profile of profiles)
      if (!this.state.profiles[profile.id])
        this.state.profiles[profile.id] = {
          keys: {
            resourceId:
              [
                "key-static",
                "key-breath",
                "key-star",
                "key-flow",
                "key-ripple",
                "key-transform",
              ][profile.ledsConfigs?.ledsEffectStyle ?? 0] || "key-static",
            revision: 1,
          },
          ambient: {
            resourceId:
              [
                "ambient-static",
                "ambient-breath",
                "ambient-quake",
                "ambient-meteor",
              ][profile.ledsConfigs?.aroundLedEffectStyle ?? 0] || "ambient-static",
            revision: 1,
          },
        };
    const compiled = await Promise.all(this.state.sources.map(compileResource)),
      used = compiled.reduce((n, b) => n + b.length, 0);
    const index = this.state.sources.findIndex(
      (s) => s.resourceId === p.resourceId && s.revision === p.revision,
    );
    if (command === "resources_list" || command === "resources_status")
      return {
        schemaVersion: 1,
        engineVersion: 1,
        storageReady: true,
        capacity: 20480,
        used,
        maxEntries: 32,
        limits: { 'key-lighting': 8, 'ambient-lighting': 8 },
        counts: Object.fromEntries(['key-lighting', 'ambient-lighting'].map(type => [type, this.state.sources.filter(s => s.type === type).length])),
        removeWithFallback: true,
        maxResourceBytes: 2048,
        received: this.bytes.length,
        transferId: this.transfer,
        activeProfileId: active,
        running: this.state.profiles[active],
        items: this.state.sources.map((s) => ({
          resourceId: s.resourceId,
          revision: s.revision,
          type: s.type,
          name: s.name,
          factorySupplied: sources.some(
            (f) => resourceKey(f) === resourceKey(s),
          ),
        })),
        profiles: Object.entries(this.state.profiles).map(
          ([profileId, refs]) => ({ profileId, ...refs }),
        ),
      };
    if (command === "resources_begin") {
      if (typeof p.size !== 'number' || !Number.isInteger(p.size) || p.size < 192 || p.size > 2048)
        throw Error("Invalid resource size");
      this.size = p.size;
      this.bytes = [];
      return { transferId: ++this.transfer };
    }
    if (
      ["resources_chunk", "resources_commit", "resources_abort"].includes(
        command,
      )
    ) {
      if (!this.size || p.transferId !== this.transfer)
        throw Error("No matching resource transfer");
      if (command === "resources_abort") {
        this.size = 0;
        this.bytes = [];
        return {};
      }
      if (command === "resources_chunk") {
        const chunk = fromHex(p.hex);
        const offset = p.offset;
        if (
          chunk.length > 256 ||
          typeof offset !== 'number' || !Number.isInteger(offset) ||
          offset < 0 ||
          offset > this.bytes.length ||
          offset + chunk.length > this.size
        )
          throw Error("Invalid chunk");
        if (offset < this.bytes.length) {
          if (
            offset + chunk.length > this.bytes.length ||
            chunk.some((b, i) => b !== this.bytes[offset + i])
          )
            throw Error("Conflicting chunk");
        } else this.bytes.push(...chunk);
        return { received: this.bytes.length };
      }
      if (this.bytes.length !== this.size) throw Error("Incomplete resource");
      const bytes = Uint8Array.from(this.bytes),
        source = await verifyResource(bytes);
      if (source.type === "switch-mapping")
        throw Error("Use mapping installer");
      const existing = this.state.sources.findIndex(
        (s) => resourceKey(s) === resourceKey(source),
      );
      if (existing >= 0) {
        if (toHex(compiled[existing]) !== toHex(bytes))
          throw Error("Immutable resource conflict");
      } else {
        if (this.state.sources.filter(s => s.type === source.type).length >= 8)
          throw Error("RESOURCE_COUNT_LIMIT");
        if (this.state.sources.length >= 32 || used + bytes.length > 20480)
          throw Error("RESOURCE_STORAGE_FULL");
        this.state.sources.push(source);
      }
      this.size = 0;
      this.bytes = [];
      return { ...source, sha256: toHex(bytes.slice(32, 64)), installed: true };
    }
    if (index < 0) throw Error("Resource not installed");
    if (command === "resources_get") return { hex: toHex(compiled[index]) };
    if (command === "resources_apply") {
      const refs = typeof p.profileId === 'string' ? this.state.profiles[p.profileId] : undefined;
      if (!refs) throw Error("Profile not found");
      refs[
        this.state.sources[index].type === "key-lighting" ? "keys" : "ambient"
      ] = { resourceId: this.state.sources[index].resourceId, revision: this.state.sources[index].revision };
      return { applied: true, runtimeReloaded: p.profileId === active };
    }
    if (command === "resources_remove") {
      const item = this.state.sources[index], zone = item.type === 'key-lighting' ? 'keys' : 'ambient';
      if (protectedResource(item)) return { removed: false, configurationSaved: false, errorCode: 'RESOURCE_PROTECTED' };
      const affectedProfiles = Object.entries(this.state.profiles).filter(([,refs]) => resourceKey(refs[zone]) === resourceKey(item)).map(([id]) => id);
      if (affectedProfiles.length && p.resetReferences !== true) throw Error('Resource in use');
      const fallback = staticResource(item.type);
      if (!this.state.sources.some(s => resourceKey(s) === resourceKey(fallback)))
        return { removed: false, configurationSaved: false, errorCode: 'RESOURCE_FALLBACK_UNAVAILABLE' };
      if (affectedProfiles.length && this.failConfigSave)
        return { removed: false, configurationSaved: false, errorCode: 'RESOURCE_CONFIG_SAVE_FAILED', affectedProfiles, fallback };
      for (const id of affectedProfiles) this.state.profiles[id][zone] = {...fallback};
      if (!this.failRemoval) this.state.sources.splice(index, 1);
      return { removed: !this.failRemoval, configurationSaved: true, runtimeReloaded: true, affectedProfiles, fallback,
        ...(this.failRemoval ? {errorCode:'RESOURCE_REMOVE_FAILED'} : {}) };
    }
    throw Error("Unknown resource command");
  }
}
