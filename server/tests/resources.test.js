const test = require("node:test"),
  assert = require("node:assert/strict"),
  crypto = require("node:crypto");
const { SwitchMappingStore } = require("../src/switch-mappings");
const {
  ResourceStore,
  initResourceRoutes,
  compile,
} = require("../src/resources");
const codec = require("../../common/xora-resource-codec.cjs");
const source = require("../../resources/xora/key-ripple.xora-resource.json");
test("axis files preserve exact IDs, revisions and compiled bytes in the existing catalog", () => {
  const mappings = new SwitchMappingStore({ databasePath: ":memory:" });
  try {
    const store = new ResourceStore(mappings, { seed: false });
    const axis = require("../../resources/xora/factory-axis.xora-resource.json");
    const first = store.upload(axis);
    assert.deepEqual(store.asset(first), compile(axis));
    store.publish(first.catalogId, first.revision);
    const next = { ...axis, resourceId: "axis-next", revision: 2 };
    const second = store.upload(next, first.catalogId);
    store.publish(second.catalogId, second.revision);
    assert.deepEqual(store.asset(second), compile(next));
    assert.deepEqual(store.list()[0].previousResourceIds, [
      "axis-next",
      "factory-axis",
    ]);
    assert.throws(
      () => store.upload({ ...next, revision: 3 }, first.catalogId),
      /distinct resource ID/,
    );
    assert.equal(mappings.listAdmin().length, 1);
  } finally {
    mappings.close();
  }
});
test("resource compiler is deterministic and rejects malformed/unsupported resources", () => {
  const b = compile(source);
  assert.deepEqual(b, compile(JSON.parse(JSON.stringify(source))));
  assert.equal(codec.decode(b).resourceId, source.resourceId);
  assert.equal(
    b.subarray(32, 64).toString("hex"),
    crypto.createHash("sha256").update(codec.digestInput(b)).digest("hex"),
  );
  for (const changed of [
    { ...source, engineVersion: 2 },
    { ...source, revision: 0 },
    {
      ...source,
      payload: {
        ...source.payload,
        layers: Array(9).fill(source.payload.layers[0]),
      },
    },
  ])
    assert.throws(() => compile(changed));
  const bad = Buffer.from(b);
  bad[79] = 1;
  assert.throws(() => codec.decode(bad));
});

test("HTTP catalog separates administrator drafts from downloadable revisions", async (t) => {
  const express = require("express");
  const mappings = new SwitchMappingStore({ databasePath: ":memory:" });
  const store = new ResourceStore(mappings, { seed: false });
  const app = express();
  app.use(express.json());
  initResourceRoutes(app, {
    store,
    adminAccess: {
      requireAdmin(options) {
        assert.equal(options.humanOnly, true);
        return (req, res, next) =>
          req.get("X-Test-Role") === "admin" ? next() : res.sendStatus(403);
      },
    },
    deviceAuth: {
      requireSession(scopes) {
        assert.deepEqual(scopes, ["config.read"]);
        return (req, res, next) =>
          req.get("X-Test-Role") ? next() : res.sendStatus(401);
      },
    },
  });
  const server = app.listen(0, "127.0.0.1");
  await new Promise((resolve) => server.once("listening", resolve));
  t.after(async () => {
    await new Promise((resolve) => server.close(resolve));
    mappings.close();
  });
  const url = `http://127.0.0.1:${server.address().port}`;
  const call = (route, role = "user", method = "GET", body) =>
    fetch(url + route, {
      method,
      headers: {
        ...(role ? { "X-Test-Role": role } : {}),
        "Content-Type": "application/json",
      },
      ...(body ? { body: JSON.stringify(body) } : {}),
    });
  assert.equal((await call("/api/resources", null)).status, 401);
  assert.equal((await call("/api/admin/resources")).status, 403);
  assert.equal(
    (await call("/api/admin/resources", "user", "POST", { source })).status,
    403,
  );
  assert.equal(
    (await call("/api/admin/resources", "admin", "POST", { source })).status,
    200,
  );
  assert.equal(
    (await (await call("/api/resources")).json()).data.items.length,
    0,
  );
  const path = `/resources/${source.resourceId}/revisions/1`;
  assert.equal((await call(`/api${path}/download`)).status, 404);
  assert.equal(
    (await call(`/api/admin${path}/publish`, "admin", "POST")).status,
    200,
  );
  const downloaded = Buffer.from(
    await (await call(`/api${path}/download`)).arrayBuffer(),
  );
  assert.deepEqual(downloaded, compile(source));
  assert.deepEqual(
    await (await call(`/api/admin${path}/source`, "admin")).json(),
    source,
  );
  assert.equal(
    (await call("/api/admin/resources", "admin", "POST", { source })).status,
    409,
  );
  assert.equal(
    (
      await call(
        `/api/admin/resources/${source.resourceId}/unpublish`,
        "admin",
        "POST",
      )
    ).status,
    200,
  );
  assert.equal((await call(`/api${path}/download`)).status, 404);
  assert.equal((await call(`/api/admin${path}/download`, "admin")).status, 200);
});
test("resource publication is explicit, immutable and withdrawal survives restart", () => {
  const mappings = new SwitchMappingStore({ databasePath: ":memory:" });
  const store = new ResourceStore(mappings, { seed: false });
  try {
    store.upload(source);
    assert.equal(store.list().length, 0);
    store.publish(source.resourceId, 1);
    assert.equal(store.list().length, 1);
    assert.throws(() => store.upload(source));
    store.upload({ ...source, revision: 2 });
    assert.equal(store.list()[0].revision, 1);
    store.publish(source.resourceId, 2);
    assert.equal(store.list()[0].revision, 2);
    store.unpublish(source.resourceId);
    assert.equal(store.list().length, 0);
    assert.equal(new ResourceStore(mappings, { seed: false }).list().length, 0);
    assert.equal(store.list(true).length, 2);
  } finally {
    mappings.close();
  }
});
test("factory catalog includes ten standalone resources and rejects invalid mapping publication", () => {
  const mappings = new SwitchMappingStore({ databasePath: ":memory:" });
  try {
    const store = new ResourceStore(mappings);
    assert.equal(store.list().length, 10);
    const draft = mappings.createDraft({
      catalogId: null,
      metadata: { displayName: "Incomplete", description: "" },
      compatibility: {
        productId: "HBOX",
        pcbRevision: "2.0.0",
        hardwareVersion: "2.0.0",
      },
      mapping: {
        name: "Incomplete",
        length: 2,
        step: 0.1,
        samplingNoise: 0,
        samplingFrequency: 1,
        originalValues: [0, 0],
      },
      actor: "test",
      allowBlank: true,
    });
    assert.throws(() => store.publish(draft.catalogId, 1));
    assert.equal(store.list().length, 10);
    assert.equal(store.list(true).at(0).validationError, "Invalid sample");
  } finally {
    mappings.close();
  }
});
