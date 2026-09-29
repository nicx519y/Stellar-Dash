// Explicit .cjs keeps Next.js page and Worker bundles in CommonJS mode.
// UIMG v5: fixed 4 KiB header + indexed baseline JPEG frames. Browser/Node shared.
'use strict';
const JPEG_FORMAT = 3;
const JPEG_MAX_FRAMES = 255; // uint8 wire format ceiling; device limit is queried.
const JPEG_FPS = 12;
const JPEG_CAPABILITY = 8;
const HEADER_BYTES = 4096;
function crc32(bytes) {
    let crc = 0xffffffff;
    for (const byte of bytes) {
        crc ^= byte;
        for (let bit = 0; bit < 8; bit++) crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
    }
    return (crc ^ 0xffffffff) >>> 0;
}
function fail() { throw new Error('Invalid baseline JPEG sequence'); }
function jpegInfo(bytes, width = 320, height = 172) {
    if (bytes.length < 4 || bytes[0] !== 255 || bytes[1] !== 216 || bytes[bytes.length - 2] !== 255 || bytes[bytes.length - 1] !== 217) fail();
    let offset = 2, found = false, tables = 0;
    while (offset + 4 <= bytes.length) {
        if (bytes[offset++] !== 255) fail();
        while (bytes[offset] === 255) offset++;
        const marker = bytes[offset++];
        const length = (bytes[offset] << 8) | bytes[offset + 1];
        if (length < 2 || offset + length > bytes.length) fail();
        if (marker === 0xdb) tables |= 1;
        if (marker === 0xc4) tables |= 2;
        if (marker === 0xc0) {
            if (found || length !== 17 || bytes[offset + 2] !== 8 || bytes[offset + 7] !== 3 ||
                ((bytes[offset + 3] << 8) | bytes[offset + 4]) !== height ||
                ((bytes[offset + 5] << 8) | bytes[offset + 6]) !== width ||
                bytes[offset + 8] !== 1 || bytes[offset + 11] !== 2 || bytes[offset + 14] !== 3 ||
                ![0x11, 0x21, 0x22].includes(bytes[offset + 9]) ||
                bytes[offset + 12] !== 0x11 || bytes[offset + 15] !== 0x11) fail();
            found = true;
        } else if (marker === 0xda) {
            if (!found || tables !== 3 || length !== 12 || bytes[offset + 2] !== 3 ||
                bytes[offset + 3] !== 1 || bytes[offset + 5] !== 2 || bytes[offset + 7] !== 3 ||
                bytes[offset + 9] !== 0 || bytes[offset + 10] !== 63 || bytes[offset + 11] !== 0) fail();
            return;
        } else if (![0xdb, 0xc4, 0xdd, 0xfe].includes(marker) && (marker < 0xe0 || marker > 0xef)) fail();
        offset += length;
    }
    fail();
}
function validateJpegPayload(payload, frameCount, width = 320, height = 172) {
    if (!Number.isInteger(frameCount) || frameCount < 1 || frameCount > JPEG_MAX_FRAMES ||
        payload.length < 8 + frameCount * 8) fail();
    const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
    if (view.getUint32(0, true) !== 0x5145534a || view.getUint16(4, true) !== frameCount || view.getUint16(6, true) !== 0) fail();
    let end = 8 + frameCount * 8;
    const known = new Map();
    const entries = [];
    for (let i = 0; i < frameCount; i++) {
        const offset = view.getUint32(8 + i * 8, true), length = view.getUint32(12 + i * 8, true);
        if (offset % 4 || length < 4 || offset > payload.length || length > payload.length - offset) fail();
        if (known.has(offset)) {
            if (known.get(offset) !== length) fail();
        } else {
            if (offset !== end) fail();
            jpegInfo(payload.subarray(offset, offset + length), width, height);
            end = (offset + length + 3) & ~3;
            if (end > payload.length) fail();
            for (let p = offset + length; p < end; p++) if (payload[p] !== 0) fail();
            known.set(offset, length);
        }
        entries.push({ offset, length });
    }
    if (end !== payload.length) fail();
    return entries;
}
function buildJpegPayload(frames) {
    if (!frames.length || frames.length > JPEG_MAX_FRAMES) fail();
    const unique = [], indices = [];
    let size = 8 + frames.length * 8;
    for (const frame of frames) {
        const checksum = crc32(frame);
        let index = unique.findIndex(item => item.bytes === frame || (item.bytes.length === frame.length && item.crc === checksum && item.bytes.every((v, i) => v === frame[i])));
        if (index < 0) {
            jpegInfo(frame);
            index = unique.length;
            unique.push({ bytes: frame, crc: checksum, offset: size });
            size += (frame.length + 3) & ~3;
        }
        indices.push(index);
    }
    const payload = new Uint8Array(size), view = new DataView(payload.buffer);
    view.setUint32(0, 0x5145534a, true); view.setUint16(4, frames.length, true);
    indices.forEach((index, i) => {
        const item = unique[index];
        view.setUint32(8 + i * 8, item.offset, true); view.setUint32(12 + i * 8, item.bytes.length, true);
    });
    unique.forEach(item => payload.set(item.bytes, item.offset));
    return payload;
}
function buildJpegUimg(payload, frameCount, fps = frameCount === 1 ? 0 : JPEG_FPS) {
    if (frameCount === 1 ? fps !== 0 : ![6, JPEG_FPS].includes(fps)) fail();
    validateJpegPayload(payload, frameCount);
    const bytes = new Uint8Array(HEADER_BYTES + payload.length), view = new DataView(bytes.buffer);
    view.setUint32(0, 0x474d4955, true); view.setUint16(4, 5, true);
    bytes[6] = 1; bytes[7] = JPEG_FORMAT;
    view.setUint16(8, 320, true); view.setUint16(10, 172, true);
    bytes[12] = frameCount; bytes[13] = fps;
    view.setUint32(16, 320 * 172 * 2, true); view.setUint32(20, HEADER_BYTES, true);
    view.setUint32(24, payload.length, true);
    bytes.set(new TextEncoder().encode('USER_IMAGE'), 76);
    view.setUint32(92, crc32(payload), true); view.setUint32(96, crc32(bytes.subarray(0, 96)), true);
    bytes.set(payload, HEADER_BYTES);
    return bytes;
}
function parseJpegUimg(bytes) {
    if (bytes.length < HEADER_BYTES) fail();
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    const frameCount = bytes[12], fps = bytes[13], payload = bytes.subarray(HEADER_BYTES);
    const expected = buildJpegUimg(payload, frameCount, fps);
    if (bytes.subarray(0, HEADER_BYTES).some((v, i) => v !== expected[i])) fail();
    return { width: 320, height: 172, frameCount, fps, format: JPEG_FORMAT, payloadBytes: payload.length, payloadCrc32: view.getUint32(92, true), payload };
}
module.exports = { JPEG_FORMAT, JPEG_MAX_FRAMES, JPEG_FPS, JPEG_CAPABILITY, jpegInfo, validateJpegPayload, buildJpegPayload, buildJpegUimg, parseJpegUimg };
