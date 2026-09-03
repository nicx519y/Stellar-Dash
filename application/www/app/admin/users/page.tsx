'use client';

import {
  Badge,
  Box,
  Center,
  Container,
  Dialog,
  Flex,
  Heading,
  HStack,
  Input,
  NativeSelect,
  Portal,
  Spinner,
  Stack,
  Table,
  Text,
} from '@chakra-ui/react';
import { useCallback, useEffect, useMemo, useState } from 'react';
import {
  LuArrowLeft,
  LuCheck,
  LuClipboard,
  LuKeyRound,
  LuImages,
  LuRefreshCw,
  LuSearch,
  LuShield,
  LuTrash2,
  LuUsers,
} from 'react-icons/lu';
import { adminRuntime } from '@hbox/admin-runtime';
import { Alert } from '@/components/ui/alert';
import { Button } from '@/components/ui/button';
import { CloseButton } from '@/components/ui/close-button';
import { Field } from '@/components/ui/field';
import { toaster } from '@/components/ui/toaster';
import { LanguageSwitcher } from '@/components/language-switcher';
import { UserAuthControl } from '@/components/user-auth-control';
import { useLanguage } from '@/contexts/language-context';
import { useUserAuth } from '@/contexts/user-auth-context';
import type {
  AdminUser,
  AdminUserPage,
  ServiceTokenMetadata,
  ServiceTokenScope,
} from '@/lib/admin/types';
import { AdminApiError } from '@/lib/admin/types';
import type { AccountRole } from '@/lib/user-auth/types';

const PAGE_SIZE = 20;

const COPY = {
  en: {
    title: 'Administration',
    subtitle: 'Manage account roles and scoped service tokens.',
    back: 'Back to WebConfig',
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
    tokens: 'Service tokens',
    tokensDescription: 'Use scoped tokens for release and device automation. Secrets are shown only once.',
    tokenName: 'Token name',
    tokenNamePlaceholder: 'Release automation',
    expires: 'Expires in days',
    scopes: 'Scopes',
    createToken: 'Create token',
    tokenCreated: 'Service token created',
    tokenSecretWarning: 'Copy this secret now. It cannot be viewed again after this dialog closes.',
    copy: 'Copy',
    copied: 'Copied',
    close: 'Close',
    expiry: 'Expiry',
    status: 'Status',
    actions: 'Actions',
    active: 'Active',
    expired: 'Expired',
    revoked: 'Revoked',
    revoke: 'Revoke',
    noTokens: 'No service tokens have been created.',
    requestFailed: 'The administrator request failed.',
    lastAdmin: 'The final active administrator cannot be downgraded.',
    roleUpdated: 'User role updated.',
    tokenRevoked: 'Service token revoked.',
    selectScope: 'Select at least one scope.',
  },
  zh: {
    title: '管理后台',
    subtitle: '管理账号角色与限定权限的服务令牌。',
    back: '返回 WebConfig',
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
    tokens: '服务令牌',
    tokensDescription: '为发版和设备自动化创建限定范围的令牌。令牌密钥只显示一次。',
    tokenName: '令牌名称',
    tokenNamePlaceholder: '发版自动化',
    expires: '有效天数',
    scopes: '权限范围',
    createToken: '创建令牌',
    tokenCreated: '服务令牌已创建',
    tokenSecretWarning: '请立即复制密钥。关闭此对话框后无法再次查看。',
    copy: '复制',
    copied: '已复制',
    close: '关闭',
    expiry: '到期时间',
    status: '状态',
    actions: '操作',
    active: '有效',
    expired: '已过期',
    revoked: '已撤销',
    revoke: '撤销',
    noTokens: '尚未创建服务令牌。',
    requestFailed: '管理员请求失败。',
    lastAdmin: '不能降级最后一个有效管理员。',
    roleUpdated: '用户角色已更新。',
    tokenRevoked: '服务令牌已撤销。',
    selectScope: '请至少选择一个权限范围。',
  },
};

function serviceTokenStatus(token: ServiceTokenMetadata) {
  if (token.revokedAt !== null) return 'revoked';
  if (token.expiresAt <= Date.now()) return 'expired';
  return 'active';
}

