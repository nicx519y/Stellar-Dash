'use client';

import { createContext, useCallback, useContext, useState, useEffect } from 'react';
import { UI_TEXT, UI_TEXT_ZH } from '@/types/gamepad-config';
import { readLanguagePreference, saveLanguagePreference, type Language } from '@/lib/language-preference';

// 使用 Record 和联合类型来定义文本类型
type TextValue = string;
type TextKey = keyof typeof UI_TEXT;
type TextType = Record<TextKey, TextValue>;

type LanguageContextType = {
    currentLanguage: 'en' | 'zh';
    setLanguage: (lang: 'en' | 'zh') => void;
    t: TextType;
};

const LanguageContext = createContext<LanguageContextType>({
    currentLanguage: 'en',
    setLanguage: () => {},
    t: UI_TEXT,
});

export function LanguageProvider({ children }: { children: React.ReactNode }) {
    // Static HTML cannot know this browser's preference. Keep translated content
    // unmounted until it is resolved, so hydration never displays the wrong locale.
    const [currentLanguage, setCurrentLanguage] = useState<Language | null>(null);
    useEffect(() => {
        const language = readLanguagePreference();
        document.documentElement.lang = language;
        setCurrentLanguage(language);
    }, []);
    const setLanguage = useCallback(
        (lang: Language) => {
            saveLanguagePreference(lang);
            document.documentElement.lang = lang;
            setCurrentLanguage(lang);
        },
        [],
    );

    if (currentLanguage === null) return null;

    const value = {
        currentLanguage,
        setLanguage,
        t: currentLanguage === 'en' ? UI_TEXT : UI_TEXT_ZH,
    };

    return (
        <LanguageContext.Provider value={value}>
            {children}
        </LanguageContext.Provider>
    );
}

export const useLanguage = () => useContext(LanguageContext);
