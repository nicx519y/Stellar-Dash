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

// 字体加载检测函数
const waitForFont = (fontFamily: string): Promise<void> => {
    return new Promise((resolve) => {
        if (document.fonts && document.fonts.check) {
            // 现代浏览器
            const checkFont = () => {
                if (document.fonts.check(`12px ${fontFamily}`)) {
                    resolve();
                } else {
                    setTimeout(checkFont, 100);
                }
            };
            checkFont();
        } else {
            // 兼容性处理
            setTimeout(resolve, 1000);
        }
    });
};

export function LanguageProvider({ children }: { children: React.ReactNode }) {
    const [currentLanguage, setCurrentLanguage] = useState<'en' | 'zh'>('en');

    // 根据语言设置字体
    useEffect(() => {
        const applyFont = async () => {
            const targetFont = 'custom_en';
            
            try {
                // 等待字体加载
                await waitForFont(targetFont);
                
                // 应用字体
                document.documentElement.style.fontFamily = 
                    // currentLanguage === 'zh' 
                    //     ? 'system-ui, sans-serif'
                    //     : 
                        'custom_en, system-ui, sans-serif';
            } catch (error) {
                console.warn('字体加载失败，使用备用字体:', error);
                // 如果字体加载失败，使用备用字体
                document.documentElement.style.fontFamily = 'system-ui, sans-serif';
            }
        };

        applyFont();
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