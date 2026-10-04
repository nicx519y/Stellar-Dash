'use client';

import {
  Box,
  Center,
  Flex,
  Heading,
  HStack,
  Input,
  NativeSelect,
  Spinner,
  Stack,
  Table,
  Text,
} from '@chakra-ui/react';
import { useCallback, useEffect, useMemo, useState } from 'react';
import {
  LuRefreshCw,
  LuSearch,
  LuUsers,
} from 'react-icons/lu';
import { adminRuntime } from '@hbox/admin-runtime';
import { Alert } from '@/components/ui/alert';
import { Button } from '@/components/ui/button';
import { toaster } from '@/components/ui/toaster';
import { AdminCard, AdminPageHeader } from '@/components/admin/admin-surface';
import { useLanguage } from '@/contexts/language-context';
import { useUserAuth } from '@/contexts/user-auth-context';
import type {
  AdminUser,
  AdminUserPage,
} from '@/lib/admin/types';
import { AdminApiError } from '@/lib/admin/types';
import type { AccountRole } from '@/lib/user-auth/types';

const PAGE_SIZE = 20;

const COPY = {
  en: {
    subtitle: 'Manage account roles.',
    signInRequired: 'Sign in with an administrator account to continue.',
    permissionRequired: 'This account does not have administrator permission.',
    users: 'Users',
    usersDescription: 'Role changes take effect on every active session immediately.',
    searchPlaceholder: 'Search email or display name',
    search: 'Search',
    refresh: 'Refresh',
    email: 'Email',
    displayName: 'Display name',
    role: 'Role',
    registered: 'Registered',
    lastLogin: 'Last login',
    admin: 'Admin',
    user: 'User',
    never: 'Never',
    noUsers: 'No users found.',
    previous: 'Previous',
    next: 'Next',
    showing: (from: number, to: number, total: number) =>
      `Showing ${from}-${to} of ${total}`,
    requestFailed: 'The administrator request failed.',
    lastAdmin: 'The final active administrator cannot be downgraded.',
    roleUpdated: 'User role updated.',
  },
  zh: {
    subtitle: '管理账号角色。',
    signInRequired: '请先使用管理员账号登录。',
    permissionRequired: '当前账号没有管理员权限。',
    users: '用户',
    usersDescription: '角色变更会立即作用于该账号的所有现有会话。',
    searchPlaceholder: '搜索邮箱或显示名称',
    search: '搜索',
    refresh: '刷新',
    email: '邮箱',
    displayName: '显示名称',
    role: '角色',
    registered: '注册时间',
    lastLogin: '最近登录',
    admin: '管理员',
    user: '普通用户',
    never: '从未',
    noUsers: '没有找到用户。',
    previous: '上一页',
    next: '下一页',
    showing: (from: number, to: number, total: number) =>
      `显示 ${from}-${to}，共 ${total} 项`,
    requestFailed: '管理员请求失败。',
    lastAdmin: '不能降级最后一个有效管理员。',
    roleUpdated: '用户角色已更新。',
  },
};

