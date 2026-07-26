const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const Module = require('node:module');

// The production adapter uses the same @/ aliases as Next.js. Keep this
// contract test dependency-free by resolving those aliases inside this test
// process before loading the adapter.
const resolveFilename = Module._resolveFilename;
const loadModule = Module._load;
Module._resolveFilename = function resolveTestAlias(request, parent, isMain, options) {
  let resolvedRequest = request;
  if (request.startsWith('@/')) {
    const base = path.resolve(__dirname, '..', request.slice(2));
    resolvedRequest = [
      `${base}.ts`,
      `${base}.tsx`,
      `${base}.js`,
      path.join(base, 'index.ts'),
      path.join(base, 'index.tsx'),
      path.join(base, 'index.js'),
      base,
    ].find((candidate) => {
      try {
        return fs.statSync(candidate).isFile();
      } catch {
        return false;
      }
    }) ?? base;
  }
  return resolveFilename.call(this, resolvedRequest, parent, isMain, options);
};
Module._load = function loadTestDependency(request, parent, isMain) {
  if (request === '@/lib/websocket-queue-manager') {
    return {
      WebSocketQueueManager: class TestQueueManager {
        setSendFunction() {}
        clear() {}
      },
    };
  }
  return loadModule.call(this, request, parent, isMain);
};

const { MockDeviceTransport } = require('../lib/device-transport/mock-device-transport.ts');
const { DeviceTransportFrameworkAdapter } = require('../lib/device-transport/framework-adapter.ts');
Module._resolveFilename = resolveFilename;
Module._load = loadModule;

class MemoryStorage {
  constructor() {
    this.values = new Map();
  }

  getItem(key) {
    return this.values.get(key) ?? null;
  }

  setItem(key, value) {
    this.values.set(key, value);
  }
}

const delay = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds));

async function createTransport(options = { storage: null }) {
  const transport = new MockDeviceTransport(options);
  await transport.connect();
  return transport;
}

function waitForBinary(transport, command) {
  return new Promise((resolve) => {
    const unsubscribe = transport.subscribe('legacy.binary', (event) => {
      if (!event.binary || new DataView(event.binary).getUint8(0) !== command) return;
      unsubscribe();
      resolve(event.binary);
    });
  });
}

async function sendBinary(transport, bytes, responseCommand) {
  const response = waitForBinary(transport, responseCommand);
  await transport.upload('legacy-binary', bytes);
  return response;
}

function encodeMacro(triggerKeys, steps) {
  const bytes = new Uint8Array(1 + triggerKeys.length + 1 + steps.length * 10);
  const view = new DataView(bytes.buffer);
  let offset = 0;
  bytes[offset++] = triggerKeys.length;
  triggerKeys.forEach((key) => { bytes[offset++] = key; });
  bytes[offset++] = steps.length;
  steps.forEach(([timeMs, buttonMask, dynamicMask]) => {
    view.setUint16(offset, timeMs, true);
    offset += 2;
    view.setUint32(offset, buttonMask, true);
    offset += 4;
    view.setUint32(offset, dynamicMask, true);
    offset += 4;
  });
  return Buffer.from(bytes).toString('base64');
}

test('connects and serves a complete V2 fixture without auto calibration', async () => {
  const transport = await createTransport();

  assert.equal(transport.session.transport, 'mock');
  assert.equal(transport.session.authenticated, true);
  assert.equal(transport.state, 'connected');

  const global = await transport.request('get_global_config');
  assert.equal(global.data.globalConfig.hardware.hardwareVersion, '2.0.0');
  assert.equal(global.data.globalConfig.autoCalibrationEnabled, false);

  const profiles = await transport.request('get_profile_list');
  assert.equal(profiles.data.profileList.items.length, 2);
  assert.equal(profiles.data.defaultProfileDetails.name, 'Arcade');

  const layout = await transport.request('get_hitbox_layout');
  assert.equal(layout.data.length, 22);
  await transport.close();
});

