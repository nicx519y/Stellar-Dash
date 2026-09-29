const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const vm = require('node:vm');
const { spawnSync } = require('node:child_process');
const ts = require('typescript');
const { BoundedDelivery } = require('../dist/electron/pipeline/bounded-delivery');
const { MonitorEventStore } = require('../dist/electron/pipeline/event-store');
const { MonitorEventBus } = require('../dist/electron/pipeline/event-bus');

test('stalled IPC consumers remain bounded and resume with recent rows', () => {
  const queue = new BoundedDelivery(2000, 500);
  const sent = [];
  const flush = () => queue.flush((rows, sequence) => sent.push({ rows, sequence }));
  for (let i = 0; i < 100000; i++) { queue.enqueue([i]); flush(); }
  assert.equal(sent.length, 1);
  assert.deepEqual(queue.stats(), { pending: 2000, inFlight: true, dropped: 97999 });
  queue.acknowledge(-1); flush();
  assert.equal(sent.length, 1);
  queue.acknowledge(sent[0].sequence); flush();
  assert.deepEqual(sent[1].rows, Array.from({ length: 500 }, (_, i) => 98000 + i));
  queue.clear(); queue.enqueue([100001]); flush();
  assert.equal(sent.length, 2, 'clear must not create a second in-flight batch');
  queue.reset(); queue.enqueue([100002]); flush();
  queue.acknowledge(sent[1].sequence); flush();
  assert.equal(sent.length, 3, 'stale acknowledgements cannot release the new generation');
});

test('preload acknowledges only after asynchronous consumer processing completes', async () => {
  const listeners = new Map(), sent = [];
  let api;
  vm.runInNewContext(fs.readFileSync(path.join(__dirname, '../dist/electron/preload.js'), 'utf8'), {
    exports: {}, console,
    require: () => ({
      contextBridge: { exposeInMainWorld: (_name, value) => { api = value; } },
      ipcRenderer: {
        on: (name, listener) => listeners.set(name, listener),
        off: name => listeners.delete(name),
        send: (...args) => sent.push(args),
      },
    }),
  });
  for (const [subscribe, channel] of [[api.onEvents, 'monitor:events'], [api.onSerialLogs, 'serial:logs']]) {
    let finish;
    const unsubscribe = subscribe(() => new Promise(resolve => { finish = resolve; }));
    assert.equal(sent.at(-1)[0], channel + ':ready');
    const delivery = listeners.get(channel)(null, [], 123);
    await Promise.resolve();
    assert.equal(sent.at(-1)[0], channel + ':ready');
    finish(); await delivery;
    assert.equal(sent.at(-1)[0], channel + ':ack');
    assert.equal(sent.at(-1)[1], 123);
    unsubscribe();
    assert.equal(listeners.has(channel), false);
  }
});

function loadRendererModule(relative, cache = new Map()) {
  const filename = path.resolve(__dirname, '../renderer/src/ui', relative + '.ts');
  if (cache.has(filename)) return cache.get(filename);
  const exports = {};
  cache.set(filename, exports);
  const js = ts.transpileModule(fs.readFileSync(filename, 'utf8'), {
    compilerOptions: { module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2022 },
  }).outputText;
  vm.runInNewContext(js, { exports, require: name => loadRendererModule(name, cache), Date });
  return exports;
}

