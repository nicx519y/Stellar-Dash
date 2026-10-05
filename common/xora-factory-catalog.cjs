// Named exports keep the browser bundler and the native Mock harness consistent.
exports.lighting = [...require('./xora-factory-sources.json')];
exports.topology = [...require('./xora-light-topology.json')];
exports.mappings = [require('../resources/xora/factory-axis.xora-resource.json'), require('../resources/xora/7c9dbd1c.xora-resource.json')];
