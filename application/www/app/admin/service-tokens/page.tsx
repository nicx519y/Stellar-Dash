'use client';

import {
  Badge, Box, Center, Dialog, Flex, Heading, HStack, Input,
  Portal, Spinner, Stack, Table, Text,
} from '@chakra-ui/react';
import { useCallback, useEffect, useRef, useState } from 'react';
import { LuClipboard, LuKeyRound, LuTerminal, LuTrash2 } from 'react-icons/lu';
import { adminRuntime } from '@hbox/admin-runtime';
import { Alert } from '@/components/ui/alert';
import { Button } from '@/components/ui/button';
import { CloseButton } from '@/components/ui/close-button';
import { Checkbox } from '@/components/ui/checkbox';
import { Field } from '@/components/ui/field';
import { toaster } from '@/components/ui/toaster';
import { AdminCard, AdminPageHeader } from '@/components/admin/admin-surface';
import { useLanguage } from '@/contexts/language-context';
import { useUserAuth } from '@/contexts/user-auth-context';
import type { ServiceTokenMetadata, ServiceTokenScope } from '@/lib/admin/types';
import { localFirmwareTokenScript, type LocalTokenScriptPlatform } from '@/lib/admin/local-token-script';

const COPY = {
  en: {
    title: 'Service tokens',
    management: 'Token management',
    createDescription: 'Give your automation a name, set its lifetime and choose the permissions it needs.',
    nameHint: 'Use a name that identifies the script or integration.',
    expiryHint: '1–365 days',
    scopesHint: 'Select one or more permissions.',
    deviceScope: 'Register devices, update server policies and revoke devices.',
    firmwareScope: 'Read releases, upload packages, edit notes and delete drafts.',
    listDescription: 'Review existing tokens, their permissions and expiry dates.',
    description: 'Use scoped tokens for release and device automation. Secrets are shown only once.',
    signInRequired: 'Sign in with an administrator account to continue.',
    permissionRequired: 'This account does not have administrator permission.',
    tokenName: 'Token name',
    tokenNamePlaceholder: 'Release automation',
    expires: 'Expires in days',
    scopes: 'Scopes',
    createToken: 'Create token',
    tokenCreated: 'Service token created',
    tokenSecretWarning: 'Copy this secret now. It cannot be viewed again after this dialog closes.',
    copy: 'Copy token',
    copyWindowsScript: 'Copy Windows script',
    copyMacScript: 'Copy macOS script',
    localSaveTitle: 'Use for local firmware packaging',
    localSaveHint: 'Choose Windows (PowerShell) or macOS (Terminal), then paste the script in the XORA repository root. It saves this token and replaces the previous local token. Future packages can upload drafts without specifying a file.',
    localSaveScope: 'Select firmware.manage when creating a token to use local firmware packaging.',
    copyFailed: 'Could not copy to clipboard.',
    copied: 'Copied',
    close: 'Close',
    cancel: 'Cancel',
    expiry: 'Expiry',
    status: 'Status',
    actions: 'Actions',
    active: 'Active',
    expired: 'Expired',
    revoked: 'Revoked',
    revoke: 'Revoke',
    noTokens: 'No service tokens have been created.',
    requestFailed: 'The administrator request failed.',
    tokenRevoked: 'Service token revoked.',
    selectScope: 'Select at least one scope.',
  },
  zh: {
    title: '服务令牌',
    management: '令牌管理',
    createDescription: '为自动化任务命名，设置有效期，并选择需要的权限。',
    nameHint: '用名称标识使用此令牌的脚本或服务。',
    expiryHint: '1–365 天',
    scopesHint: '可选择一个或多个权限。',
    deviceScope: '注册设备、更新服务端设备策略和撤销设备。',
    firmwareScope: '查询固件、上传发布包、编辑说明和删除草稿。',
    listDescription: '查看已有令牌的权限、到期时间和状态。',
    description: '为发版和设备自动化创建限定范围的令牌。令牌密钥只显示一次。',
    signInRequired: '请先使用管理员账号登录。',
    permissionRequired: '当前账号没有管理员权限。',
    tokenName: '令牌名称',
    tokenNamePlaceholder: '发版自动化',
    expires: '有效天数',
    scopes: '权限范围',
    createToken: '创建令牌',
    tokenCreated: '服务令牌已创建',
    tokenSecretWarning: '请立即复制密钥。关闭此对话框后无法再次查看。',
    copy: '复制令牌',
    copyWindowsScript: '复制 Windows 脚本',
    copyMacScript: '复制 macOS 脚本',
    localSaveTitle: '用于本地固件打包',
    localSaveHint: '选择 Windows（PowerShell）或 macOS（终端），在 XORA 仓库根目录粘贴执行。脚本会自动保存此令牌，并替换本地旧令牌；以后打包即可自动上传草稿，无需指定文件。',
    localSaveScope: '创建时选择 firmware.manage 权限，即可用于本地固件打包。',
    copyFailed: '无法复制到剪贴板。',
    copied: '已复制',
    close: '关闭',
    cancel: '取消',
    expiry: '到期时间',
    status: '状态',
    actions: '操作',
    active: '有效',
    expired: '已过期',
    revoked: '已撤销',
    revoke: '撤销',
    noTokens: '尚未创建服务令牌。',
    requestFailed: '管理员请求失败。',
    tokenRevoked: '服务令牌已撤销。',
    selectScope: '请至少选择一个权限范围。',
  },
};

