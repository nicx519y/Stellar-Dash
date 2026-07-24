import { DeviceTransportError } from './types';

export const SECURE_HID_REPORT_VERSION = 1;
export const SECURE_HID_REPORT_SIZE = 64;
export const SECURE_HID_HEADER_SIZE = 8;
export const SECURE_HID_PAYLOAD_SIZE = 44;
export const SECURE_HID_TAG_SIZE = 12;

export enum SecureHidFrameType {
  BOOTSTRAP_REQUEST = 0x01,
  BOOTSTRAP_RESPONSE = 0x02,
  RPC_REQUEST = 0x10,
  RPC_RESPONSE = 0x11,
  EVENT = 0x12,
  PERF_SAMPLE = 0x20,
  PERF_EDGE = 0x21,
  PERF_CHECKPOINT = 0x22,
  STREAM_CHUNK = 0x30,
  ERROR = 0x7f,
}

export enum SecureHidFrameFlags {
  SECURE = 1 << 0,
  FRAGMENTED = 1 << 1,
  LAST = 1 << 2,
  ACK_REQUIRED = 1 << 3,
}

export interface SecureHidFrame {
  version: number;
  type: SecureHidFrameType;
  flags: number;
  payloadLength: number;
  sequence: number;
  payload: Uint8Array;
  secure: boolean;
}

export interface HidSessionCipher {
  seal(header: Uint8Array, sequence: number, plaintext: Uint8Array): Promise<{
    ciphertext: Uint8Array;
    tag: Uint8Array;
  }>;
  open(
    header: Uint8Array,
    sequence: number,
    ciphertext: Uint8Array,
    tag: Uint8Array,
  ): Promise<Uint8Array>;
}

const BOOTSTRAP_TYPES = new Set<SecureHidFrameType>([
  SecureHidFrameType.BOOTSTRAP_REQUEST,
  SecureHidFrameType.BOOTSTRAP_RESPONSE,
  SecureHidFrameType.ERROR,
]);

function assertPayloadLength(length: number): void {
  if (!Number.isInteger(length) || length < 0 || length > SECURE_HID_PAYLOAD_SIZE) {
    throw new DeviceTransportError(
      'protocol',
      `SecureHidReportV1 payload length ${length} exceeds ${SECURE_HID_PAYLOAD_SIZE}`,
    );
  }
}

export function isBootstrapFrameType(type: SecureHidFrameType): boolean {
  return BOOTSTRAP_TYPES.has(type);
}

export class SecureHidReportCodec {
  constructor(private cipher: HidSessionCipher | null = null) {}

  setCipher(cipher: HidSessionCipher | null): void {
    this.cipher = cipher;
  }

  async encode(frame: {
    type: SecureHidFrameType;
    flags: number;
    sequence: number;
    payload: Uint8Array;
    secure: boolean;
  }): Promise<Uint8Array> {
    assertPayloadLength(frame.payload.byteLength);
    if (!Number.isSafeInteger(frame.sequence) || frame.sequence <= 0 || frame.sequence > 0xffffffff) {
      throw new DeviceTransportError('protocol', 'SecureHidReportV1 sequence must be a non-zero u32');
    }
    if (!frame.secure && !isBootstrapFrameType(frame.type)) {
      throw new DeviceTransportError('authentication-required', 'Protected HID frame cannot be sent before authentication');
    }
    if (frame.secure && !this.cipher) {
      throw new DeviceTransportError('authentication-required', 'Secure HID session is not established');
    }

    const report = new Uint8Array(SECURE_HID_REPORT_SIZE);
    report[0] = SECURE_HID_REPORT_VERSION;
    report[1] = frame.type;
    report[2] = frame.flags | (frame.secure ? SecureHidFrameFlags.SECURE : 0);
    report[3] = frame.payload.byteLength;
    new DataView(report.buffer).setUint32(4, frame.sequence, true);
    const header = report.slice(0, SECURE_HID_HEADER_SIZE);

    if (frame.secure) {
      const sealed = await this.cipher!.seal(header, frame.sequence, frame.payload);
      if (
        sealed.ciphertext.byteLength !== frame.payload.byteLength ||
        sealed.tag.byteLength !== SECURE_HID_TAG_SIZE
      ) {
        throw new DeviceTransportError('protocol', 'AES-GCM cipher returned an invalid HID frame size');
      }
      report.set(sealed.ciphertext, SECURE_HID_HEADER_SIZE);
      report.set(sealed.tag, SECURE_HID_HEADER_SIZE + SECURE_HID_PAYLOAD_SIZE);
    } else {
      report.set(frame.payload, SECURE_HID_HEADER_SIZE);
    }
    return report;
  }

