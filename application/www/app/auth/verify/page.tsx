'use client';

import { Box, Center, Heading, Input, Stack, Text } from '@chakra-ui/react';
import { useEffect, useState } from 'react';
import { Alert } from '@/components/ui/alert';
import { Button } from '@/components/ui/button';
import { Field } from '@/components/ui/field';
import { useLanguage } from '@/contexts/language-context';
import { useUserAuth } from '@/contexts/user-auth-context';
import { AuthApiError } from '@/lib/user-auth/types';

export default function VerifyEmailPage() {
  const { t, setLanguage } = useLanguage();
  const { completeRegistration } = useUserAuth();
  const [token, setToken] = useState('');
  const [password, setPassword] = useState('');
  const [confirmation, setConfirmation] = useState('');
  const [submitting, setSubmitting] = useState(false);
  const [completed, setCompleted] = useState(false);
  const [error, setError] = useState('');

  useEffect(() => {
    const search = new URLSearchParams(window.location.search);
    const value = search.get('token') || '';
    setLanguage(search.get('lang') === 'zh' ? 'zh' : 'en');
    setToken(value);
    if (!value) setError(t.AUTH_VERIFICATION_INVALID);
  }, [setLanguage, t.AUTH_VERIFICATION_INVALID]);

  const submit = async (event: React.FormEvent) => {
    event.preventDefault();
    if (password !== confirmation) {
      setError(t.AUTH_PASSWORD_MISMATCH);
      return;
    }
    setSubmitting(true);
    setError('');
    try {
      await completeRegistration({ token, password });
      setCompleted(true);
    } catch (caught) {
      if (caught instanceof AuthApiError &&
          caught.code === 'VERIFICATION_LINK_INVALID') {
        setError(t.AUTH_VERIFICATION_INVALID);
      } else {
        setError(t.AUTH_REQUEST_FAILED);
      }
    } finally {
      setSubmitting(false);
    }
  };

  return (
    <Center minHeight="100vh" bg="app.canvas" px={4}>
      <Box
        width="full"
        maxWidth="520px"
        bg="app.panel"
        borderWidth="1px"
        borderColor="app.border"
        borderRadius="xl"
        p={{ base: 6, md: 8 }}
        boxShadow="lg"
      >
        {completed ? (
          <Stack gap={5}>
            <Alert colorPalette="green" title={t.AUTH_ACCOUNT_CREATED_TITLE}>
              {t.AUTH_ACCOUNT_CREATED_DESCRIPTION}
            </Alert>
            <Button
              colorPalette="green"
              onClick={() => {
                window.location.href = '/';
              }}
            >
              {t.AUTH_RETURN_HOME}
            </Button>
          </Stack>
        ) : (
          <Box as="form" onSubmit={submit}>
            <Stack gap={5}>
              <Stack gap={2}>
                <Heading size="xl">{t.AUTH_VERIFY_TITLE}</Heading>
                <Text color="fg.muted">{t.AUTH_VERIFY_DESCRIPTION}</Text>
              </Stack>
              {error && <Alert colorPalette="red" title={error} />}
              <Field
                label={t.AUTH_PASSWORD_LABEL}
                helperText={t.AUTH_PASSWORD_RULE}
                required
              >
                <Input
                  type="password"
                  value={password}
                  onChange={event => setPassword(event.target.value)}
                  autoComplete="new-password"
                  minLength={10}
                  maxLength={128}
                  required
                />
              </Field>
              <Field label={t.AUTH_PASSWORD_CONFIRM_LABEL} required>
                <Input
                  type="password"
                  value={confirmation}
                  onChange={event => setConfirmation(event.target.value)}
                  autoComplete="new-password"
                  minLength={10}
                  maxLength={128}
                  required
                />
              </Field>
              <Button
                type="submit"
                colorPalette="green"
                loading={submitting}
                disabled={!token}
              >
                {t.AUTH_COMPLETE_REGISTRATION}
              </Button>
            </Stack>
          </Box>
        )}
      </Box>
    </Center>
  );
}
