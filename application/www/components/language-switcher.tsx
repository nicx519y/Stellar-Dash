'use client';

import { useLanguage } from '@/contexts/language-context';
import { Button } from '@chakra-ui/react';

export function LanguageSwitcher() {
    const { currentLanguage, setLanguage } = useLanguage();

    return (
        <Button
            colorPalette="green"
            variant="surface"
            onClick={() => setLanguage(currentLanguage === 'en' ? 'zh' : 'en')}
            size="xs"
            aria-label={currentLanguage === 'en' ? 'Switch to Chinese' : '切换到英文'}
        >
            {currentLanguage === 'en' ? 'ZH' : 'EN'}
        </Button>
    );
}