export default function AdminUsersPage() {
  const { currentLanguage } = useLanguage();
  const { session, loading: sessionLoading, refreshSession } = useUserAuth();
  const copy = COPY[currentLanguage];
  const [usersPage, setUsersPage] = useState<AdminUserPage | null>(null);
  const [draftQuery, setDraftQuery] = useState('');
  const [query, setQuery] = useState('');
  const [offset, setOffset] = useState(0);
  const [loading, setLoading] = useState(false);
  const [changingUserUid, setChangingUserUid] = useState<string | null>(null);

  const isAdmin = session.authenticated && session.user?.role === 'admin';

  const showError = useCallback((error: unknown) => {
    const description = error instanceof AdminApiError &&
      error.code === 'LAST_ADMIN_REQUIRED'
      ? copy.lastAdmin
      : error instanceof Error && error.message
        ? error.message
        : copy.requestFailed;
    toaster.error({ title: copy.requestFailed, description });
  }, [copy.lastAdmin, copy.requestFailed]);

  const loadData = useCallback(async () => {
    if (!isAdmin) return;
    setLoading(true);
    try {
      setUsersPage(await adminRuntime.listUsers({ query, limit: PAGE_SIZE, offset }));
    } catch (error) {
      showError(error);
    } finally {
      setLoading(false);
    }
  }, [isAdmin, offset, query, showError]);

  useEffect(() => {
    void loadData();
  }, [loadData]);

  const locale = currentLanguage === 'zh' ? 'zh-CN' : 'en-US';
  const formatDate = (value: number | null) => value === null
    ? copy.never
    : new Intl.DateTimeFormat(locale, {
      dateStyle: 'medium',
      timeStyle: 'short',
    }).format(new Date(value));

  const pagingLabel = useMemo(() => {
    if (!usersPage || usersPage.total === 0) return copy.showing(0, 0, 0);
    return copy.showing(
      usersPage.offset + 1,
      Math.min(usersPage.offset + usersPage.users.length, usersPage.total),
      usersPage.total,
    );
  }, [copy, usersPage]);

  const changeRole = async (user: AdminUser, role: AccountRole) => {
    if (user.role === role) return;
    setChangingUserUid(user.uid);
    try {
      const updated = await adminRuntime.changeUserRole(user.uid, role);
      setUsersPage(current => current ? {
        ...current,
        users: current.users.map(item => item.uid === updated.uid ? updated : item),
      } : current);
      if (session.user?.uid === updated.uid) await refreshSession();
      toaster.success({ title: copy.roleUpdated });
    } catch (error) {
      showError(error);
    } finally {
      setChangingUserUid(null);
    }
  };

  return (
    <>
      <AdminPageHeader title={currentLanguage === 'zh' ? '账户管理' : 'Accounts'} description={copy.subtitle} />
      {sessionLoading ? (
        <Center minHeight="320px">
          <Spinner size="lg" colorPalette="green" />
        </Center>
      ) : !session.authenticated ? (
        <Alert colorPalette="orange" title={copy.signInRequired} />
      ) : !isAdmin ? (
        <Alert colorPalette="red" title={copy.permissionRequired} />
      ) : (
        <Stack gap={6}>
          <AdminCard>
            <Stack gap={5}>
              <Flex justifyContent="space-between" gap={4} flexWrap="wrap">
                <Stack gap={1}>
                  <HStack>
                    <LuUsers />
                    <Heading size="lg">{copy.users}</Heading>
                  </HStack>
                  <Text color="fg.muted">{copy.usersDescription}</Text>
                </Stack>
                <Button
                  variant="surface"
                  loading={loading}
                  onClick={() => void loadData()}
                >
                  <LuRefreshCw />
                  {copy.refresh}
                </Button>
              </Flex>
              <Box as="form" onSubmit={event => {
                event.preventDefault();
                setOffset(0);
                setQuery(draftQuery.trim());
              }}>
                <HStack alignItems="stretch">
                  <Input
                    value={draftQuery}
                    onChange={event => setDraftQuery(event.target.value)}
                    placeholder={copy.searchPlaceholder}
                    maxLength={254}
                  />
                  <Button type="submit" colorPalette="green">
                    <LuSearch />
                    {copy.search}
                  </Button>
                </HStack>
              </Box>
              <Box overflowX="auto">
                <Table.Root size="sm" minWidth="900px" interactive>
                  <Table.Header>
                    <Table.Row>
                      <Table.ColumnHeader>{copy.email}</Table.ColumnHeader>
                      <Table.ColumnHeader>{copy.displayName}</Table.ColumnHeader>
                      <Table.ColumnHeader>{copy.role}</Table.ColumnHeader>
                      <Table.ColumnHeader>{copy.registered}</Table.ColumnHeader>
                      <Table.ColumnHeader>{copy.lastLogin}</Table.ColumnHeader>
                    </Table.Row>
                  </Table.Header>
                  <Table.Body>
                    {usersPage?.users.map(user => (
                      <Table.Row key={user.uid}>
                        <Table.Cell>{user.email}</Table.Cell>
                        <Table.Cell>{user.displayName}</Table.Cell>
                        <Table.Cell>
                          <NativeSelect.Root
                            size="sm"
                            width="150px"
                            disabled={changingUserUid === user.uid}
                          >
                            <NativeSelect.Field
                              aria-label={`${copy.role}: ${user.email}`}
                              value={user.role}
                              onChange={event => void changeRole(
                                user,
                                event.target.value as AccountRole,
                              )}
                            >
                              <option value="admin">{copy.admin}</option>
                              <option value="user">{copy.user}</option>
                            </NativeSelect.Field>
                            <NativeSelect.Indicator />
                          </NativeSelect.Root>
                        </Table.Cell>
                        <Table.Cell>{formatDate(user.createdAt)}</Table.Cell>
                        <Table.Cell>{formatDate(user.lastLoginAt)}</Table.Cell>
                      </Table.Row>
                    ))}
                  </Table.Body>
                </Table.Root>
              </Box>
              {usersPage?.users.length === 0 && (
                <Text color="fg.muted" textAlign="center">{copy.noUsers}</Text>
              )}
              <Flex justifyContent="space-between" alignItems="center" gap={3} flexWrap="wrap">
                <Text color="fg.muted" fontSize="sm">{pagingLabel}</Text>
                <HStack>
                  <Button
                    size="sm"
                    variant="surface"
                    disabled={offset === 0}
                    onClick={() => setOffset(Math.max(0, offset - PAGE_SIZE))}
                  >
                    {copy.previous}
                  </Button>
                  <Button
                    size="sm"
                    variant="surface"
                    disabled={!usersPage || offset + PAGE_SIZE >= usersPage.total}
                    onClick={() => setOffset(offset + PAGE_SIZE)}
                  >
                    {copy.next}
                  </Button>
                </HStack>
              </Flex>
            </Stack>
          </AdminCard>
        </Stack>
      )}
    </>
  );
}
