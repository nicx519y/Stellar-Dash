import type { AccountRole, AuthSession } from '@/lib/user-auth/types';
import {
  AdminApiError,
  AdminRuntime,
  AdminUser,
  ServiceTokenMetadata,
  ServiceTokenScope,
} from './types';

const USERS_KEY = 'st-dash-ui-preview-admin-users';
const TOKENS_KEY = 'st-dash-ui-preview-service-tokens';
const SESSION_KEY = 'st-dash-ui-preview-user-session';

const now = () => Date.now();

function defaultUsers(): AdminUser[] {
  const timestamp = now();
  return [
    {
      uid: '00000000-0000-4000-8000-000000000001',
      email: '33618409@qq.com',
      displayName: '33618409',
      role: 'admin',
      createdAt: timestamp,
      updatedAt: timestamp,
      lastLoginAt: timestamp,
      verifiedAt: timestamp,
    },
    {
      uid: '00000000-0000-4000-8000-000000000002',
      email: 'user@example.com',
      displayName: 'Preview user',
      role: 'user',
      createdAt: timestamp,
      updatedAt: timestamp,
      lastLoginAt: null,
      verifiedAt: timestamp,
    },
  ];
}

function readArray<T>(key: string, fallback: () => T[]): T[] {
  const raw = sessionStorage.getItem(key);
  if (!raw) {
    const value = fallback();
    sessionStorage.setItem(key, JSON.stringify(value));
    return value;
  }
  try {
    return JSON.parse(raw) as T[];
  } catch {
    sessionStorage.removeItem(key);
    return readArray(key, fallback);
  }
}

function writeUsers(users: AdminUser[]) {
  sessionStorage.setItem(USERS_KEY, JSON.stringify(users));
}

function writeTokens(tokens: ServiceTokenMetadata[]) {
  sessionStorage.setItem(TOKENS_KEY, JSON.stringify(tokens));
}

function randomSecret(): string {
  const bytes = crypto.getRandomValues(new Uint8Array(32));
  const binary = Array.from(bytes, byte => String.fromCharCode(byte)).join('');
  return `stsvc_${btoa(binary).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '')}`;
}

function updatePreviewSession(user: AdminUser) {
  const raw = sessionStorage.getItem(SESSION_KEY);
  if (!raw) return;
  try {
    const session = JSON.parse(raw) as AuthSession;
    if (session.user?.uid === user.uid) {
      session.user.role = user.role;
      sessionStorage.setItem(SESSION_KEY, JSON.stringify(session));
    }
  } catch {
    // A malformed preview session will be repaired by the auth adapter.
  }
}

export const adminRuntime: AdminRuntime = {
  async listUsers({ query = '', limit = 20, offset = 0 }) {
    const needle = query.trim().toLowerCase();
    const matches = readArray<AdminUser>(USERS_KEY, defaultUsers).filter(user =>
      !needle || user.email.toLowerCase().includes(needle) ||
      user.displayName.toLowerCase().includes(needle),
    );
    return {
      users: matches.slice(offset, offset + limit),
      total: matches.length,
      limit,
      offset,
    };
  },
  async changeUserRole(uid: string, role: AccountRole) {
    const users = readArray<AdminUser>(USERS_KEY, defaultUsers);
    const target = users.find(user => user.uid === uid);
    if (!target) throw new AdminApiError('USER_NOT_FOUND', 'User not found.', 404);
    if (target.role === 'admin' && role === 'user' &&
        users.filter(user => user.role === 'admin').length === 1) {
      throw new AdminApiError(
        'LAST_ADMIN_REQUIRED',
        'The final active administrator cannot be downgraded.',
        409,
      );
    }
    target.role = role;
    target.updatedAt = now();
    writeUsers(users);
    updatePreviewSession(target);
    return target;
  },
  async listServiceTokens() {
    return readArray<ServiceTokenMetadata>(TOKENS_KEY, () => []);
  },
  async createServiceToken(input) {
    if (!input.name.trim() || input.scopes.length === 0) {
      throw new AdminApiError('INVALID_SERVICE_TOKEN', 'Invalid service token.', 400);
    }
    const timestamp = now();
    const token = {
      id: crypto.randomUUID(),
      name: input.name.trim(),
      scopes: [...input.scopes] as ServiceTokenScope[],
      createdAt: timestamp,
      expiresAt: timestamp + input.expiresInDays * 86400000,
    };
    const metadata: ServiceTokenMetadata = {
      id: token.id,
      name: token.name,
      scopes: token.scopes,
      expiresAt: token.expiresAt,
      revokedAt: null,
    };
    const tokens = readArray<ServiceTokenMetadata>(TOKENS_KEY, () => []);
    tokens.unshift(metadata);
    writeTokens(tokens);
    return { token, secret: randomSecret() };
  },
  async revokeServiceToken(id: string) {
    const tokens = readArray<ServiceTokenMetadata>(TOKENS_KEY, () => []);
    const token = tokens.find(item => item.id === id && item.revokedAt === null);
    if (!token) {
      throw new AdminApiError(
        'SERVICE_TOKEN_NOT_FOUND',
        'Active service token not found.',
        404,
      );
    }
    token.revokedAt = now();
    writeTokens(tokens);
  },
};
