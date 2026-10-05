"use strict";
const crypto = require("node:crypto");
const fs = require("node:fs");
const path = require("node:path");
const express = require("express");
const codec = require("../../common/xora-resource-codec.cjs");
const { normalizeSwitchImage } = require("./switch-mappings");
const compatibility = {
  productId: "HBOX",
  pcbRevision: "2.0.0",
  hardwareVersion: "2.0.0",
};
function compile(source) {
  const b = codec.encode(source);
  b.set(crypto.createHash("sha256").update(codec.digestInput(b)).digest(), 32);
  return Buffer.from(b);
}
function error(message, status = 400) {
  return Object.assign(new Error(message), { status });
}
class ResourceStore {
  constructor(mappings, { seed = true } = {}) {
    this.mappings = mappings;
    this.db = mappings.database;
    this.db
      .exec(`CREATE TABLE IF NOT EXISTS resource_catalogs(id TEXT PRIMARY KEY,type TEXT NOT NULL,name TEXT NOT NULL,description TEXT NOT NULL,published INTEGER,image BLOB,image_mime TEXT);
   CREATE TABLE IF NOT EXISTS resource_revisions(id TEXT NOT NULL,revision INTEGER NOT NULL,source TEXT NOT NULL,asset BLOB NOT NULL,published_at TEXT,PRIMARY KEY(id,revision),FOREIGN KEY(id) REFERENCES resource_catalogs(id));`);
    if (seed) {
      const dir = path.join(__dirname, "../../resources/xora");
      for (const file of fs
        .readdirSync(dir)
        .filter((f) => f.endsWith(".xora-resource.json"))) {
        const source = JSON.parse(
          fs.readFileSync(path.join(dir, file), "utf8"),
        );
        if (
          source.type === "switch-mapping" ||
          this.db
            .prepare("SELECT 1 FROM resource_catalogs WHERE id=?")
            .get(source.resourceId)
        )
          continue;
        this.upload(source);
        this.publish(source.resourceId, source.revision);
      }
    }
  }
  mappingSource(catalog, revision) {
    return {
      schemaVersion: 1,
      engineVersion: 1,
      resourceId: revision.revisionId,
      revision: revision.revision,
      type: "switch-mapping",
      name: catalog.displayName,
      description: catalog.description,
      compatibility: { hardwareVersion: catalog.hardwareVersion },
      payload: { ...revision.mapping, id: undefined },
    };
  }
  mappingItems(admin) {
    return this.mappings
      .listAdmin()
      .filter((c) => !c.archived)
      .flatMap((c) => {
        const rows = this.db
          .prepare(
            "SELECT revision_id FROM switch_mapping_revisions WHERE catalog_id=? ORDER BY revision DESC",
          )
          .all(c.catalogId);
        return rows.flatMap((row) => {
          const d = this.mappings.getAdminCatalog(c.catalogId, row.revision_id);
          const source = this.mappingSource(c, d.revision);
          let asset = null,
            validationError = null;
          try {
            asset = compile(source);
          } catch (e) {
            validationError = e.message;
          }
          const published = c.publishedRevisionId === row.revision_id;
          if (!admin && (!published || !asset)) return [];
          return [
            {
              catalogId: c.catalogId,
              resourceId: row.revision_id,
              revision: d.revision.revision,
              type: "switch-mapping",
              name: c.displayName,
              description: c.description,
              status: published
                ? "published"
                : d.revision.publishedAt
                  ? "withdrawn"
                  : "draft",
              size: asset?.length || 0,
              sha256: asset?.subarray(32, 64).toString("hex") || null,
              source,
              validationError,
              hasImage: c.hasImage,
              mapping: d.revision.mapping,
              mappingSha256: d.revision.sha256,
              previousResourceIds: rows.map((row) => row.revision_id),
            },
          ];
        });
      });
  }
  list(admin = false) {
    const lights = this.db
      .prepare(
        `SELECT c.*,r.revision,r.source,r.asset,r.published_at FROM resource_catalogs c JOIN resource_revisions r ON c.id=r.id ${admin ? "" : "WHERE c.published=r.revision"} ORDER BY c.name,r.revision DESC`,
      )
      .all()
      .map((r) => ({
        catalogId: r.id,
        resourceId: r.id,
        revision: r.revision,
        type: r.type,
        name: r.name,
        description: r.description,
        status:
          r.published === r.revision
            ? "published"
            : r.published_at
              ? "withdrawn"
              : "draft",
        size: r.asset.length,
        sha256: r.asset.subarray(32, 64).toString("hex"),
        hasImage: !!r.image,
        source: JSON.parse(r.source),
      }));
    return [...this.mappingItems(admin), ...lights];
  }
  get(id, revision, admin = false) {
    return (
      this.list(admin).find(
        (r) => r.catalogId === id && r.revision === Number(revision),
      ) || null
    );
  }
  asset(item) {
    return item.type === "switch-mapping"
      ? compile(item.source)
      : this.db
          .prepare(
            "SELECT asset FROM resource_revisions WHERE id=? AND revision=?",
          )
          .get(item.catalogId, item.revision).asset;
  }
  upload(source, catalogId = null, actor = "admin") {
    codec.validateSource(source);
    const asset = compile(source);
    if (source.type === "switch-mapping") {
      const draft = this.mappings.createDraft({
        catalogId,
        metadata: { displayName: source.name, description: source.description },
        compatibility,
        mapping: source.payload,
        actor,
        resourceIdentity: { id: source.resourceId, revision: source.revision },
      });
      return this.get(draft.catalogId, draft.revision.revision, true);
    }
    return this.db.transaction(() => {
      const existing = this.db
        .prepare("SELECT * FROM resource_catalogs WHERE id=?")
        .get(source.resourceId);
      if (existing && existing.type !== source.type)
        throw error("Resource type cannot change", 409);
      const latest =
        this.db
          .prepare(
            "SELECT MAX(revision) AS revision FROM resource_revisions WHERE id=?",
          )
          .get(source.resourceId).revision || 0;
      if (source.revision <= latest)
        throw error(
          "Upload a new, higher revision; existing revisions are immutable",
          409,
        );
      this.db
        .prepare(
          "INSERT INTO resource_catalogs(id,type,name,description) VALUES(?,?,?,?) ON CONFLICT(id) DO NOTHING",
        )
        .run(source.resourceId, source.type, source.name, source.description);
      this.db
        .prepare(
          "INSERT INTO resource_revisions(id,revision,source,asset) VALUES(?,?,?,?)",
        )
        .run(source.resourceId, source.revision, JSON.stringify(source), asset);
      return this.get(source.resourceId, source.revision, true);
    })();
  }
  publish(id, revision) {
    const item = this.get(id, revision, true);
    if (!item) throw error("Resource not found", 404);
    compile(item.source);
    if (item.type === "switch-mapping")
      this.mappings.publish(id, item.resourceId);
    else
      this.db.transaction(() => {
        this.db
          .prepare(
            "UPDATE resource_revisions SET published_at=COALESCE(published_at,?) WHERE id=? AND revision=?",
          )
          .run(new Date().toISOString(), id, revision);
        this.db
          .prepare("UPDATE resource_catalogs SET published=? WHERE id=?")
          .run(revision, id);
      })();
    return this.get(id, revision, true);
  }
  unpublish(id) {
    if (this.mappings.catalog(id)) this.mappings.unpublish(id);
    else if (
      !this.db
        .prepare("UPDATE resource_catalogs SET published=NULL WHERE id=?")
        .run(id).changes
    )
      throw error("Resource not found", 404);
  }
  metadata(id, body) {
    if (
      typeof body.name !== "string" ||
      !body.name.trim() ||
      body.name.length > 80 ||
      typeof body.description !== "string" ||
      body.description.length > 500
    )
      throw error("Invalid resource metadata");
    if (this.mappings.catalog(id))
      this.mappings.updateMetadata(id, {
        displayName: body.name.trim(),
        description: body.description,
      });
    else if (
      !this.db
        .prepare("UPDATE resource_catalogs SET name=?,description=? WHERE id=?")
        .run(body.name.trim(), body.description, id).changes
    )
      throw error("Resource not found", 404);
  }
  image(id) {
    if (this.mappings.catalog(id))
      return this.mappings.adminImage(id, compatibility);
    const row = this.db
      .prepare("SELECT image,image_mime FROM resource_catalogs WHERE id=?")
      .get(id);
    return row?.image ? { data: row.image, mimeType: row.image_mime } : null;
  }
  setImage(id, mime, bytes) {
    const image = normalizeSwitchImage(mime, bytes);
    if (this.mappings.catalog(id)) this.mappings.updateImage(id, mime, bytes);
    else if (
      !this.db
        .prepare("UPDATE resource_catalogs SET image=?,image_mime=? WHERE id=?")
        .run(image.data, image.mimeType, id).changes
    )
      throw error("Resource not found", 404);
  }
}
function initResourceRoutes(app, { store, adminAccess, deviceAuth }) {
  const admin = adminAccess.requireAdmin({ humanOnly: true }),
    read = deviceAuth.requireSession(["config.read"]);
  const wrap = (fn) => (req, res) => {
    res.set("Cache-Control", "private, no-store");
    try {
      fn(req, res);
    } catch (e) {
      res.status(e.status || 400).json({ success: false, message: e.message });
    }
  };
  const send = (res, data) => res.json({ success: true, data });
  for (const [prefix, gate, isAdmin] of [
    ["/api/resources", read, false],
    ["/api/admin/resources", admin, true],
  ]) {
    app.get(
      prefix,
      gate,
      wrap((req, res) =>
        send(res, {
          items: store
            .list(isAdmin)
            .filter((r) => !req.query.type || r.type === req.query.type),
        }),
      ),
    );
    app.get(
      `${prefix}/:id/revisions/:revision`,
      gate,
      wrap((req, res) => {
        const item = store.get(req.params.id, req.params.revision, isAdmin);
        if (!item) throw error("Resource not found", 404);
        send(res, item);
      }),
    );
    app.get(
      `${prefix}/:id/revisions/:revision/:format`,
      gate,
      wrap((req, res) => {
        const item = store.get(req.params.id, req.params.revision, isAdmin);
        if (!item) throw error("Resource not found", 404);
        if (req.params.format === "source") {
          res
            .type("application/json")
            .attachment(`${item.resourceId}.xora-resource.json`)
            .send(JSON.stringify(item.source, null, 2));
        } else if (req.params.format === "download") {
          res
            .type("application/octet-stream")
            .attachment(`${item.resourceId}.xora-resource`)
            .send(store.asset(item));
        } else throw error("Unknown format", 404);
      }),
    );
    app.get(
      `${prefix}/:id/image`,
      gate,
      wrap((req, res) => {
        if (!store.list(isAdmin).some((r) => r.catalogId === req.params.id))
          throw error("Resource not found", 404);
        const image = store.image(req.params.id);
        if (!image) throw error("Image not found", 404);
        res.type(image.mimeType).send(image.data);
      }),
    );
  }
  app.post(
    "/api/admin/resources",
    admin,
    wrap((req, res) =>
      send(
        res,
        store.upload(
          req.body.source,
          req.body.catalogId,
          req.authenticatedAdmin?.username,
        ),
      ),
    ),
  );
  app.post(
    "/api/admin/resources/:id/revisions/:revision/publish",
    admin,
    wrap((req, res) =>
      send(res, store.publish(req.params.id, Number(req.params.revision))),
    ),
  );
  app.post(
    "/api/admin/resources/:id/unpublish",
    admin,
    wrap((req, res) => {
      store.unpublish(req.params.id);
      send(res, { withdrawn: true });
    }),
  );
  app.patch(
    "/api/admin/resources/:id",
    admin,
    wrap((req, res) => {
      store.metadata(req.params.id, req.body);
      send(res, { updated: true });
    }),
  );
  app.put(
    "/api/admin/resources/:id/image",
    admin,
    express.raw({
      type: ["image/png", "image/jpeg", "image/webp"],
      limit: "2mb",
    }),
    wrap((req, res) => {
      store.setImage(req.params.id, req.headers["content-type"], req.body);
      send(res, { updated: true });
    }),
  );
}
module.exports = { ResourceStore, initResourceRoutes, compile };