test('worker emits only one full snapshot while renderer is stalled', () => {
  const posted = [], timers = new Set();
  const self = { postMessage: message => posted.push(message) };
  const source = fs.readFileSync(path.join(__dirname, '../renderer/src/ui/monitorStream.worker.ts'), 'utf8');
  const js = ts.transpileModule(source, {
    compilerOptions: { module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2022 },
  }).outputText;
  vm.runInNewContext(js, {
    exports: {}, require: name => loadRendererModule(name), self,
    setTimeout: fn => { timers.add(fn); return fn; }, clearTimeout: fn => timers.delete(fn),
  });
  const send = data => self.onmessage({ data });
  for (let i = 0; i < 10000; i++) {
    send({ type: 'batch', events: [{ kind: 'device_status', timestampMs: i }], requestId: i });
    for (const fn of [...timers]) fn();
  }
  assert.equal(posted.filter(m => m.type === 'snapshot').length, 1);
  assert.equal(posted.filter(m => m.type === 'processed').length, 10000);
  assert.equal(timers.size, 0, 'no repeated snapshot allocation while blocked');
  send({ type: 'snapshotConsumed' });
  for (const fn of [...timers]) fn();
  const latest = posted.at(-1).snapshot;
  assert.equal(latest.events.length, 500);
  assert.equal(latest.events.at(-1).timestampMs, 9999);
  send({ type: 'reset' });
  send({ type: 'snapshotConsumed' });
  for (const fn of [...timers]) fn();
  assert.equal(posted.at(-1).snapshot.events.length, 0);
});

function tempStore(t) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'monitor-memory-'));
  t.after(() => fs.rmSync(dir, { recursive: true, force: true }));
  return { dir, file: path.join(dir, 'monitor-events.jsonl'), store: new MonitorEventStore(dir) };
}

test('history write failures do not kill live delivery and can recover', t => {
  const { store, file } = tempStore(t);
  fs.mkdirSync(file); // An unwritable history target without changing system permissions.
  const bus = new MonitorEventBus(1, store), delivered = [];
  bus.subscribe(event => delivered.push(event));
  const event = { kind: 'device_status', timestampMs: 1 };
  bus.publish(event); bus.publish(event);
  assert.equal(delivered.length, 2);
  assert.ok(store.lastWriteError);
  fs.rmdirSync(file);
  bus.publish(event);
  assert.equal(store.lastWriteError, null);
  assert.equal(store.queryBefore(2, 10).length, 1);
});

test('reverse history paging preserves UTF-8, order and timestamp boundaries', t => {
  const { store, file } = tempStore(t);
  const events = Array.from({ length: 700 }, (_, i) => ({
    kind: 'error', timestampMs: i, message: '测试🙂'.repeat(80),
  }));
  fs.writeFileSync(file, events.map(e => JSON.stringify(e)).join('\r\n') + '\n{broken');
  assert.deepEqual(store.queryBefore(601, 500), events.slice(101, 601));
  assert.deepEqual(store.queryBefore(1, 500), [events[0]]);
  for (const limit of [NaN, Infinity, -1, 0]) assert.deepEqual(store.queryBefore(100, limit), []);
});

test('oversized corrupt records cannot grow the reverse-reader buffer indefinitely', t => {
  const { store, file } = tempStore(t);
  const first = { kind: 'error', timestampMs: 1, message: 'first' };
  const last = { kind: 'error', timestampMs: 2, message: 'last' };
  fs.writeFileSync(file, JSON.stringify(first) + '\n' + 'x'.repeat(3 * 1024 * 1024) + '\n' + JSON.stringify(last));
  assert.deepEqual(store.queryBefore(3, 100), [first, last]);
});

test('64 MB history can be paged under a 32 MB V8 heap limit', t => {
  const { dir, file } = tempStore(t);
  const row = JSON.stringify({ kind: 'error', timestampMs: 1, message: 'x'.repeat(4000) }) + '\n';
  const block = row.repeat(1024);
  for (let i = 0; i < 16; i++) fs.appendFileSync(file, block);
  const child = spawnSync(process.execPath, ['--max-old-space-size=32', '-e', `
    const {MonitorEventStore}=require(process.argv[1]);
    const rows=new MonitorEventStore(process.argv[2]).queryBefore(2,500);
    if(rows.length!==500) process.exit(2);
    console.log(JSON.stringify({rows:rows.length,heapUsed:process.memoryUsage().heapUsed}));
  `, require.resolve('../dist/electron/pipeline/event-store'), dir], { encoding: 'utf8' });
  assert.equal(child.status, 0, child.stderr);
  assert.equal(JSON.parse(child.stdout).rows, 500);
});
