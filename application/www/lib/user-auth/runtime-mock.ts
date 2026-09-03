import {
  AuthApiError,
  AuthSession,
  CaptchaAction,
  CaptchaChallenge,
  RegistrationRequestResult,
  UserAuthRuntime,
} from './types';

const SESSION_KEY = 'st-dash-ui-preview-user-session';
const PENDING_KEY = 'st-dash-ui-preview-registration';
const CAPTCHA_PREFIX = 'preview-captcha-';

function unauthenticated(): AuthSession {
  return { authenticated: false, registrationEnabled: true };
}

function readSession(): AuthSession {
  const raw = sessionStorage.getItem(SESSION_KEY);
  if (!raw) return unauthenticated();
  try {
    return JSON.parse(raw) as AuthSession;
  } catch {
    sessionStorage.removeItem(SESSION_KEY);
    return unauthenticated();
  }
}

function captchaImage(action: CaptchaAction): string {
  const label = action === 'login' ? '876543' : '234567';
  const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="240" height="72"><rect width="240" height="72" rx="8" fill="#edf2f7"/><text x="52" y="48" font-size="32" fill="#276749" letter-spacing="8">${label}</text></svg>`;
  return `data:image/svg+xml;base64,${btoa(svg)}`;
}

function assertCaptcha(action: CaptchaAction, id: string, answer: string) {
  const expected = action === 'login' ? '876543' : '234567';
  if (!id.startsWith(`${CAPTCHA_PREFIX}${action}-`) || answer !== expected) {
    throw new AuthApiError('CAPTCHA_INVALID', 'Invalid preview captcha.', 400);
  }
}

function previewSession(email: string): AuthSession {
  const session: AuthSession = {
    authenticated: true,
    registrationEnabled: true,
    expiresAt: Date.now() + 7 * 24 * 60 * 60 * 1000,
    user: {
      uid: '00000000-0000-4000-8000-000000000001',
      email,
      displayName: email.split('@')[0] || 'User',
      role: email === '33618409@qq.com' ? 'admin' : 'user',
      avatarId: null,
      avatarUrl: null,
    },
  };
  sessionStorage.setItem(SESSION_KEY, JSON.stringify(session));
  return session;
}

export const userAuthRuntime: UserAuthRuntime = {
  async getSession() {
    return readSession();
  },
  async createCaptcha(action): Promise<CaptchaChallenge> {
    return {
      challengeId: `${CAPTCHA_PREFIX}${action}-${Date.now()}`,
      image: captchaImage(action),
      expiresAt: Date.now() + 300000,
    };
  },
  async requestRegistration(input): Promise<RegistrationRequestResult> {
    assertCaptcha(
      'register',
      input.captchaChallengeId,
      input.captchaAnswer,
    );
    sessionStorage.setItem(PENDING_KEY, input.email.trim().toLowerCase());
    return { accepted: true, verificationToken: 'preview-email-verification' };
  },
  async completeRegistration(input) {
    const email = sessionStorage.getItem(PENDING_KEY);
    if (input.token !== 'preview-email-verification' || !email) {
      throw new AuthApiError(
        'VERIFICATION_LINK_INVALID',
        'The preview verification link is invalid.',
        400,
      );
    }
    if (input.password.length < 10) {
      throw new AuthApiError('INVALID_PASSWORD', 'Password is too short.', 400);
    }
    sessionStorage.removeItem(PENDING_KEY);
    return previewSession(email);
  },
  async login(input) {
    assertCaptcha('login', input.captchaChallengeId, input.captchaAnswer);
    if (input.password.length < 10) {
      throw new AuthApiError('INVALID_CREDENTIALS', 'Invalid credentials.', 401);
    }
    return previewSession(input.email.trim().toLowerCase());
  },
  async logout() {
    sessionStorage.removeItem(SESSION_KEY);
  },
  async updateAvatar(avatarId) {
    const session = readSession();
    if (!session.authenticated || !session.user) {
      throw new AuthApiError('AUTH_REQUIRED', 'Sign in is required.', 401);
    }
    session.user = {
      ...session.user,
      avatarId,
      avatarUrl: `/images/account-avatars/${avatarId}.webp`,
    };
    sessionStorage.setItem(SESSION_KEY, JSON.stringify(session));
    return session.user;
  },
};