test('persists configuration in the injected tab storage and isolates new tabs', async () => {
  const storage = new MemoryStorage();
  const first = await createTransport({ storage });
  await first.request('update_global_config', {
    globalConfig: {
      wirelessReportRate: '4K',
      power: { wakeHoldMs: 3000, autoStandbyMs: 10000 },
    },
  });
  await first.request('update_profile', {
    profileId: 'profile-arcade',
    profileDetails: { name: 'Edited Offline' },
  });
  await first.close();

  const refreshed = await createTransport({ storage });
  const global = await refreshed.request('get_global_config');
  const profile = await refreshed.request('get_default_profile');
  assert.equal(global.data.globalConfig.wirelessReportRate, '4K');
  assert.equal(global.data.globalConfig.power.autoStandbyMs, 10000);
  assert.equal(profile.data.defaultProfileDetails.name, 'Edited Offline');

  const otherTab = await createTransport({ storage: new MemoryStorage() });
  const otherProfile = await otherTab.request('get_default_profile');
  assert.equal(otherProfile.data.defaultProfileDetails.name, 'Arcade');
  await refreshed.close();
  await otherTab.close();
});

test('supports Profile CRUD, hotkeys, screen settings and device logs', async () => {
  const transport = await createTransport();

  const created = await transport.request('create_profile', {
    profileName: 'Offline QA',
  });
  const createdProfile = created.data.profileList.items.find(
    (profile) => profile.name === 'Offline QA',
  );
  assert.ok(createdProfile);

  await transport.request('switch_default_profile', {
    profileId: createdProfile.id,
  });
  await transport.request('update_profile', {
    profileId: createdProfile.id,
    profileDetails: { name: 'Offline QA Edited' },
  });
  let profiles = await transport.request('get_profile_list');
  assert.equal(profiles.data.profileList.defaultId, createdProfile.id);
  assert.equal(
    profiles.data.profileList.items.find((profile) => profile.id === createdProfile.id).name,
    'Offline QA Edited',
  );

  const initialHotkeys = await transport.request('get_hotkeys_config');
  const nextHotkeys = initialHotkeys.data.hotkeysConfig.slice(0, 2);
  await transport.request('update_hotkeys_config', {
    hotkeysConfig: nextHotkeys,
  });
  const hotkeys = await transport.request('get_hotkeys_config');
  assert.deepEqual(hotkeys.data.hotkeysConfig, nextHotkeys);

  await transport.request('update_screen_control_config', {
    screenControl: { brightness: 33, currentPageId: 7 },
  });
  const screen = await transport.request('get_screen_control_config');
  assert.equal(screen.data.screenControl.brightness, 33);
  assert.equal(screen.data.screenControl.currentPageId, 7);

  const logs = await transport.request('get_device_logs_list');
  assert.ok(logs.data.items.length > 0);
  assert.match(logs.data.items[0], /\[MOCK\]/);

  await transport.request('delete_profile', {
    profileId: createdProfile.id,
  });
  profiles = await transport.request('get_profile_list');
  assert.equal(
    profiles.data.profileList.items.some((profile) => profile.id === createdProfile.id),
    false,
  );
  assert.notEqual(profiles.data.profileList.defaultId, createdProfile.id);
  await transport.close();
});

test('uses {k,s} profile macro slots and Base64 for the single-macro API', async () => {
  const transport = await createTransport();
  const initial = await transport.request('get_profile_macros', { pid: 'profile-arcade' });
  assert.equal(initial.data.m.length, 5);
  assert.deepEqual(initial.data.m[0].k, [18, 19]);
  assert.equal('t' in initial.data.m[0], false);

  // This is the exact shape sent by updateProfileDetails. JSON transports omit
  // the undefined macros field, so the mock must preserve the existing macro.
  await transport.request('update_profile', {
    profileId: 'profile-arcade',
    profileDetails: {
      keysConfig: {
        invertXAxis: true,
        macros: undefined,
      },
    },
  });
  const afterProfileSave = await transport.request('get_profile_macros', {
    pid: 'profile-arcade',
  });
  assert.deepEqual(afterProfileSave.data.m[0], initial.data.m[0]);

  const wire = [
    null,
    { k: [4, 5], s: [[25, 0x1234, 0x80000000]] },
    null,
    null,
    null,
  ];
  const updated = await transport.request('update_profile_macros', {
    pid: 'profile-arcade',
    m: wire,
  });
  assert.deepEqual(updated.data.m, wire);

  const encoded = encodeMacro([7], [[40, 0x10, 0x20]]);
  const singleUpdate = await transport.request('update_macro', {
    profileId: 'profile-arcade',
    macro: { index: 2, data: encoded },
  });
  assert.deepEqual(singleUpdate.data.macro, { index: 2, data: encoded });
  const singleRead = await transport.request('get_macro', {
    profileId: 'profile-arcade',
    index: 2,
  });
  assert.deepEqual(singleRead.data.macro, { index: 2, data: encoded });
  await transport.close();
});

