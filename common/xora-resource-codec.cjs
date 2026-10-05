"use strict";

// XRES v1: 64-byte LE header, SHA-256(header[0..32) || payload), bounded payload.
const TYPES = ["switch-mapping", "key-lighting", "ambient-lighting"];
const SIGNALS = [
  "constant",
  "sine",
  "scan",
  "radial",
  "random-set",
  "scan-latch",
  "ring-tail",
  "center-wave",
  "pressed",
  "triangle",
  "envelope",
];
const BLENDS = ["replace", "add", "max"];
const CLOCKS = ["key-cycle", "ambient-cycle", "half-ambient-cycle"];
const MAX_BYTES = 2048;
function requireValue(ok, message) {
  if (!ok) throw new Error(message);
}
function integer(n, lo, hi, field) {
  requireValue(Number.isInteger(n) && n >= lo && n <= hi, `Invalid ${field}`);
  return n;
}
function textBytes(value, max, field) {
  requireValue(
    typeof value === "string" && !/[\u0000-\u001f\u007f]/.test(value),
    `Invalid ${field}`,
  );
  const bytes = new TextEncoder().encode(value);
  requireValue(
    bytes.length > 0 && bytes.length <= max,
    `Invalid ${field} length`,
  );
  return bytes;
}
function validateSource(s) {
  requireValue(
    s && s.schemaVersion === 1 && TYPES.includes(s.type),
    "Unsupported resource format",
  );
  requireValue(
    /^[a-zA-Z0-9_-]{1,15}$/.test(s.resourceId),
    "Resource ID must be 1-15 ASCII letters, digits, _ or -",
  );
  integer(s.revision, 1, 0xffffffff, "revision");
  textBytes(s.name, 79, "name");
  requireValue(
    typeof s.description === "string" && s.description.length <= 500,
    "Invalid description",
  );
  requireValue(
    s.compatibility?.hardwareVersion === "2.0.0" && s.engineVersion === 1,
    "Unsupported hardware or engine",
  );
  const p = s.payload;
  requireValue(p && typeof p === "object", "Missing payload");
  if (s.type === "switch-mapping") {
    textBytes(p.name, 15, "mapping name");
    integer(p.length, 2, 40, "mapping length");
    requireValue(
      Number.isFinite(p.step) && p.step >= 0.1 && p.step <= 10,
      "Invalid mapping step",
    );
    integer(p.samplingNoise, 0, 65535, "sampling noise");
    integer(p.samplingFrequency, 1, 65535, "sampling frequency");
    requireValue(
      Array.isArray(p.originalValues) && p.originalValues.length === p.length,
      "Invalid samples",
    );
    p.originalValues.forEach((v) => integer(v, 1, 65535, "sample"));
    requireValue(
      p.originalValues.every((v, i, a) => !i || a[i - 1] >= v) &&
        p.originalValues[0] > p.originalValues.at(-1),
      "Mapping must descend from pressed to released",
    );
    requireValue(
      Object.keys(p).every((k) =>
        [
          "id",
          "name",
          "length",
          "step",
          "samplingNoise",
          "samplingFrequency",
          "originalValues",
        ].includes(k),
      ),
      "Device calibration is not a resource",
    );
  } else {
    requireValue(
      CLOCKS.includes(p.clock) &&
        Array.isArray(p.colors) &&
        p.colors.length === 3,
      "Invalid lighting clock/colors",
    );
    p.colors.forEach((c) => integer(c, 0, 0xffffff, "color"));
    requireValue(
      Array.isArray(p.layers) && p.layers.length >= 1 && p.layers.length <= 8,
      "Lighting requires 1-8 layers",
    );
    p.layers.forEach((l) => {
      requireValue(
        SIGNALS.includes(l.signal) && BLENDS.includes(l.blend),
        "Unknown lighting primitive",
      );
      integer(l.from, 0, 2, "from color");
      integer(l.to, 0, 2, "to color");
      integer(l.mask, 0, 2, "mask");
      requireValue(
        Array.isArray(l.params) &&
          l.params.length === 6 &&
          l.params.every((v) => Number.isFinite(v) && Math.abs(v) <= 10000),
        "Invalid layer parameters",
      );
      requireValue(
        Math.fround(l.params[0]) > 0 && Math.fround(l.params[1]) > 0,
        "Layer duration and width must be positive",
      );
    });
  }
  return s;
}
function encode(s) {
  validateSource(s);
  const p = s.payload,
    mapping = s.type === "switch-mapping";
  const bytes = new Uint8Array(
    64 + (mapping ? 220 : 96 + p.layers.length * 32),
  );
  const v = new DataView(bytes.buffer);
  bytes.set([88, 82, 69, 83]);
  v.setUint16(4, 1, true);
  bytes[6] = TYPES.indexOf(s.type) + 1;
  bytes[7] = 1;
  v.setUint32(8, s.revision, true);
  v.setUint16(12, bytes.length - 64, true);
  bytes[14] = s.resourceId.length;
  bytes.set(new TextEncoder().encode(s.resourceId), 16);
  if (mapping) {
    bytes.set(new TextEncoder().encode("HBOX-ADC-MAP-V1\0"), 64);
    bytes.set(new TextEncoder().encode(s.resourceId), 80);
    bytes.set(textBytes(p.name, 15, "mapping name"), 96);
    v.setUint32(112, p.length, true);
    v.setFloat32(116, p.step, true);
    v.setUint16(120, p.samplingNoise, true);
    v.setUint16(122, p.samplingFrequency, true);
    p.originalValues.forEach((n, i) => v.setUint32(124 + i * 4, n, true));
  } else {
    p.colors.forEach((c, i) => v.setUint32(64 + i * 4, c, true));
    bytes[76] = p.layers.length;
    bytes[77] = CLOCKS.indexOf(p.clock);
    const name = textBytes(s.name, 79, "name");
    bytes[78] = name.length;
    bytes.set(name, 80);
    p.layers.forEach((l, i) => {
      const o = 160 + i * 32;
      bytes[o] = SIGNALS.indexOf(l.signal);
      bytes[o + 1] = l.from;
      bytes[o + 2] = l.to;
      bytes[o + 3] = BLENDS.indexOf(l.blend);
      bytes[o + 4] = l.mask;
      l.params.forEach((n, j) => v.setFloat32(o + 8 + j * 4, n, true));
    });
  }
  return bytes;
}
function digestInput(bytes) {
  const b = new Uint8Array(bytes.length - 32);
  b.set(bytes.subarray(0, 32));
  b.set(bytes.subarray(64), 32);
  return b;
}
function decode(bytes) {
  requireValue(
    bytes instanceof Uint8Array &&
      bytes.length >= 80 &&
      bytes.length <= MAX_BYTES,
    "Invalid resource size",
  );
  const v = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  requireValue(
    v.getUint32(0, true) === 0x53455258 &&
      v.getUint16(4, true) === 1 &&
      bytes[7] === 1 &&
      bytes[6] >= 1 &&
      bytes[6] <= 3,
    "Unsupported resource header",
  );
  requireValue(
    v.getUint16(12, true) + 64 === bytes.length &&
      bytes[14] >= 1 &&
      bytes[14] <= 15 &&
      bytes[15] === 0,
    "Invalid resource length",
  );
  requireValue(
    bytes.subarray(16 + bytes[14], 32).every((n) => n === 0),
    "Invalid ID padding",
  );
  const resourceId = new TextDecoder("utf-8", { fatal: true }).decode(
    bytes.subarray(16, 16 + bytes[14]),
  );
  const s = {
    schemaVersion: 1,
    engineVersion: 1,
    resourceId,
    revision: v.getUint32(8, true),
    type: TYPES[bytes[6] - 1],
    name: resourceId,
    description: "",
    compatibility: { hardwareVersion: "2.0.0" },
    payload: {},
  };
  if (bytes[6] === 1) {
    requireValue(bytes.length === 284, "Invalid mapping size");
    requireValue(
      new TextDecoder().decode(bytes.subarray(64, 80)) === "HBOX-ADC-MAP-V1\0",
      "Invalid mapping domain",
    );
    const n = v.getUint32(112, true);
    integer(n, 2, 40, "sample count");
    s.payload = {
      name: new TextDecoder("utf-8", { fatal: true })
        .decode(bytes.subarray(96, 112))
        .replace(/\0.*$/s, ""),
      length: n,
      step: v.getFloat32(116, true),
      samplingNoise: v.getUint16(120, true),
      samplingFrequency: v.getUint16(122, true),
      originalValues: Array.from({ length: n }, (_, i) =>
        v.getUint32(124 + i * 4, true),
      ),
    };
  } else {
    const n = bytes[76];
    requireValue(
      n >= 1 &&
        n <= 8 &&
        bytes.length === 160 + n * 32 &&
        bytes[78] >= 1 &&
        bytes[78] <= 79 &&
        bytes[79] === 0,
      "Invalid lighting payload",
    );
    s.name = new TextDecoder("utf-8", { fatal: true }).decode(
      bytes.subarray(80, 80 + bytes[78]),
    );
    requireValue(
      bytes.subarray(80 + bytes[78], 160).every((n) => n === 0),
      "Invalid name padding",
    );
    s.payload = {
      colors: [0, 1, 2].map((i) => v.getUint32(64 + i * 4, true)),
      clock: CLOCKS[bytes[77]],
      layers: Array.from({ length: n }, (_, i) => {
        const o = 160 + i * 32;
        requireValue(
          bytes.subarray(o + 5, o + 8).every((n) => n === 0),
          "Invalid layer padding",
        );
        return {
          signal: SIGNALS[bytes[o]],
          from: bytes[o + 1],
          to: bytes[o + 2],
          blend: BLENDS[bytes[o + 3]],
          mask: bytes[o + 4],
          params: Array.from({ length: 6 }, (_, j) =>
            v.getFloat32(o + 8 + j * 4, true),
          ),
        };
      }),
    };
  }
  validateSource(s);
  // Require a unique wire representation, including mapping padding and identity.
  const canonical = encode(s);
  requireValue(
    canonical.every((n, i) => (i >= 32 && i < 64) || n === bytes[i]),
    "Non-canonical resource",
  );
  return s;
}
module.exports = {
  TYPES,
  SIGNALS,
  BLENDS,
  CLOCKS,
  MAX_BYTES,
  validateSource,
  encode,
  decode,
  digestInput,
};
