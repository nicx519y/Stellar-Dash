const test = require('node:test');
const assert = require('node:assert/strict');
const { batteryTrendChart } = require('../lib/battery-trend-chart.ts');

const sample = (end, valid = 10, missing = 0) => ({
  data: [1, 7, end - 1000, end, 3500, 3510, 510, 500, valid, missing],
});

test('chart uses measured volts, percent and elapsed hours, retaining gaps as null', () => {
  const data = batteryTrendChart([sample(3600000), sample(5400000, 0), sample(6000000, 10, 1), sample(7200000)], { uptimeMs: 0 });
  assert.deepEqual(data.voltage.map(p => p.y), [3.5, null, null, 3.5]);
  assert.deepEqual(data.soc.map(p => p.y), [50, null, null, 50]);
  assert.equal(data.voltage[0].x, 1);
  assert.equal(data.maxHours, 2);
});

test('partial pages use their loaded interval and unsigned uptime wrap stays bounded', () => {
  assert.equal(batteryTrendChart([sample(3600000)]).maxHours, 1000 / 3600000);
  const wrapped = batteryTrendChart([sample(20)], { uptimeMs: 0xfffffff0 });
  assert.equal(wrapped.maxHours, 36 / 3600000);
  const empty = batteryTrendChart([]);
  assert.deepEqual(empty.voltage, []);
  assert.ok(Number.isFinite(empty.maxHours));
});
