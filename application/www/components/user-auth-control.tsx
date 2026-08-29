'use client';

import {
  Box,
  Dialog,
  HStack,
  IconButton,
  Image,
  Input,
  Portal,
  SimpleGrid,
  Stack,
  Text,
} from '@chakra-ui/react';
import { useCallback, useEffect, useState } from 'react';
import { LuCheck, LuLogIn, LuLogOut, LuRefreshCw, LuSettings } from 'react-icons/lu';
import { RiImageEditLine } from 'react-icons/ri';
import { Alert } from '@/components/ui/alert';
import { Avatar } from '@/components/ui/avatar';
import { Button } from '@/components/ui/button';
import { CloseButton } from '@/components/ui/close-button';
import { Field } from '@/components/ui/field';
import {
  MenuContent,
  MenuItem,
  MenuRoot,
  MenuTrigger,
} from '@/components/ui/menu';
import { toaster } from '@/components/ui/toaster';
import { useLanguage } from '@/contexts/language-context';
import { useUserAuth } from '@/contexts/user-auth-context';
import { AuthApiError, CaptchaAction, CaptchaChallenge } from '@/lib/user-auth/types';
import { ACCOUNT_AVATARS } from '@/lib/user-auth/avatar-catalog';

type AuthView = 'login' | 'register' | 'sent';

function localizedError(error: unknown, t: ReturnType<typeof useLanguage>['t']): string {
  if (!(error instanceof AuthApiError)) return t.AUTH_REQUEST_FAILED;
  switch (error.code) {
    case 'CAPTCHA_INVALID':
      return t.AUTH_CAPTCHA_INVALID;
    case 'INVALID_CREDENTIALS':
      return t.AUTH_CREDENTIALS_INVALID;
    case 'AUTH_RATE_LIMITED':
      return t.AUTH_RATE_LIMITED;
    case 'EMAIL_AUTH_NOT_CONFIGURED':
      return t.AUTH_NOT_CONFIGURED;
    case 'VERIFICATION_LINK_INVALID':
      return t.AUTH_VERIFICATION_INVALID;
    default:
      return error.status >= 500 ? t.AUTH_REQUEST_FAILED : error.message;
  }
}

function CaptchaField({
  action,
  challenge,
  answer,
  loading,
  onAnswerChange,
  onRefresh,
}: {
  action: CaptchaAction;
  challenge: CaptchaChallenge | null;
  answer: string;
  loading: boolean;
  onAnswerChange(value: string): void;
  onRefresh(): void;
}) {
  const { t } = useLanguage();
  return (
    <Field label={t.AUTH_CAPTCHA_LABEL} required>
      <Stack gap={2} width="full">
        <HStack alignItems="stretch">
          <Box
            borderWidth="1px"
            borderColor="border"
            borderRadius="md"
            overflow="hidden"
            flex="1"
            minHeight="72px"
            bg="gray.100"
          >
            {challenge && (
              <Image
                src={challenge.image}
                alt={t.AUTH_CAPTCHA_ALT}
                width="full"
                height="72px"
                objectFit="cover"
              />
            )}
          </Box>
          <IconButton
            aria-label={t.AUTH_CAPTCHA_REFRESH}
            title={t.AUTH_CAPTCHA_REFRESH}
            variant="surface"
            onClick={onRefresh}
            loading={loading}
            alignSelf="stretch"
            height="auto"
          >
            <LuRefreshCw />
          </IconButton>
        </HStack>
        <Input
          name={`${action}-captcha`}
          value={answer}
          onChange={event => onAnswerChange(
            event.target.value.replace(/[^2-9]/g, '').slice(0, 6),
          )}
          inputMode="numeric"
          autoComplete="off"
          maxLength={6}
          required
        />
      </Stack>
    </Field>
  );
}

