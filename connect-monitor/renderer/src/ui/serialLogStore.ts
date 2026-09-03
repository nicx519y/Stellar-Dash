import type { SerialLogLine } from "../../../shared/monitor-types";

const DB_NAME = "connect-monitor-serial-logs";
const DB_VERSION = 1;
const STORE_NAME = "serial_logs";
const PORT_INDEX = "portPath";
const MAX_LOGS_PER_PORT = 500;

let dbPromise: Promise<IDBDatabase> | null = null;

function openDatabase(): Promise<IDBDatabase> {
  if (dbPromise) return dbPromise;

  dbPromise = new Promise((resolve, reject) => {
    const request = indexedDB.open(DB_NAME, DB_VERSION);

    request.onupgradeneeded = () => {
      const db = request.result;
      const store = db.objectStoreNames.contains(STORE_NAME)
        ? request.transaction?.objectStore(STORE_NAME)
        : db.createObjectStore(STORE_NAME, { keyPath: "id" });
      if (store && !store.indexNames.contains(PORT_INDEX)) {
        store.createIndex(PORT_INDEX, "portPath", { unique: false });
      }
    };

    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error ?? new Error("Failed to open serial log database"));
  });

  return dbPromise;
}

export async function appendSerialLogLines(lines: SerialLogLine[]): Promise<void> {
  if (lines.length === 0) return;
  const db = await openDatabase();

  await new Promise<void>((resolve, reject) => {
    const tx = db.transaction(STORE_NAME, "readwrite");
    const store = tx.objectStore(STORE_NAME);

    tx.oncomplete = () => resolve();
    tx.onerror = () => reject(tx.error ?? new Error("Failed to write serial logs"));
    tx.onabort = () => reject(tx.error ?? new Error("Serial log write was aborted"));

    for (const line of lines) {
      store.put(line);
    }
  });
}

export async function clearSerialLogLines(): Promise<void> {
  const db = await openDatabase();

  await new Promise<void>((resolve, reject) => {
    const tx = db.transaction(STORE_NAME, "readwrite");
    const store = tx.objectStore(STORE_NAME);

    tx.oncomplete = () => resolve();
    tx.onerror = () => reject(tx.error ?? new Error("Failed to clear serial logs"));
    tx.onabort = () => reject(tx.error ?? new Error("Serial log clear was aborted"));

    store.clear();
  });
}

export async function loadSerialLogLines(portPath: string, limit = MAX_LOGS_PER_PORT): Promise<SerialLogLine[]> {
  if (!portPath || limit <= 0) return [];
  const db = await openDatabase();

  return new Promise((resolve, reject) => {
    const tx = db.transaction(STORE_NAME, "readonly");
    const store = tx.objectStore(STORE_NAME);
    const range = IDBKeyRange.only(portPath);
    const request = store.index(PORT_INDEX).openCursor(range, "prev");
    const rows: SerialLogLine[] = [];

    request.onsuccess = () => {
      const cursor = request.result;
      if (!cursor || rows.length >= limit) {
        resolve(rows.reverse());
        return;
      }
      rows.push(cursor.value as SerialLogLine);
      cursor.continue();
    };
    request.onerror = () => reject(request.error ?? new Error("Failed to load serial logs"));
  });
}

export async function loadAllSerialLogLines(portPath: string): Promise<SerialLogLine[]> {
  if (!portPath) return [];
  const db = await openDatabase();

  return new Promise((resolve, reject) => {
    const tx = db.transaction(STORE_NAME, "readonly");
    const store = tx.objectStore(STORE_NAME);
    const range = IDBKeyRange.only(portPath);
    const request = store.index(PORT_INDEX).openCursor(range, "next");
    const rows: SerialLogLine[] = [];

    request.onsuccess = () => {
      const cursor = request.result;
      if (!cursor) {
        resolve(rows);
        return;
      }
      rows.push(cursor.value as SerialLogLine);
      cursor.continue();
    };
    request.onerror = () => reject(request.error ?? new Error("Failed to load serial logs"));
  });
}
