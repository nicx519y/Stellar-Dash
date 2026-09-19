const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {EventEmitter} = require('node:events');

test('TX restart reapplies config without reopening USB or retrying every telemetry frame', () => {
  let handle;
  let callbacks = 0;
  const published = [];
  const fakeHid = {
    devices: () => [{path:'test-only'}],
    HID: class extends EventEmitter {
      constructor() {super();handle=this;}
      close() {}
    },
  };
  const module = {exports:{}};
  const mocks = {
    'node-hid':fakeHid,
    './relative-latency':{RelativeLatencyDecoder:class {parse(){return null;} reset(){}}},
    './button-latency-source':{buttonLatencyTracker:{reset(){}},monotonicNowUsForMonitor:()=>0},
    './application-hid-telemetry-source':{parseApplicationHidTelemetryFrame:()=>[]},
    './dongle-hid-telemetry-source':{parseDongleHidTelemetryFrame:buf=>[{kind:'device_status',state:buf}]},
    './hid-device-selection':{matchesHidTelemetryDevice:()=>true},
  };
  const filename=path.join(__dirname,'../dist/electron/sources/hid-telemetry-source.js');
  new Function('require','module','exports',fs.readFileSync(filename,'utf8'))(
    id=>{if(!(id in mocks))throw Error(`Unexpected dependency ${id}`);return mocks[id];},module,module.exports);
  const stop=module.exports.startHidTelemetrySource(ev=>published.push(ev),{onControlReady:()=>callbacks++});
  try {
    assert.equal(callbacks,1); // USB open
    handle.emit('data','Connecting');handle.emit('data','Connected');
    assert.equal(callbacks,2);
    for(let i=0;i<100;i++)handle.emit('data','Connected');
    assert.equal(callbacks,2); // no continuous configuration traffic
    handle.emit('data','Reconnecting');handle.emit('data','Connecting');handle.emit('data','Connected');
    assert.equal(callbacks,3); // same USB handle, new RF session
    assert.equal(published.at(-1).state,'Connected');
  } finally {stop();}
});
