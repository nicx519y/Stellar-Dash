import type { AccountRole } from '@/lib/user-auth/types';
import {
  AdminApiError,
  AdminRuntime,
  AdminUser,
  AdminUserPage,
  CreatedServiceToken,
  ServiceTokenMetadata,
} from './types';

interface ApiEnvelope<T> {
  success: true;
  data: T;
}

async function apiRequest<T>(path: string, init?: RequestInit): Promise<T> {
  const response = await fetch(path, {
    ...init,
    credentials: 'same-origin',
    cache: 'no-store',
    headers: {
      ...(init?.body ? { 'Content-Type': 'application/json' } : {}),
      ...init?.headers,
    },
  });
  if (!response.ok) {
    let payload: { error?: string; message?: string } = {};
    try {
      payload = await response.json();
    } catch {
      // Proxy error pages are intentionally reduced to a generic message.
    }
    throw new AdminApiError(
      payload.error || 'ADMIN_REQUEST_FAILED',
      payload.message || 'Administrator request failed.',
      response.status,
    );
  }
  if (response.status === 204) return undefined as T;
  const envelope = await response.json() as ApiEnvelope<T>;
  return envelope.data;
}

export const adminRuntime: AdminRuntime = {
  listUsers(input) {
    const query = new URLSearchParams({
      query: input.query || '',
      limit: String(input.limit || 20),
      offset: String(input.offset || 0),
    });
    return apiRequest<AdminUserPage>(`/api/admin/users?${query}`);
  },
  async changeUserRole(uid: string, role: AccountRole) {
    const data = await apiRequest<{ user: AdminUser }>(
      `/api/admin/users/${encodeURIComponent(uid)}/role`,
      { method: 'PATCH', body: JSON.stringify({ role }) },
    );
    return data.user;
  },
  async listServiceTokens() {
    const data = await apiRequest<{ tokens: ServiceTokenMetadata[] }>(
      '/api/admin/service-tokens',
    );
    return data.tokens;
  },
  createServiceToken(input) {
    return apiRequest<CreatedServiceToken>('/api/admin/service-tokens', {
      method: 'POST',
      body: JSON.stringify(input),
    });
  },
  revokeServiceToken(id: string) {
    return apiRequest<void>(
      `/api/admin/service-tokens/${encodeURIComponent(id)}`,
      { method: 'DELETE' },
    );
  },
};
