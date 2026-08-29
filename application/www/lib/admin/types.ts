import type { AccountRole } from '@/lib/user-auth/types';

export type ServiceTokenScope = 'device.manage' | 'firmware.manage';

export interface AdminUser {
  uid: string;
  email: string;
  displayName: string;
  role: AccountRole;
  createdAt: number;
  updatedAt: number;
  lastLoginAt: number | null;
  verifiedAt: number;
}

export interface AdminUserPage {
  users: AdminUser[];
  total: number;
  limit: number;
  offset: number;
}

export interface ServiceTokenMetadata {
  id: string;
  name: string;
  scopes: ServiceTokenScope[];
  expiresAt: number;
  revokedAt: number | null;
}

export interface CreatedServiceToken {
  token: {
    id: string;
    name: string;
    scopes: ServiceTokenScope[];
    createdAt: number;
    expiresAt: number;
  };
  secret: string;
}

export interface AdminRuntime {
  listUsers(input: {
    query?: string;
    limit?: number;
    offset?: number;
  }): Promise<AdminUserPage>;
  changeUserRole(uid: string, role: AccountRole): Promise<AdminUser>;
  listServiceTokens(): Promise<ServiceTokenMetadata[]>;
  createServiceToken(input: {
    name: string;
    scopes: ServiceTokenScope[];
    expiresInDays: number;
  }): Promise<CreatedServiceToken>;
  revokeServiceToken(id: string): Promise<void>;
}

export class AdminApiError extends Error {
  constructor(
    public readonly code: string,
    message: string,
    public readonly status: number,
  ) {
    super(message);
    this.name = 'AdminApiError';
  }
}