export function UserAuthControl() {
  const { t, currentLanguage } = useLanguage();
  const {
    session,
    loading: sessionLoading,
    createCaptcha,
    requestRegistration,
    login,
    logout,
    updateAvatar,
  } = useUserAuth();
  const [open, setOpen] = useState(false);
  const [view, setView] = useState<AuthView>('login');
  const [email, setEmail] = useState('');
  const [password, setPassword] = useState('');
  const [captcha, setCaptcha] = useState<CaptchaChallenge | null>(null);
  const [captchaAnswer, setCaptchaAnswer] = useState('');
  const [submitting, setSubmitting] = useState(false);
  const [captchaLoading, setCaptchaLoading] = useState(false);
  const [previewVerificationToken, setPreviewVerificationToken] = useState<string | null>(null);
  const [menuOpen, setMenuOpen] = useState(false);
  const [avatarOpen, setAvatarOpen] = useState(false);
  const [selectedAvatarId, setSelectedAvatarId] = useState<string | null>(null);
  const [avatarSaving, setAvatarSaving] = useState(false);

  const captchaAction: CaptchaAction = view === 'register' ? 'register' : 'login';
  const refreshCaptcha = useCallback(async () => {
    if (!session.registrationEnabled || view === 'sent') return;
    setCaptchaLoading(true);
    setCaptchaAnswer('');
    try {
      setCaptcha(await createCaptcha(captchaAction));
    } catch (error) {
      setCaptcha(null);
      toaster.error({
        title: t.AUTH_REQUEST_FAILED,
        description: localizedError(error, t),
      });
    } finally {
      setCaptchaLoading(false);
    }
  }, [captchaAction, createCaptcha, session.registrationEnabled, t, view]);

  useEffect(() => {
    if (open && session.registrationEnabled && view !== 'sent') {
      void refreshCaptcha();
    }
  }, [open, session.registrationEnabled, view, refreshCaptcha]);

  const switchView = (next: AuthView) => {
    setView(next);
    setPassword('');
    setCaptcha(null);
    setCaptchaAnswer('');
    setPreviewVerificationToken(null);
  };

  const submitLogin = async (event: React.FormEvent) => {
    event.preventDefault();
    if (!captcha) return;
    setSubmitting(true);
    try {
      await login({
        email,
        password,
        captchaChallengeId: captcha.challengeId,
        captchaAnswer,
      });
      setOpen(false);
      toaster.success({ title: t.AUTH_SIGN_IN_SUCCESS });
    } catch (error) {
      toaster.error({
        title: t.AUTH_SIGN_IN,
        description: localizedError(error, t),
      });
      await refreshCaptcha();
    } finally {
      setSubmitting(false);
    }
  };

  const submitRegistration = async (event: React.FormEvent) => {
    event.preventDefault();
    if (!captcha) return;
    setSubmitting(true);
    try {
      const result = await requestRegistration({
        email,
        locale: currentLanguage,
        captchaChallengeId: captcha.challengeId,
        captchaAnswer,
      });
      setPreviewVerificationToken(result.verificationToken || null);
      setView('sent');
    } catch (error) {
      toaster.error({
        title: t.AUTH_REGISTER_TITLE,
        description: localizedError(error, t),
      });
      await refreshCaptcha();
    } finally {
      setSubmitting(false);
    }
  };

  const handleLogout = async () => {
    try {
      await logout();
    } catch (error) {
      toaster.error({
        title: t.AUTH_SIGN_OUT,
        description: localizedError(error, t),
      });
    }
  };

  if (session.authenticated && session.user) {
    return (
      <>
      <MenuRoot
        positioning={{ placement: 'bottom-end' }}
        open={menuOpen}
        onOpenChange={details => setMenuOpen(details.open)}
      >
        <MenuTrigger asChild>
          <IconButton
            aria-label={session.user.email}
            title={session.user.email}
            variant="ghost"
            size="md"
            borderRadius="full"
          >
            <Avatar
              name={session.user.displayName || session.user.email}
              src={session.user.avatarUrl || undefined}
              size="md"
              colorPalette="green"
            />
          </IconButton>
        </MenuTrigger>
        <MenuContent minWidth="220px" zIndex={11001}>
          <Box px={3} py={2}>
            <Text fontSize="sm" fontWeight="medium" truncate>
              {session.user.displayName}
            </Text>
            <HStack gap={1} justifyContent="space-between">
              <Text fontSize="xs" color="fg.muted" truncate flex="1">
                {session.user.email}
              </Text>
              <IconButton
                aria-label={t.AUTH_EDIT_AVATAR}
                title={t.AUTH_EDIT_AVATAR}
                size="2xs"
                variant="plain"
                borderWidth="0"
                focusVisibleRing="none"
                onClick={() => {
                  setSelectedAvatarId(session.user?.avatarId || null);
                  setMenuOpen(false);
                  setAvatarOpen(true);
                }}
              >
                <RiImageEditLine />
              </IconButton>
            </HStack>
          </Box>
          {session.user.role === 'admin' && (
            <MenuItem
              value="administration"
              onClick={() => {
                window.location.href = '/admin/users/';
              }}
            >
              <LuSettings />
              {t.AUTH_ADMINISTRATION}
            </MenuItem>
          )}
          <MenuItem value="sign-out" onClick={handleLogout}>
            <LuLogOut />
            {t.AUTH_SIGN_OUT}
          </MenuItem>
        </MenuContent>
      </MenuRoot>
      <Portal>
        <Dialog.Root
          open={avatarOpen}
          onOpenChange={details => {
            if (!avatarSaving) setAvatarOpen(details.open);
          }}
        >
          <Dialog.Backdrop backdropFilter="blur(4px)" zIndex={11000} />
          <Dialog.Positioner alignItems="flex-start" pt={8} zIndex={11001}>
            <Dialog.Content width="min(94vw, 760px)" maxHeight="calc(100vh - 96px)">
              <Dialog.Header>
                <Dialog.Title>{t.AUTH_AVATAR_TITLE}</Dialog.Title>
              </Dialog.Header>
              <Dialog.Body overflowY="auto">
                <Stack gap={4}>
                  <Text color="fg.muted">{t.AUTH_AVATAR_DESCRIPTION}</Text>
                  <SimpleGrid columns={{ base: 3, sm: 4, md: 5 }} gap={3}>
                    {ACCOUNT_AVATARS.map(option => {
                      const selected = selectedAvatarId === option.id;
                      return (
                        <Button
                          key={option.id}
                          height="auto"
                          px={2}
                          py={2}
                          variant={selected ? 'solid' : 'surface'}
                          colorPalette={selected ? 'green' : 'gray'}
                          flexDirection="column"
                          gap={2}
                          aria-pressed={selected}
                          onClick={() => setSelectedAvatarId(option.id)}
                        >
                          <Box position="relative">
                            <Avatar src={option.src} name={option.name} size="xl" />
                            {selected && (
                              <Box position="absolute" right="-1" bottom="-1">
                                <LuCheck />
                              </Box>
                            )}
                          </Box>
                          <Text fontSize="xs" lineClamp={1}>{option.name}</Text>
                        </Button>
                      );
                    })}
                  </SimpleGrid>
                </Stack>
              </Dialog.Body>
              <Dialog.Footer>
                <Button
                  variant="surface"
                  onClick={() => setAvatarOpen(false)}
                  disabled={avatarSaving}
                >
                  {t.BUTTON_CANCEL}
                </Button>
                <Button
                  colorPalette="green"
                  loading={avatarSaving}
                  disabled={!selectedAvatarId || selectedAvatarId === session.user.avatarId}
                  onClick={async () => {
                    if (!selectedAvatarId) return;
                    setAvatarSaving(true);
                    try {
                      await updateAvatar(selectedAvatarId);
                      setAvatarOpen(false);
                      toaster.success({ title: t.AUTH_AVATAR_SAVED });
                    } catch (error) {
                      toaster.error({
                        title: t.AUTH_AVATAR_TITLE,
                        description: localizedError(error, t),
                      });
                    } finally {
                      setAvatarSaving(false);
                    }
                  }}
                >
                  {t.AUTH_AVATAR_SAVE}
                </Button>
              </Dialog.Footer>
              <Dialog.CloseTrigger asChild>
                <CloseButton size="sm" disabled={avatarSaving} />
              </Dialog.CloseTrigger>
            </Dialog.Content>
          </Dialog.Positioner>
        </Dialog.Root>
      </Portal>
      </>
    );
  }

  return (
    <>
      <Button
        size="xs"
        variant="surface"
        colorPalette="green"
        onClick={() => setOpen(true)}
        loading={sessionLoading}
      >
        <LuLogIn />
        {t.AUTH_SIGN_IN}
      </Button>
      <Portal>
        <Dialog.Root
          open={open}
          onOpenChange={details => {
            setOpen(details.open);
            if (!details.open) switchView('login');
          }}
        >
          <Dialog.Backdrop backdropFilter="blur(4px)" zIndex={11000} />
          <Dialog.Positioner alignItems="flex-start" pt={16} zIndex={11001}>
            <Dialog.Content width="min(92vw, 520px)">
              <Dialog.Header>
                <Dialog.Title>
                  {view === 'register'
                    ? t.AUTH_REGISTER_TITLE
                    : view === 'sent'
                      ? t.AUTH_EMAIL_SENT_TITLE
                      : t.AUTH_SIGN_IN}
                </Dialog.Title>
              </Dialog.Header>
              <Dialog.Body>
                {!session.registrationEnabled ? (
                  <Alert colorPalette="orange" title={t.AUTH_NOT_CONFIGURED} />
                ) : view === 'sent' ? (
                  <Stack gap={4}>
                    <Alert
                      colorPalette="green"
                      title={t.AUTH_EMAIL_SENT_TITLE}
                    >
                      {t.AUTH_EMAIL_SENT_DESCRIPTION}
                    </Alert>
                    {previewVerificationToken && (
                      <Button
                        colorPalette="purple"
                        onClick={() => {
                          window.location.href = `/auth/verify/?token=${encodeURIComponent(previewVerificationToken)}&lang=${currentLanguage}`;
                        }}
                      >
                        {t.AUTH_PREVIEW_CONTINUE}
                      </Button>
                    )}
                    <Button variant="ghost" onClick={() => switchView('login')}>
                      {t.AUTH_BACK_TO_SIGN_IN}
                    </Button>
                  </Stack>
                ) : view === 'login' ? (
                  <Box as="form" onSubmit={submitLogin}>
                    <Stack gap={4}>
                      <Text color="fg.muted">{t.AUTH_SIGN_IN_DESCRIPTION}</Text>
                      <Field label={t.AUTH_EMAIL_LABEL} required>
                        <Input
                          type="email"
                          value={email}
                          onChange={event => setEmail(event.target.value)}
                          autoComplete="email"
                          required
                        />
                      </Field>
                      <Field label={t.AUTH_PASSWORD_LABEL} required>
                        <Input
                          type="password"
                          value={password}
                          onChange={event => setPassword(event.target.value)}
                          autoComplete="current-password"
                          minLength={10}
                          maxLength={128}
                          required
                        />
                      </Field>
                      <CaptchaField
                        action="login"
                        challenge={captcha}
                        answer={captchaAnswer}
                        loading={captchaLoading}
                        onAnswerChange={setCaptchaAnswer}
                        onRefresh={() => void refreshCaptcha()}
                      />
                      <Button
                        type="submit"
                        colorPalette="green"
                        loading={submitting}
                        disabled={!captcha || captchaAnswer.length !== 6}
                      >
                        {t.AUTH_SIGN_IN_ACTION}
                      </Button>
                      <Button variant="ghost" onClick={() => switchView('register')}>
                        {t.AUTH_CREATE_ACCOUNT}
                      </Button>
                    </Stack>
                  </Box>
                ) : (
                  <Box as="form" onSubmit={submitRegistration}>
                    <Stack gap={4}>
                      <Text color="fg.muted">{t.AUTH_REGISTER_DESCRIPTION}</Text>
                      <Field label={t.AUTH_EMAIL_LABEL} required>
                        <Input
                          type="email"
                          value={email}
                          onChange={event => setEmail(event.target.value)}
                          autoComplete="email"
                          required
                        />
                      </Field>
                      <CaptchaField
                        action="register"
                        challenge={captcha}
                        answer={captchaAnswer}
                        loading={captchaLoading}
                        onAnswerChange={setCaptchaAnswer}
                        onRefresh={() => void refreshCaptcha()}
                      />
                      <Button
                        type="submit"
                        colorPalette="green"
                        loading={submitting}
                        disabled={!captcha || captchaAnswer.length !== 6}
                      >
                        {t.AUTH_SEND_VERIFICATION}
                      </Button>
                      <Button variant="ghost" onClick={() => switchView('login')}>
                        {t.AUTH_BACK_TO_SIGN_IN}
                      </Button>
                    </Stack>
                  </Box>
                )}
              </Dialog.Body>
              <Dialog.CloseTrigger asChild>
                <CloseButton size="sm" />
              </Dialog.CloseTrigger>
            </Dialog.Content>
          </Dialog.Positioner>
        </Dialog.Root>
      </Portal>
    </>
  );
}