function serviceTokenStatus(token: ServiceTokenMetadata) {
  if (token.revokedAt !== null) return 'revoked';
  if (token.expiresAt <= Date.now()) return 'expired';
  return 'active';
}

export default function AdminServiceTokensPage() {
  const { currentLanguage } = useLanguage();
  const { session, loading: sessionLoading } = useUserAuth();
  const copy = COPY[currentLanguage];
  const [tokens, setTokens] = useState<ServiceTokenMetadata[]>([]);
  const [loading, setLoading] = useState(false);
  const [revokingTokenId, setRevokingTokenId] = useState<string | null>(null);
  const [tokenName, setTokenName] = useState('');
  const [expiresInDays, setExpiresInDays] = useState('90');
  const [scopes, setScopes] = useState<ServiceTokenScope[]>(['device.manage']);
  const [creatingToken, setCreatingToken] = useState(false);
  const [createOpen, setCreateOpen] = useState(false);
  const creationInFlight = useRef(false);
  const createButton = useRef<HTMLButtonElement>(null);
  const nameInput = useRef<HTMLInputElement>(null);
  const secretInput = useRef<HTMLInputElement>(null);
  const [createdSecret, setCreatedSecret] = useState('');
  const [createdScopes, setCreatedScopes] = useState<ServiceTokenScope[]>([]);
  const isAdmin = session.authenticated && session.user?.role === 'admin';

  const showError = useCallback((error: unknown) => {
    const description = error instanceof Error && error.message
      ? error.message : copy.requestFailed;
    toaster.error({ title: copy.requestFailed, description });
  }, [copy.requestFailed]);

  const loadTokens = useCallback(async () => {
    if (!isAdmin) return;
    setLoading(true);
    try {
      setTokens(await adminRuntime.listServiceTokens());
    } catch (error) {
      showError(error);
    } finally {
      setLoading(false);
    }
  }, [isAdmin, showError]);

  useEffect(() => {
    void loadTokens();
  }, [loadTokens]);

  const toggleScope = (scope: ServiceTokenScope) => {
    setScopes(current => current.includes(scope)
      ? current.filter(item => item !== scope)
      : [...current, scope]);
  };

  const createToken = async (event: React.FormEvent) => {
    event.preventDefault();
    if (!isAdmin || creationInFlight.current) return;
    if (scopes.length === 0) {
      toaster.error({ title: copy.selectScope });
      return;
    }
    creationInFlight.current = true;
    setCreatingToken(true);
    try {
      const created = await adminRuntime.createServiceToken({
        name: tokenName,
        scopes,
        expiresInDays: Number(expiresInDays),
      });
      setCreatedSecret(created.secret);
      setCreatedScopes(created.token.scopes);
      setCreateOpen(false);
      setTokenName('');
      setExpiresInDays('90');
      setTokens(await adminRuntime.listServiceTokens());
    } catch (error) {
      showError(error);
    } finally {
      creationInFlight.current = false;
      setCreatingToken(false);
    }
  };

  const closeCreateDialog = () => {
    if (creationInFlight.current) return;
    setCreateOpen(false); setCreatedSecret(''); setCreatedScopes([]);
    setTokenName(''); setExpiresInDays('90'); setScopes(['device.manage']);
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

  const locale = currentLanguage === 'zh' ? 'zh-CN' : 'en-US';
  const copyCreatedToken = async (platform?: LocalTokenScriptPlatform) => {
    try {
      await navigator.clipboard.writeText(platform ? localFirmwareTokenScript(createdSecret, platform) : createdSecret);
      toaster.success({ title: copy.copied });
    } catch { toaster.error({ title: copy.copyFailed }); }
  };
  const formatDate = (value: number) => new Intl.DateTimeFormat(locale, {
    dateStyle: 'medium', timeStyle: 'short',
  }).format(new Date(value));

  return <>
    <AdminPageHeader title={copy.title} description={copy.description}
      actions={isAdmin && !sessionLoading ? <Button ref={createButton} colorPalette="green" onClick={() => setCreateOpen(true)}><LuKeyRound />{copy.createToken}</Button> : undefined} />
    {sessionLoading ? (
      <Center minHeight="320px"><Spinner size="lg" colorPalette="green" /></Center>
    ) : !session.authenticated ? (
      <Alert colorPalette="orange" title={copy.signInRequired} />
    ) : !isAdmin ? (
      <Alert colorPalette="red" title={copy.permissionRequired} />
    ) : (
      <AdminCard>
        <Stack gap={5}>
          <Flex justify="space-between" align="center" gap={3}>
            <Stack gap={1}><Heading as="h2" size="lg">{copy.management}</Heading>
              <Text fontSize="sm" color="fg.muted">{copy.listDescription}</Text></Stack>
            <Badge variant="subtle">{tokens.length}</Badge>
          </Flex>
          <Box overflowX="auto">
            <Table.Root size="sm" minWidth="720px" interactive>
              <Table.Header><Table.Row>
                <Table.ColumnHeader>{copy.tokenName}</Table.ColumnHeader>
                <Table.ColumnHeader>{copy.scopes}</Table.ColumnHeader>
                <Table.ColumnHeader>{copy.expiry}</Table.ColumnHeader>
                <Table.ColumnHeader>{copy.status}</Table.ColumnHeader>
                <Table.ColumnHeader>{copy.actions}</Table.ColumnHeader>
              </Table.Row></Table.Header>
              <Table.Body>
                {tokens.map(token => {
                  const status = serviceTokenStatus(token);
                  return <Table.Row key={token.id}>
                    <Table.Cell>{token.name}</Table.Cell>
                    <Table.Cell><HStack flexWrap="wrap">
                      {token.scopes.map(scope => <Badge key={scope} variant="surface"
                        colorPalette="purple">{scope}</Badge>)}
                    </HStack></Table.Cell>
                    <Table.Cell>{formatDate(token.expiresAt)}</Table.Cell>
                    <Table.Cell><Badge colorPalette={status === 'active' ? 'green'
                      : status === 'expired' ? 'orange' : 'red'}>{copy[status]}</Badge></Table.Cell>
                    <Table.Cell><Button size="sm" variant="surface" colorPalette="red"
                      disabled={status !== 'active'} loading={revokingTokenId === token.id}
                      onClick={() => void revokeToken(token.id)}><LuTrash2 />{copy.revoke}</Button></Table.Cell>
                  </Table.Row>;
                })}
              </Table.Body>
            </Table.Root>
          </Box>
          {!loading && tokens.length === 0 && (
            <Text color="fg.muted" textAlign="center">{copy.noTokens}</Text>
          )}
        </Stack>
      </AdminCard>
    )}
    <Portal>
      <Dialog.Root open={isAdmin && (createOpen || createdSecret.length > 0)}
        initialFocusEl={() => nameInput.current ?? secretInput.current} finalFocusEl={() => createButton.current}
        closeOnEscape={!creatingToken} closeOnInteractOutside={!creatingToken}
        onOpenChange={details => { if (!details.open) closeCreateDialog(); }}>
        <Dialog.Backdrop backdropFilter="blur(4px)" />
        <Dialog.Positioner alignItems="flex-start" pt={{ base: 6, md: 12 }} pb={6} px={4}>
          <Dialog.Content my={0} maxW="none" width="min(92vw, 620px)" maxH={{ base: 'calc(100dvh - 48px)', md: 'calc(100dvh - 96px)' }}>
            <Dialog.Header px={{ base: 6, md: 8 }}><Stack gap={2}>
              <Dialog.Title>{createdSecret ? copy.tokenCreated : copy.createToken}</Dialog.Title>
              {!createdSecret && <Dialog.Description>{copy.createDescription}</Dialog.Description>}
            </Stack></Dialog.Header>
            <Dialog.Body overflowY="auto" minH="0" px={{ base: 6, md: 8 }} pb={8}>
              {!createdSecret ? (
                <Box as="form" id="create-service-token-form" onSubmit={createToken}>
                  <Stack gap={8}>
                    <Stack gap={5}>
                      <Field label={copy.tokenName} helperText={copy.nameHint} required>
                        <Input ref={nameInput} value={tokenName} disabled={creatingToken} onChange={event => setTokenName(event.target.value)}
                          placeholder={copy.tokenNamePlaceholder} minLength={1} maxLength={80} required />
                      </Field>
                      <Field label={copy.expires} helperText={copy.expiryHint} required>
                        <Input type="number" value={expiresInDays} disabled={creatingToken}
                          onChange={event => setExpiresInDays(event.target.value)}
                          min={1} max={365} required />
                      </Field>
                    </Stack>
                    <Box as="fieldset" minW="0" border="0" p="0" m="0">
                      <Text as="legend" fontWeight="semibold" mb={1}>{copy.scopes}</Text>
                      <Text id="token-scopes-hint" color="fg.muted" fontSize="sm" mb={5}>{copy.scopesHint}</Text>
                      <Stack gap={5}>
                        {(['device.manage', 'firmware.manage'] as ServiceTokenScope[]).map(scope => (
                          <Checkbox key={scope} name="scopes" value={scope} checked={scopes.includes(scope)}
                            disabled={creatingToken} colorPalette="green" alignItems="flex-start" gap={3}
                            inputProps={{ 'aria-describedby': 'token-scopes-hint' }}
                            onCheckedChange={() => toggleScope(scope)}>
                            <Box as="span" display="block" fontWeight="medium">{scope}</Box>
                            <Box as="span" display="block" fontSize="sm" color="fg.muted" mt={1}>
                              {scope === 'device.manage' ? copy.deviceScope : copy.firmwareScope}
                            </Box>
                          </Checkbox>
                        ))}
                      </Stack>
                    </Box>
                  </Stack>
                </Box>
              ) : <Stack gap={4}>
              <Alert colorPalette="orange" title={copy.tokenSecretWarning} />
              <Input ref={secretInput} value={createdSecret} readOnly fontFamily="mono" />
              <Button variant="surface" alignSelf="flex-start" onClick={() => void copyCreatedToken()}><LuClipboard />{copy.copy}</Button>
              <Stack gap={3} borderTopWidth="1px" borderColor="app.border" pt={4}>
                <Text fontWeight="semibold">{copy.localSaveTitle}</Text>
                <Text fontSize="sm" color="fg.muted">{createdScopes.includes('firmware.manage') ? copy.localSaveHint : copy.localSaveScope}</Text>
                {createdScopes.includes('firmware.manage') && <Flex direction={{ base: 'column', md: 'row' }} gap={3}>
                  <Button flex={{ base: 'none', md: '1' }} colorPalette="green" onClick={() => void copyCreatedToken('windows')}><LuTerminal />{copy.copyWindowsScript}</Button>
                  <Button flex={{ base: 'none', md: '1' }} colorPalette="green" variant="outline" onClick={() => void copyCreatedToken('macos')}><LuTerminal />{copy.copyMacScript}</Button>
                </Flex>}
              </Stack>
            </Stack>}
            </Dialog.Body>
            <Dialog.Footer px={{ base: 6, md: 8 }} borderTopWidth="1px" borderColor="app.border">
              <Button variant="surface" disabled={creatingToken} onClick={closeCreateDialog}>{createdSecret ? copy.close : copy.cancel}</Button>
              {!createdSecret && <Button type="submit" form="create-service-token-form" colorPalette="green" loading={creatingToken}><LuKeyRound />{copy.createToken}</Button>}
            </Dialog.Footer>
            <Dialog.CloseTrigger asChild><CloseButton size="sm" disabled={creatingToken} /></Dialog.CloseTrigger>
          </Dialog.Content>
        </Dialog.Positioner>
      </Dialog.Root>
    </Portal>
  </>;
}
