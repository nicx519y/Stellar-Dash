import { AdminApiError } from './types';
import type { FirmwareRuntime, ReleaseImport, ReleaseQuery } from './firmware-types';

async function request<T>(url: string, init?: RequestInit): Promise<T> {
  const response = await fetch(url, { ...init, credentials: 'same-origin', cache: 'no-store', headers: { ...(init?.body ? { 'Content-Type': 'application/json' } : {}), ...init?.headers } });
  const body = await response.json().catch(() => ({}));
  if (!response.ok || body.success !== true) throw new AdminApiError(body.error || 'FIRMWARE_REQUEST_FAILED', body.message || 'Firmware request failed', response.status);
  return body.data;
}
const root = '/api/admin/firmware';
const queryString = (query: ReleaseQuery = {}) => new URLSearchParams(Object.entries(query).map(([k, v]) => [k, String(v)])).toString();
const mutate = <T>(id: string, method: string, body: object, suffix = '') => request<T>(`${root}/releases/${encodeURIComponent(id)}${suffix}`, { method, body: JSON.stringify(body) });

export const firmwareRuntime: FirmwareRuntime = {
  list: query => request(`${root}/releases?${queryString(query)}`),
  detail: id => request(`${root}/releases/${encodeURIComponent(id)}`),
  edit: (id, revision, notes, acceptance) => mutate(id, 'PATCH', { revision, notes, acceptance }),
  publish: (id, revision) => mutate(id, 'POST', { revision }, '/publish'),
  withdraw: (id, revision, reason) => mutate(id, 'POST', { revision, reason }, '/withdraw'),
  remove: (id, revision) => mutate(id, 'DELETE', { revision }),
  legacy: () => request(`${root}/legacy`),
  catalog: query => request(`/api/firmware-releases?${queryString(query)}`),
  importBundle(file, onProgress) {
    return new Promise<ReleaseImport>((resolve, reject) => {
      const xhr = new XMLHttpRequest(); xhr.open('POST', `${root}/imports`); xhr.timeout = 120000;
      xhr.upload.onprogress = e => { if (e.lengthComputable) onProgress(Math.round(e.loaded / e.total * 100)); };
      xhr.onerror = () => reject(new Error('Upload connection failed. Refresh the release list before uploading again.'));
      xhr.ontimeout = () => reject(new Error('Upload result is uncertain. Refresh the release list before uploading again.'));
      xhr.onload = () => {
        try {
          const body = JSON.parse(xhr.responseText);
          if (xhr.status < 200 || xhr.status >= 300 || body.success !== true) throw new Error(body.message || 'Upload failed');
          resolve(body.data);
        } catch (error) { reject(error); }
      };
      const form = new FormData(); form.append('bundle', file); xhr.send(form);
    });
  },
};
