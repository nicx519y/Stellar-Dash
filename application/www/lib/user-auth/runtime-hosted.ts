import {
  AuthApiError,
  AuthSession,
  CaptchaAction,
  CaptchaChallenge,
  RegistrationRequestResult,
  UserAuthRuntime,
} from './types';

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
      // A proxy error page is intentionally reduced to a generic message.
    }
    throw new AuthApiError(
      payload.error || 'AUTH_REQUEST_FAILED',
      payload.message || 'Authentication request failed.',
      response.status,
    );
  }
  if (response.status === 204) {
    return undefined as T;
  }
  return response.json() as Promise<T>;
}

export const userAuthRuntime: UserAuthRuntime = {
  getSession: () => apiRequest<AuthSession>('/api/auth/session'),
  createCaptcha: (action: CaptchaAction) => apiRequest<CaptchaChallenge>(
    '/api/auth/captcha',
    { method: 'POST', body: JSON.stringify({ action }) },
  ),
  requestRegistration: input => apiRequest<RegistrationRequestResult>(
    '/api/auth/register/email/request',
    { method: 'POST', body: JSON.stringify(input) },
  ),
  completeRegistration: input => apiRequest<AuthSession>(
    '/api/auth/register/email/complete',
    { method: 'POST', body: JSON.stringify(input) },
  ),
  login: input => apiRequest<AuthSession>(
    '/api/auth/login/email',
    { method: 'POST', body: JSON.stringify(input) },
  ),
  logout: () => apiRequest<void>(
    '/api/auth/logout',
    { method: 'POST' },
  ),
  updateAvatar: avatarId => apiRequest<{ user: import('./types').AuthUser }>(
    '/api/auth/profile/avatar',
    { method: 'PATCH', body: JSON.stringify({ avatarId }) },
  ).then(result => result.user),
};
