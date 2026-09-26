'use client';

import { useLanguage } from '@/contexts/language-context';
import { Button } from '@chakra-ui/react';
import { useEffect } from 'react';
const LANGUAGE_STORAGE_KEY = 'preferred_language';

export function LanguageSwitcher() {
    const { currentLanguage, setLanguage } = useLanguage();

    // 初始化时从本地存储读取语言设置
    useEffect(() => {
        const savedLanguage = localStorage.getItem(LANGUAGE_STORAGE_KEY);
        if (savedLanguage === 'en' || savedLanguage === 'zh') {
            setLanguage(savedLanguage);
        }
    }, []);

    // 当语言改变时保存到本地存储
    useEffect(() => {
        localStorage.setItem(LANGUAGE_STORAGE_KEY, currentLanguage);
    }, [currentLanguage]);

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