test('stages imports and applies them atomically only on finish', async () => {
  const transport = await createTransport();
  await transport.request('import_config_part', {
    section: 'global',
    data: { wirelessReportRate: '2K' },
  });
  let global = await transport.request('get_global_config');
  assert.equal(global.data.globalConfig.wirelessReportRate, '8K');

  await transport.request('import_config_finish');
  global = await transport.request('get_global_config');
  assert.equal(global.data.globalConfig.wirelessReportRate, '2K');

  await transport.request('import_config_part', {
    section: 'global',
    data: { wirelessReportRate: '1K' },
  });
  await transport.request('import_config_part', {
    section: 'hotkeys',
    data: { invalid: true },
  });
  await assert.rejects(() => transport.request('import_config_finish'), /hotkeys must be an array/);
  global = await transport.request('get_global_config');
  assert.equal(global.data.globalConfig.wirelessReportRate, '2K');
  await transport.close();
});

test('exports and atomically restores the default profile selection', async () => {
  const transport = await createTransport();
  await transport.request('switch_default_profile', {
    profileId: 'profile-tournament',
  });

  let exportedGlobal;
  const complete = new Promise((resolve) => {
    const unsubscribe = transport.subscribe('export_all_config', (event) => {
      if (event.data.section === 'global') exportedGlobal = event.data.data;
      if (event.data.section === 'end') {
        unsubscribe();
        resolve();
      }
    });
  });
  await transport.request('export_all_config');
  await complete;
  assert.equal(exportedGlobal.defaultProfileId, 'profile-tournament');

  await transport.request('switch_default_profile', {
    profileId: 'profile-arcade',
  });
  await transport.request('import_config_part', {
    section: 'global',
    data: exportedGlobal,
  });
  let profiles = await transport.request('get_profile_list');
  assert.equal(profiles.data.profileList.defaultId, 'profile-arcade');

  await transport.request('import_config_finish');
  profiles = await transport.request('get_profile_list');
  assert.equal(profiles.data.profileList.defaultId, 'profile-tournament');
  const global = await transport.request('get_global_config');
  assert.equal(global.data.globalConfig.defaultProfileId, 'profile-tournament');
  await transport.close();
});

test('keeps normal button monitoring silent and emits performance frames explicitly', async () => {
  const transport = await createTransport();
  const commands = [];
  const unsubscribe = transport.subscribe('legacy.binary', (event) => {
    commands.push(new DataView(event.binary).getUint8(0));
  });

  await transport.request('start_button_monitoring');
  await delay(220);
  assert.deepEqual(commands, []);
  const states = await transport.request('get_button_states');
  assert.equal(states.data.triggerMask, 0);

  await transport.request('start_button_performance_monitoring');
  await delay(140);
  await transport.request('stop_button_performance_monitoring');
  assert.ok(commands.includes(2));
  unsubscribe();
  await transport.close();
});

