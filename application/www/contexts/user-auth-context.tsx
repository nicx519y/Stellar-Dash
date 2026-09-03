'use client';

import {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useMemo,
  useState,
} from 'react';
import { userAuthRuntime } from '@hbox/user-auth-runtime';
import type {
  AuthSession,
  CaptchaAction,
  RegistrationRequestResult,
} from '@/lib/user-auth/types';

interface UserAuthContextValue {
  session: AuthSession;
  loading: boolean;
  refreshSession(): Promise<AuthSession>;
  createCaptcha: typeof userAuthRuntime.createCaptcha;
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
  updateAvatar(avatarId: string): Promise<AuthSession['user']>;
}

const INITIAL_SESSION: AuthSession = {
  authenticated: false,
  registrationEnabled: false,
};

const UserAuthContext = createContext<UserAuthContextValue | null>(null);

export function UserAuthProvider({ children }: { children: React.ReactNode }) {
  const [session, setSession] = useState<AuthSession>(INITIAL_SESSION);
  const [loading, setLoading] = useState(true);

  const refreshSession = useCallback(async () => {
    const next = await userAuthRuntime.getSession();
    setSession(next);
    return next;
  }, []);

  useEffect(() => {
    let active = true;
    userAuthRuntime.getSession()
      .then(next => {
        if (active) setSession(next);
      })
      .catch(() => {
        if (active) setSession(INITIAL_SESSION);
      })
      .finally(() => {
        if (active) setLoading(false);
      });
    return () => {
      active = false;
    };
  }, []);

  const requestRegistration = useCallback(
    (input: {
      email: string;
      locale: 'en' | 'zh';
      captchaChallengeId: string;
      captchaAnswer: string;
    }) => userAuthRuntime.requestRegistration(input),
    [],
  );
  const completeRegistration = useCallback(async (input: {
    token: string;
    password: string;
  }) => {
    const next = await userAuthRuntime.completeRegistration(input);
    setSession(next);
    return next;
  }, []);
  const login = useCallback(async (input: {
    email: string;
    password: string;
    captchaChallengeId: string;
    captchaAnswer: string;
  }) => {
    const next = await userAuthRuntime.login(input);
    setSession(next);
    return next;
  }, []);
  const logout = useCallback(async () => {
    await userAuthRuntime.logout();
    setSession({
      authenticated: false,
      registrationEnabled: session.registrationEnabled,
    });
  }, [session.registrationEnabled]);
  const updateAvatar = useCallback(async (avatarId: string) => {
    const user = await userAuthRuntime.updateAvatar(avatarId);
    setSession(current => ({ ...current, authenticated: true, user }));
    return user;
  }, []);

  const value = useMemo<UserAuthContextValue>(() => ({
    session,
    loading,
    refreshSession,
    createCaptcha: (action: CaptchaAction) => userAuthRuntime.createCaptcha(action),
    requestRegistration,
    completeRegistration,
    login,
    logout,
    updateAvatar,
  }), [
    session,
    loading,
    refreshSession,
    requestRegistration,
    completeRegistration,
    login,
    logout,
    updateAvatar,
  ]);

  return (
    <UserAuthContext.Provider value={value}>
      {children}
    </UserAuthContext.Provider>
  );
}

export function useUserAuth(): UserAuthContextValue {
  const value = useContext(UserAuthContext);
  if (!value) {
    throw new Error('useUserAuth must be used inside UserAuthProvider');
  }
  return value;
}
