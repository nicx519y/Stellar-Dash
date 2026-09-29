/** Device configuration stays in this browser; a committed backup is required before installation. */
export async function saveReleaseConfigBackup(key: string, backup: Record<string, unknown>): Promise<void> {
  await new Promise<void>((resolve, reject) => {
    const open = indexedDB.open('xora-release-backups', 1);
    open.onupgradeneeded = () => { open.result.createObjectStore('backups'); };
    open.onerror = () => reject(open.error);
    open.onblocked = () => reject(new Error('Close other XORA tabs before saving the configuration backup'));
    open.onsuccess = () => {
      const db = open.result;
      const tx = db.transaction('backups', 'readwrite');
      tx.objectStore('backups').put({ createdAt: new Date().toISOString(), backup }, key);
      tx.oncomplete = () => { db.close(); resolve(); };
      tx.onerror = tx.onabort = () => { db.close(); reject(tx.error || new Error('Configuration backup failed')); };
    };
  });
}