test('emits uncalibrated, top, bottom and completed calibration states', async () => {
  const transport = await createTransport();
  const phases = [];
  const complete = new Promise((resolve) => {
    const unsubscribe = transport.subscribe('calibration_update', (event) => {
      const status = event.data.calibrationStatus;
      phases.push(status.buttons[0].phase);
      if (status.allCalibrated) {
        unsubscribe();
        resolve();
      }
    });
  });

  const started = await transport.request('start_manual_calibration');
  assert.equal(started.data.calibrationStatus.allCalibrated, false);
  // Match React development Strict Mode's immediate effect cleanup. It must
  // not cancel the newly mounted calibration view's deterministic sequence.
  await transport.request('stop_manual_calibration');
  await Promise.race([
    complete,
    delay(1000).then(() => { throw new Error('calibration events timed out'); }),
  ]);
  assert.deepEqual(phases, ['IDLE', 'TOP_SAMPLING', 'BOTTOM_SAMPLING', 'COMPLETED']);
  const completed = await transport.request('check_is_manual_calibration_completed');
  assert.equal(completed.data.isCompleted, true);
  await transport.close();
});

test('samples ADC mapping without shifting the first point and writes it back', async () => {
  const transport = await createTransport();
  const started = await transport.request('ms_mark_mapping_start', { id: 'mapping-default' });
  const length = started.data.status.length;
  let result;
  for (let index = 0; index < length; index += 1) {
    result = await transport.request('ms_mark_mapping_step');
  }

  assert.equal(result.data.status.is_completed, true);
  assert.equal(result.data.status.values.length, length);
  assert.equal(result.data.status.values[0], 4000);
  assert.equal(result.data.status.values.at(-1), 800);
  const mapping = await transport.request('ms_get_mapping', { id: 'mapping-default' });
  assert.deepEqual(mapping.data.mapping.originalValues, result.data.status.values);
  assert.equal(mapping.data.mapping.calibratedValues.at(-1), (length - 1) * 100);
  await transport.close();
});

test('uploads, reports, reads and deletes an in-memory background image', async () => {
  const transport = await createTransport();
  const cid = 0x10203040;
  const pixels = Uint8Array.from([1, 2, 3, 4, 5, 6, 7, 8]);

  const begin = new Uint8Array(18);
  let view = new DataView(begin.buffer);
  view.setUint8(0, 0x30);
  view.setUint32(2, cid, true);
  view.setUint16(6, 2, true);
  view.setUint16(8, 2, true);
  view.setUint32(10, pixels.length, true);
  view.setUint8(14, 1);
  const beginResponse = new DataView(await sendBinary(transport, begin, 0xb0));
  assert.equal(beginResponse.getUint8(1), 1);

  const chunk = new Uint8Array(14 + pixels.length);
  view = new DataView(chunk.buffer);
  view.setUint8(0, 0x31);
  view.setUint32(2, cid, true);
  view.setUint16(10, pixels.length, true);
  chunk.set(pixels, 14);
  const chunkResponse = new DataView(await sendBinary(transport, chunk, 0xb1));
  assert.equal(chunkResponse.getUint32(6, true), pixels.length);

  const commit = new Uint8Array(6);
  view = new DataView(commit.buffer);
  view.setUint8(0, 0x32);
  view.setUint32(2, cid, true);
  const commitResponse = new DataView(await sendBinary(transport, commit, 0xb2));
  assert.equal(commitResponse.getUint8(1), 1);

  const info = new Uint8Array(6);
  view = new DataView(info.buffer);
  view.setUint8(0, 0x34);
  view.setUint32(2, cid, true);
  const infoResponse = new DataView(await sendBinary(transport, info, 0xb4));
  assert.equal(infoResponse.getUint8(6), 1);
  assert.equal(infoResponse.getUint8(7), 1);
  assert.equal(infoResponse.getUint32(12, true), pixels.length);

  const read = new Uint8Array(14);
  view = new DataView(read.buffer);
  view.setUint8(0, 0x35);
  view.setUint8(1, 0);
  view.setUint32(2, cid, true);
  view.setUint16(10, pixels.length, true);
  const readBuffer = await sendBinary(transport, read, 0xb5);
  const readView = new DataView(readBuffer);
  assert.equal(readView.getUint8(1), 1);
  assert.equal(readView.getUint32(4, true), cid);
  assert.equal(readView.getUint16(20, true), pixels.length);
  assert.deepEqual(
    Array.from(new Uint8Array(readBuffer, 55, pixels.length)),
    Array.from(pixels),
  );

  const remove = new Uint8Array(6);
  view = new DataView(remove.buffer);
  view.setUint8(0, 0x33);
  view.setUint32(2, cid, true);
  await sendBinary(transport, remove, 0xb3);
  const deletedInfo = new DataView(await sendBinary(transport, info, 0xb4));
  assert.equal(deletedInfo.getUint8(6), 0);
  await transport.close();
});