export default function AdminUsersPage() {
  const { currentLanguage } = useLanguage();
  const { session, loading: sessionLoading, refreshSession } = useUserAuth();
  const copy = COPY[currentLanguage];
  const [usersPage, setUsersPage] = useState<AdminUserPage | null>(null);
  const [tokens, setTokens] = useState<ServiceTokenMetadata[]>([]);
  const [draftQuery, setDraftQuery] = useState('');
  const [query, setQuery] = useState('');
  const [offset, setOffset] = useState(0);
  const [loading, setLoading] = useState(false);
  const [changingUserUid, setChangingUserUid] = useState<string | null>(null);
  const [revokingTokenId, setRevokingTokenId] = useState<string | null>(null);
  const [tokenName, setTokenName] = useState('');
  const [expiresInDays, setExpiresInDays] = useState('90');
  const [scopes, setScopes] = useState<ServiceTokenScope[]>(['device.manage']);
  const [creatingToken, setCreatingToken] = useState(false);
  const [createdSecret, setCreatedSecret] = useState('');

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
      const [nextUsers, nextTokens] = await Promise.all([
        adminRuntime.listUsers({ query, limit: PAGE_SIZE, offset }),
        adminRuntime.listServiceTokens(),
      ]);
      setUsersPage(nextUsers);
      setTokens(nextTokens);
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

  const toggleScope = (scope: ServiceTokenScope) => {
    setScopes(current => current.includes(scope)
      ? current.filter(item => item !== scope)
      : [...current, scope]);
  };

  const createToken = async (event: React.FormEvent) => {
    event.preventDefault();
    if (scopes.length === 0) {
      toaster.error({ title: copy.selectScope });
      return;
    }
    setCreatingToken(true);
    try {
      const created = await adminRuntime.createServiceToken({
        name: tokenName,
        scopes,
        expiresInDays: Number(expiresInDays),
      });
      setCreatedSecret(created.secret);
      setTokenName('');
      setExpiresInDays('90');
      setTokens(await adminRuntime.listServiceTokens());
    } catch (error) {
      showError(error);
    } finally {
      setCreatingToken(false);
    }
  };

  const revokeToken = async (id: string) => {
    setRevokingTokenId(id);
    try {
      await adminRuntime.revokeServiceToken(id);
      setTokens(await adminRuntime.listServiceTokens());
      toaster.success({ title: copy.tokenRevoked });
    } catch (error) {
      showError(error);
    } finally {
      setRevokingTokenId(null);
    }
  };

  return (
    <Box minHeight="100vh" bg="app.canvas">
      <Container maxWidth="7xl" py={{ base: 5, md: 8 }}>
        <Stack gap={6}>
          <Flex
            as="header"
            alignItems={{ base: 'flex-start', md: 'center' }}
            justifyContent="space-between"
            gap={4}
            direction={{ base: 'column', md: 'row' }}
          >
            <Stack gap={1}>
              <HStack>
                <LuShield />
                <Heading size="2xl">{copy.title}</Heading>
              </HStack>
              <Text color="fg.muted">{copy.subtitle}</Text>
            </Stack>
            <HStack gap={2} flexWrap="wrap">
              <Button variant="surface" onClick={() => {
                window.location.href = '/admin/images/';
              }}>
                <LuImages />
                {currentLanguage === 'zh' ? '官方图库' : 'Official Gallery'}
              </Button>
              <Button variant="surface" onClick={() => {
                window.location.href = '/';
              }}>
                <LuArrowLeft />
                {copy.back}
              </Button>
              <UserAuthControl />
              <LanguageSwitcher />
            </HStack>
          </Flex>

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
              <Box
                bg="app.panel"
                borderWidth="1px"
                borderColor="app.border"
                borderRadius="xl"
                p={{ base: 4, md: 6 }}
              >
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
                  <Flex justifyContent="space-between" alignItems="center" gap={3}>
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
              </Box>

              <Box
                bg="app.panel"
                borderWidth="1px"
                borderColor="app.border"
                borderRadius="xl"
                p={{ base: 4, md: 6 }}
              >
                <Stack gap={5}>
                  <Stack gap={1}>
                    <HStack>
                      <LuKeyRound />
                      <Heading size="lg">{copy.tokens}</Heading>
                    </HStack>
                    <Text color="fg.muted">{copy.tokensDescription}</Text>
                  </Stack>
                  <Box as="form" onSubmit={createToken}>
                    <Stack gap={4}>
                      <Flex gap={4} direction={{ base: 'column', md: 'row' }}>
                        <Field label={copy.tokenName} required flex="1">
                          <Input
                            value={tokenName}
                            onChange={event => setTokenName(event.target.value)}
                            placeholder={copy.tokenNamePlaceholder}
                            minLength={1}
                            maxLength={80}
                            required
                          />
                        </Field>
                        <Field label={copy.expires} required width={{ base: 'full', md: '180px' }}>
                          <Input
                            type="number"
                            value={expiresInDays}
                            onChange={event => setExpiresInDays(event.target.value)}
                            min={1}
                            max={365}
                            required
                          />
                        </Field>
                      </Flex>
                      <Field label={copy.scopes} required>
                        <HStack flexWrap="wrap">
                          {(['device.manage', 'firmware.manage'] as ServiceTokenScope[]).map(scope => {
                            const selected = scopes.includes(scope);
                            return (
                              <Button
                                key={scope}
                                type="button"
                                variant={selected ? 'solid' : 'surface'}
                                colorPalette={selected ? 'green' : 'gray'}
                                onClick={() => toggleScope(scope)}
                                aria-pressed={selected}
                              >
                                {selected && <LuCheck />}
                                {scope}
                              </Button>
                            );
                          })}
                        </HStack>
                      </Field>
                      <Button
                        type="submit"
                        colorPalette="green"
                        loading={creatingToken}
                        alignSelf="flex-start"
                      >
                        <LuKeyRound />
                        {copy.createToken}
                      </Button>
                    </Stack>
                  </Box>
                  <Box overflowX="auto">
                    <Table.Root size="sm" minWidth="720px" interactive>
                      <Table.Header>
                        <Table.Row>
                          <Table.ColumnHeader>{copy.tokenName}</Table.ColumnHeader>
                          <Table.ColumnHeader>{copy.scopes}</Table.ColumnHeader>
                          <Table.ColumnHeader>{copy.expiry}</Table.ColumnHeader>
                          <Table.ColumnHeader>{copy.status}</Table.ColumnHeader>
                          <Table.ColumnHeader>{copy.actions}</Table.ColumnHeader>
                        </Table.Row>
                      </Table.Header>
                      <Table.Body>
                        {tokens.map(token => {
                          const status = serviceTokenStatus(token);
                          return (
                            <Table.Row key={token.id}>
                              <Table.Cell>{token.name}</Table.Cell>
                              <Table.Cell>
                                <HStack flexWrap="wrap">
                                  {token.scopes.map(scope => (
                                    <Badge key={scope} variant="surface" colorPalette="purple">
                                      {scope}
                                    </Badge>
                                  ))}
                                </HStack>
                              </Table.Cell>
                              <Table.Cell>{formatDate(token.expiresAt)}</Table.Cell>
                              <Table.Cell>
                                <Badge
                                  colorPalette={status === 'active'
                                    ? 'green'
                                    : status === 'expired' ? 'orange' : 'red'}
                                >
                                  {copy[status]}
                                </Badge>
                              </Table.Cell>
                              <Table.Cell>
                                <Button
                                  size="sm"
                                  variant="surface"
                                  colorPalette="red"
                                  disabled={status !== 'active'}
                                  loading={revokingTokenId === token.id}
                                  onClick={() => void revokeToken(token.id)}
                                >
                                  <LuTrash2 />
                                  {copy.revoke}
                                </Button>
                              </Table.Cell>
                            </Table.Row>
                          );
                        })}
                      </Table.Body>
                    </Table.Root>
                  </Box>
                  {tokens.length === 0 && (
                    <Text color="fg.muted" textAlign="center">{copy.noTokens}</Text>
                  )}
                </Stack>
              </Box>
            </Stack>
          )}
        </Stack>
      </Container>

      <Portal>
        <Dialog.Root
          open={createdSecret.length > 0}
          onOpenChange={details => {
            if (!details.open) setCreatedSecret('');
          }}
        >
          <Dialog.Backdrop backdropFilter="blur(4px)" />
          <Dialog.Positioner alignItems="flex-start" pt={16}>
            <Dialog.Content width="min(92vw, 620px)">
              <Dialog.Header>
                <Dialog.Title>{copy.tokenCreated}</Dialog.Title>
              </Dialog.Header>
              <Dialog.Body>
                <Stack gap={4}>
                  <Alert colorPalette="orange" title={copy.tokenSecretWarning} />
                  <Input value={createdSecret} readOnly fontFamily="mono" />
                  <Button
                    colorPalette="green"
                    onClick={async () => {
                      await navigator.clipboard.writeText(createdSecret);
                      toaster.success({ title: copy.copied });
                    }}
                  >
                    <LuClipboard />
                    {copy.copy}
                  </Button>
                </Stack>
              </Dialog.Body>
              <Dialog.Footer>
                <Button variant="surface" onClick={() => setCreatedSecret('')}>
                  {copy.close}
                </Button>
              </Dialog.Footer>
              <Dialog.CloseTrigger asChild>
                <CloseButton size="sm" />
              </Dialog.CloseTrigger>
            </Dialog.Content>
          </Dialog.Positioner>
        </Dialog.Root>
      </Portal>
    </Box>
  );
}
