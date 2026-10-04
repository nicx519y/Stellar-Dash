'use client';

import type { ReactNode } from 'react';
import { Center, Spinner, Text } from '@chakra-ui/react';
import { useUserAuth } from '@/contexts/user-auth-context';
import { useLanguage } from '@/contexts/language-context';

export function AdminAccessGuard({ children }: { children: ReactNode }) {
    const { session, loading } = useUserAuth();
    const { t } = useLanguage();

    if (loading) {
        return <Center minH="320px"><Spinner size="lg" colorPalette="green" /></Center>;
    }
    if (!session.authenticated || session.user?.role !== 'admin') {
        return (
            <Center minH="320px" padding={6}>
                <Text role="alert">{t.AUTH_ADMIN_ACCESS_REQUIRED}</Text>
            </Center>
        );
    }

    return children;
}