test('keeps firmware checks offline and returns a simulated reboot success', async () => {
  const transport = await createTransport();
  const response = await transport.authorizedFetch(
    'https://firmware.example/api/firmware-check-update',
  );
  const body = await response.json();
  assert.equal(response.status, 200);
  assert.equal(body.data.updateAvailable, false);
  assert.equal(body.data.currentVersion, '2.0.0-mock');

  const reboot = await transport.request('reboot');
  assert.equal(reboot.data.success, true);
  assert.equal(transport.state, 'connected');
  await transport.close();
});

test('serves firmware metadata, upgrade sessions and the binary chunk ACK', async () => {
  const transport = await createTransport();
  const metadata = await transport.request('get_firmware_metadata');
  assert.equal(metadata.data.version, '2.0.0-mock');
  assert.ok(metadata.data.components.some((component) => component.name === 'application'));

  const created = await transport.request('create_firmware_upgrade_session', {
    session_id: 'mock-upgrade-contract',
    manifest: {},
  });
  assert.equal(created.data.session_id, 'mock-upgrade-contract');
  const chunk = await transport.request('upload_firmware_chunk', {
    session_id: created.data.session_id,
    chunk_size: 4096,
  });
  assert.equal(chunk.data.success, true);
  const completed = await transport.request('complete_firmware_upgrade_session', {
    session_id: created.data.session_id,
  });
  assert.equal(completed.data.status, 'completed');

  await transport.request('create_firmware_upgrade_session', {
    session_id: 'mock-upgrade-abort',
    manifest: {},
  });
  const aborted = await transport.request('abort_firmware_upgrade_session', {
    session_id: 'mock-upgrade-abort',
  });
  assert.equal(aborted.data.status, 'aborted');

  const binaryChunk = new Uint8Array(62);
  const binaryView = new DataView(binaryChunk.buffer);
  binaryView.setUint8(0, 0x01);
  binaryView.setUint32(54, 2, true);
  binaryView.setUint32(58, 4, true);
  const ack = new DataView(await sendBinary(transport, binaryChunk, 0x81));
  assert.equal(ack.getUint8(1), 1);
  assert.equal(ack.getUint32(2, true), 2);
  assert.equal(ack.getUint32(6, true), 75);
  await transport.close();
});

test('emits a complete ordered configuration export stream', async () => {
  const transport = await createTransport();
  const sections = [];
  const complete = new Promise((resolve) => {
    const unsubscribe = transport.subscribe('export_all_config', (event) => {
      sections.push(event.data.section);
      if (event.data.section === 'end') {
        unsubscribe();
        resolve();
      }
    });
  });

  await transport.request('export_all_config');
  await complete;
  assert.deepEqual(sections, [
    'global',
    'hotkeys',
    'screenControl',
    'profile',
    'profile',
    'end',
  ]);
  await transport.close();
});

test('adapter delivers legacy binary once without duplicating it as JSON', async () => {
  const transport = new MockDeviceTransport({ storage: null });
  const adapter = new DeviceTransportFrameworkAdapter(transport);
  await adapter.connect();

  let jsonMessages = 0;
  let binaryMessages = 0;
  const unsubscribeMessage = adapter.onMessage(() => {
    jsonMessages += 1;
  });
  const binaryReceived = new Promise((resolve) => {
    const unsubscribeBinary = adapter.onBinaryMessage(() => {
      binaryMessages += 1;
      unsubscribeBinary();
      resolve();
    });
  });

  const request = new ArrayBuffer(6);
  const requestView = new DataView(request);
  requestView.setUint8(0, 0x34);
  requestView.setUint32(2, 0x12345678, true);
  adapter.sendBinaryMessage(request);

  await binaryReceived;
  await Promise.resolve();
  unsubscribeMessage();
  adapter.dispose();

  assert.equal(binaryMessages, 1);
  assert.equal(jsonMessages, 0);
});