  async decode(source: ArrayBuffer | Uint8Array | DataView): Promise<SecureHidFrame> {
    const bytes = toExactUint8Array(source);
    if (bytes.byteLength !== SECURE_HID_REPORT_SIZE) {
      throw new DeviceTransportError(
        'protocol',
        `SecureHidReportV1 must be exactly ${SECURE_HID_REPORT_SIZE} bytes`,
      );
    }
    if (bytes[0] !== SECURE_HID_REPORT_VERSION) {
      throw new DeviceTransportError('protocol', `Unsupported SecureHidReport version ${bytes[0]}`);
    }
    const type = bytes[1] as SecureHidFrameType;
    const flags = bytes[2];
    const payloadLength = bytes[3];
    assertPayloadLength(payloadLength);
    const sequence = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength).getUint32(4, true);
    if (sequence === 0) {
      throw new DeviceTransportError('protocol', 'SecureHidReportV1 sequence zero is reserved');
    }
    const secure = (flags & SecureHidFrameFlags.SECURE) !== 0;
    if (!secure && !isBootstrapFrameType(type)) {
      throw new DeviceTransportError('authentication-required', 'Received an unauthenticated protected HID frame');
    }
    const paddedPayload = bytes.slice(
      SECURE_HID_HEADER_SIZE,
      SECURE_HID_HEADER_SIZE + SECURE_HID_PAYLOAD_SIZE,
    );
    for (let index = payloadLength; index < paddedPayload.byteLength; index += 1) {
      if (paddedPayload[index] !== 0) {
        throw new DeviceTransportError('protocol', 'SecureHidReportV1 contains non-zero payload padding');
      }
    }
    const header = bytes.slice(0, SECURE_HID_HEADER_SIZE);
    const ciphertext = paddedPayload.slice(0, payloadLength);
    const tag = bytes.slice(
      SECURE_HID_HEADER_SIZE + SECURE_HID_PAYLOAD_SIZE,
      SECURE_HID_REPORT_SIZE,
    );

    let payload: Uint8Array;
    if (secure) {
      if (!this.cipher) {
        throw new DeviceTransportError('authentication-required', 'Received secure HID data before session setup');
      }
      payload = await this.cipher.open(header, sequence, ciphertext, tag);
    } else {
      if (tag.some((value) => value !== 0)) {
        throw new DeviceTransportError('protocol', 'Bootstrap HID report has a non-zero authentication tag');
      }
      payload = ciphertext;
    }

    return {
      version: bytes[0],
      type,
      flags,
      payloadLength,
      sequence,
      payload,
      secure,
    };
  }
}

export function fragmentPayload(payload: Uint8Array): Uint8Array[] {
  if (payload.byteLength === 0) {
    return [new Uint8Array()];
  }
  const fragments: Uint8Array[] = [];
  for (let offset = 0; offset < payload.byteLength; offset += SECURE_HID_PAYLOAD_SIZE) {
    fragments.push(payload.slice(offset, offset + SECURE_HID_PAYLOAD_SIZE));
  }
  return fragments;
}

export class FragmentAssembler {
  private chunks: Uint8Array[] = [];
  private length = 0;
  private active = false;

  constructor(private readonly maximumLength = 1024 * 1024) {}

  push(frame: SecureHidFrame): Uint8Array | null {
    const fragmented = (frame.flags & SecureHidFrameFlags.FRAGMENTED) !== 0;
    const last = (frame.flags & SecureHidFrameFlags.LAST) !== 0;
    if (!this.active) {
      this.active = true;
    } else if (!fragmented) {
      this.reset();
      throw new DeviceTransportError('protocol', 'Unfragmented HID message interleaved with an active message');
    }
    this.length += frame.payload.byteLength;
    if (this.length > this.maximumLength) {
      this.reset();
      throw new DeviceTransportError('protocol', 'Fragmented HID message exceeds the configured limit');
    }
    this.chunks.push(frame.payload);
    if (fragmented && !last) {
      return null;
    }
    const value = new Uint8Array(this.length);
    let offset = 0;
    for (const chunk of this.chunks) {
      value.set(chunk, offset);
      offset += chunk.byteLength;
    }
    this.reset();
    return value;
  }

  reset(): void {
    this.chunks = [];
    this.length = 0;
    this.active = false;
  }
}

export function toExactUint8Array(source: ArrayBuffer | Uint8Array | DataView): Uint8Array {
  if (source instanceof Uint8Array) {
    return source.slice();
  }
  if (source instanceof DataView) {
    return new Uint8Array(source.buffer, source.byteOffset, source.byteLength).slice();
  }
  return new Uint8Array(source.slice(0));
}
