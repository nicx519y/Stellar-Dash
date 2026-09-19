const test = require('node:test');
const assert = require('node:assert/strict');
const { parseDongleHidTelemetryFrame } = require('../dist/electron/sources/dongle-hid-telemetry-source.js');
const fs = require('node:fs');
const path = require('node:path');
const ts = require('typescript');

function loadRendererModule(name) {
  const filename = path.join(__dirname, '../renderer/src/ui', `${name}.ts`);
  const code = ts.transpileModule(fs.readFileSync(filename, 'utf8'), {
    compilerOptions: { module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2022 },
  }).outputText;
  const module = { exports: {} };
  new Function('require', 'module', 'exports', code)(
    (id) => id === './monitorStreamTypes' ? loadRendererModule('monitorStreamTypes') : require(id),
    module, module.exports);
  return module.exports;
}

function frame(page) {
  const data = Buffer.alloc(32);
  data.writeUInt32LE(0x31444852, 0);
  data.writeUInt32LE(17, 4);
  data[8] = 1; data[9] = page; data[10] = 2; data[11] = 22;
  return data;
}
test('RHD1 timing page preserves exact RF and USB boundaries', () => {
  const data = frame(0);
  data.writeUInt16LE(8, 12); data.writeUInt16LE(0xffff, 14);
  data.writeUInt16LE(47, 16); data.writeUInt16LE(12, 18);
  data.writeUInt32LE(3, 20); data.writeUInt32LE(2, 24);
  data[28] = 5; data[29] = 1;
  data.writeUInt16LE(0x1902, 30);
  const [packet] = parseDongleHidTelemetryFrame(data, 123);
  assert.equal(packet.messageType, 'RFH_RHD1_0');
  assert.equal(packet.rfReadyMs, 8); assert.equal(packet.usbReadyMs, undefined);
  assert.equal(packet.rfBuildId, 0x1902);
  assert.equal(packet.rfConnectMs, 47); assert.equal(packet.rfConnectCount, 3);
  assert.equal(packet.rfAckWatchdog, 2); assert.equal(packet.airPendingMax, 5);
  assert.equal(packet.sampleCount, undefined); // must not inflate DATA rate
});
test('RHD1 counters are unsigned 32-bit and unknown versions are rejected', () => {
  const data = frame(1);
  for(let offset=12; offset<32; offset+=4) data.writeUInt32LE(0xf0000000 + offset, offset);
  const [packet] = parseDongleHidTelemetryFrame(data);
  assert.equal(packet.rfAckLate, 0xf000000c);
  assert.equal(packet.rfInputEdgeDrop, 0xf0000018);
  assert.equal(packet.rfCrcTotal, 0xf000001c);
  data[8] = 2; assert.deepEqual(parseDongleHidTelemetryFrame(data), []);
  data[8] = 1; data[9] = 4; assert.deepEqual(parseDongleHidTelemetryFrame(data), []);
  assert.deepEqual(parseDongleHidTelemetryFrame(data.subarray(0, 31)), []);
});

test('RX timing maxima and SDK failures do not count as DATA packets', () => {
  const data = frame(3);
  data.writeUInt32LE(0xf0000012, 12);
  data.writeUInt16LE(8, 16); data.writeUInt16LE(70, 18);
  data.writeUInt16LE(4, 20); data.writeUInt16LE(9, 22);
  data.writeUInt32LE(7, 24); data.writeUInt32LE(1000000, 28);
  const [packet] = parseDongleHidTelemetryFrame(data);
  assert.equal(packet.rfRxArmFailures, 0xf0000012);
  assert.equal(packet.rfRxRearmMaxUs, 8); assert.equal(packet.rfRxCallbackMaxUs, 70);
  assert.equal(packet.rfInputCommitMaxUs, 4); assert.equal(packet.rfInputCaptureMaxUs, 9);
  assert.equal(packet.rfAckSendFailures, 7); assert.equal(packet.rfShortDecodedTotal, 1000000);
  assert.equal(packet.sampleCount, undefined); assert.equal(packet.rateHz, undefined);
});

test('TX cadence and air loss counters remain separate from measured RX rate', () => {
  const data = frame(2);
  data.writeUInt16LE(1000, 12); data.writeUInt16LE(8000, 14);
  data.writeUInt16LE(6300, 16); data.writeUInt16LE(1700, 18);
  data.writeUInt16LE(150, 20); data.writeUInt32LE(29, 22);
  data.writeUInt32LE(6271, 26); data[30] = 2; data[31] = 1;
  const [packet] = parseDongleHidTelemetryFrame(data);
  assert.equal(packet.rfTxDue, 8000); assert.equal(packet.rfTxStarted, 6300);
  assert.equal(packet.rfTxDropped, 1700); assert.equal(packet.rfAirMissingTotal, 29);
  assert.equal(packet.rfAirReceivedTotal, 6271); assert.equal(packet.rfTxDiagnosticValid, true);
  assert.equal(packet.rateHz, undefined); assert.equal(packet.sampleCount, undefined);
  data[31] = 0;
  assert.equal(parseDongleHidTelemetryFrame(data)[0].rfTxDiagnosticValid, false);
});

test('diagnostics do not invent DATA throughput or complete a running hop', () => {
  const { MonitorStreamProcessor } = loadRendererModule('monitorStreamProcessor');
  const processor = new MonitorStreamProcessor();
  const [diagnostic] = parseDongleHidTelemetryFrame(frame(0), Date.now());
  processor.processBatch([diagnostic]);
  assert.equal(processor.snapshot().packets.rfRxPerSec, 0);
  assert.equal(processor.snapshot().channelSwitches.length, 0);
  const start = { ...diagnostic, rfDiagnosticVersion: undefined, messageType: 'RFH_RHM1',
    hopEvent: 'start', rfStateCode: 'D', oldChannelNumber: 16, targetChannelNumber: 22 };
  processor.processBatch([start]);
  assert.equal(processor.snapshot().channelSwitches.length, 1);
  processor.processBatch([diagnostic]);
  assert.equal(processor.snapshot().channelSwitches.length, 1);
  assert.equal(processor.snapshot().events.length, 3); // still retained for diagnostics/export
});

test('negotiated hop remains connected and recovery is reported separately', () => {
  const data = Buffer.alloc(32);
  data.writeUInt32LE(0x314d4852, 0);
  data[22] = 3; data[23] = 16; data[25] = 22;
  let events = parseDongleHidTelemetryFrame(data);
  assert.equal(events.find(e => e.kind === 'device_status').state, 'Connected');
  data[22] = 5;
  events = parseDongleHidTelemetryFrame(data);
  assert.equal(events.find(e => e.kind === 'device_status').state, 'Reconnecting');
});
