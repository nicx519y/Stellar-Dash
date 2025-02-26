'use client';

import { createContext, useContext, useState, useEffect } from 'react';
import { UI_TEXT, UI_TEXT_ZH } from '@/types/gamepad-config';

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
    const [currentLanguage, setCurrentLanguage] = useState<'en' | 'zh'>('en');

    // 根据语言设置字体
    useEffect(() => {
        document.documentElement.style.fontFamily = 
            currentLanguage === 'zh' 
                ? '"Microsoft YaHei", system-ui, sans-serif'
                : 'system-ui, sans-serif';
    }, [currentLanguage]);

    const value = {
        currentLanguage,
        setLanguage: (lang: 'en' | 'zh') => setCurrentLanguage(lang),
        t: currentLanguage === 'en' ? UI_TEXT : UI_TEXT_ZH,
    };

    return (
        <LanguageContext.Provider value={value}>
            {children}
        </LanguageContext.Provider>
    );
}

export const useLanguage = () => useContext(LanguageContext); 