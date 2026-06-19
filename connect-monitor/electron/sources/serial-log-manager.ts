import type { SerialLogLine, SerialPortInfo } from "../../shared/monitor-types";

type PublishSerialLogsFn = (lines: SerialLogLine[]) => void;

const LOG_SLOT_COUNT = 2;

function loadSerialPortCtor(): any | null {
  try {
    const serialport = require("serialport");
    return serialport.SerialPort ?? serialport;
  } catch (_err) {
    return null;
  }
}

function displayNameForPort(port: any): string {
  const path = String(port?.path ?? "");
  const friendlyName = typeof port?.friendlyName === "string" ? port.friendlyName.trim() : "";
  const manufacturer = typeof port?.manufacturer === "string" ? port.manufacturer.trim() : "";
  if (friendlyName && friendlyName !== path) return `${path} - ${friendlyName}`;
  if (manufacturer) return `${path} - ${manufacturer}`;
  return path;
}

function normalizePortInfo(port: any): SerialPortInfo | null {
  const path = typeof port?.path === "string" ? port.path : "";
  if (!path) return null;
  return {
    path,
    displayName: displayNameForPort(port),
    manufacturer: typeof port?.manufacturer === "string" ? port.manufacturer : undefined,
    friendlyName: typeof port?.friendlyName === "string" ? port.friendlyName : undefined,
    vendorId: typeof port?.vendorId === "string" ? port.vendorId : undefined,
    productId: typeof port?.productId === "string" ? port.productId : undefined,
  };
}

function serialLogId(portPath: string, timestampMs: number, seq: number): string {
  const timePart = String(timestampMs).padStart(13, "0");
  const seqPart = String(seq).padStart(8, "0");
  return `${portPath}|${timePart}|${seqPart}`;
}

export class SerialLogManager {
  private readonly selections: string[] = Array.from({ length: LOG_SLOT_COUNT }, () => "");
  private readonly opened = new Map<string, { port: any; lineBuffer: string }>();
  private readonly baudRate: number;
  private readonly scanTimer: NodeJS.Timeout;
  private serialPortCtor: any | null | undefined;
  private disposed = false;
  private seq = 0;

  constructor(private readonly publish: PublishSerialLogsFn) {
    this.baudRate = Number(process.env.MONITOR_SERIAL_LOG_BAUD ?? process.env.MONITOR_SERIAL_BAUD ?? "115200") || 115200;
    this.scanTimer = setInterval(() => {
      this.syncOpenPorts();
    }, 2000);
    this.scanTimer.unref?.();
  }

  async listPorts(): Promise<SerialPortInfo[]> {
    const SerialPortCtor = this.getSerialPortCtor();
    if (!SerialPortCtor) return [];
    try {
      const ports = await SerialPortCtor.list();
      return ports
        .map((port: any) => normalizePortInfo(port))
        .filter((port: SerialPortInfo | null): port is SerialPortInfo => Boolean(port));
    } catch (_err) {
      return [];
    }
  }

  getSelections(): string[] {
    return [...this.selections];
  }

  setSelections(nextSelections: Array<string | null | undefined>): string[] {
    for (let slot = 0; slot < LOG_SLOT_COUNT; slot++) {
      this.selections[slot] = typeof nextSelections[slot] === "string" ? nextSelections[slot]?.trim() ?? "" : "";
    }
    this.syncOpenPorts();
    return this.getSelections();
  }

  dispose(): void {
    this.disposed = true;
    clearInterval(this.scanTimer);
    for (const record of this.opened.values()) {
      try {
        record.port.close();
      } catch (_err) {
      }
    }
    this.opened.clear();
  }

  private getSerialPortCtor(): any | null {
    if (this.serialPortCtor !== undefined) return this.serialPortCtor;
    this.serialPortCtor = loadSerialPortCtor();
    return this.serialPortCtor;
  }

  private selectedPortSet(): Set<string> {
    return new Set(this.selections.filter(Boolean));
  }

  private syncOpenPorts(): void {
    if (this.disposed) return;
    const selectedPorts = this.selectedPortSet();
    for (const [path, record] of this.opened) {
      if (!selectedPorts.has(path)) {
        this.opened.delete(path);
        try {
          record.port.close();
        } catch (_err) {
        }
      }
    }
    for (const path of selectedPorts) {
      this.openPort(path);
    }
  }

  private openPort(path: string): void {
    if (this.disposed || this.opened.has(path)) return;
    const SerialPortCtor = this.getSerialPortCtor();
    if (!SerialPortCtor) return;

    const port = new SerialPortCtor({ path, baudRate: this.baudRate, autoOpen: false });
    const record = { port, lineBuffer: "" };
    this.opened.set(path, record);

    port.on("data", (chunk: Uint8Array) => {
      record.lineBuffer += Buffer.from(chunk).toString("utf8");
      if (record.lineBuffer.length > 8192) {
        this.publishText(path, record.lineBuffer.slice(0, 8192));
        record.lineBuffer = record.lineBuffer.slice(8192);
      }

      const lines = record.lineBuffer.split(/\r\n|\n|\r/);
      record.lineBuffer = lines.pop() ?? "";
      const logLines = lines
        .filter((line) => line.length > 0)
        .map((line) => this.createLogLine(path, line));
      if (logLines.length > 0) {
        this.publish(logLines);
      }
    });

    port.on("error", (err: unknown) => {
      this.publishText(path, `[serial] ${err instanceof Error ? err.message : String(err)}`);
    });

    port.on("close", () => {
      this.opened.delete(path);
    });

    port.open((err: unknown) => {
      if (!err && !this.disposed) return;
      this.opened.delete(path);
      try {
        port.close();
      } catch (_closeErr) {
      }
      if (err) {
        this.publishText(path, `[serial] ${err instanceof Error ? err.message : String(err)}`);
      }
    });
  }

  private publishText(portPath: string, text: string): void {
    if (!text) return;
    this.publish([this.createLogLine(portPath, text)]);
  }

  private createLogLine(portPath: string, text: string): SerialLogLine {
    const timestampMs = Date.now();
    this.seq = (this.seq + 1) >>> 0;
    return {
      id: serialLogId(portPath, timestampMs, this.seq),
      timestampMs,
      portPath,
      text,
    };
  }
}
