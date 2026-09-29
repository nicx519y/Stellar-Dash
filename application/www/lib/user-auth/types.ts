export type AccountRole = 'admin' | 'user';

export interface AuthUser {
  uid: string;
  email: string;
  displayName: string;
  role: AccountRole;
  avatarId: string | null;
  avatarUrl: string | null;
}

export interface AuthSession {
  authenticated: boolean;
  user?: AuthUser;
  expiresAt?: number;
  registrationEnabled: boolean;
}

export type CaptchaAction = 'register' | 'login';

export interface CaptchaChallenge {
  challengeId: string;
  image: string;
  expiresAt: number;
}

export interface RegistrationRequestResult {
  accepted: true;
  verificationToken?: string;
}

export interface UserAuthRuntime {
  getSession(): Promise<AuthSession>;
  createCaptcha(action: CaptchaAction): Promise<CaptchaChallenge>;
  requestRegistration(input: {
    email: string;
    locale: 'en' | 'zh';
    captchaChallengeId: string;
    captchaAnswer: string;
  }): Promise<RegistrationRequestResult>;
  completeRegistration(input: {
    token: string;
    password: string;
  }): Promise<AuthSession>;
  login(input: {
    email: string;
    password: string;
    captchaChallengeId: string;
    captchaAnswer: string;
  }): Promise<AuthSession>;
  logout(): Promise<void>;
  updateAvatar(avatarId: string): Promise<AuthUser>;
}

export class AuthApiError extends Error {
  constructor(
    public readonly code: string,
    message: string,
    public readonly status: number,
  ) {
    super(message);
    this.name = 'AuthApiError';
  }
}
