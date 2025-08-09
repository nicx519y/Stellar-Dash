'use client';

import { create } from 'zustand';
import { useEffect } from 'react';
import { GlobalSettingContent } from '@/components/global-setting-content';
import { KeysSettingContent } from '@/components/keys-setting-content';
import { LEDsSettingContent } from '@/components/leds-setting-content';
import { ButtonsPerformanceContent } from '@/components/buttons-performance-content';
import { FirmwareContent } from '@/components/firmware-content';
import { SwitchMarkingContent } from '@/components/switch-marking-content';

export type Route = '' | 'global' | 'keys' | 'lighting' | 'buttons-performance' | 'switch-marking' | 'firmware';

interface RouterState {
    currentRoute: Route;
    setRoute: (route: Route) => void;
}

export const useRouterStore = create<RouterState>((set) => ({
    currentRoute: '',
    setRoute: (route) => {
        set({ currentRoute: route });
        // 只在客户端环境中使用 window 对象
        if (typeof window !== 'undefined') {
            window.history.pushState(null, '', `/${route}`);
        }
    },
}));

export function Router() {
    const { currentRoute, setRoute } = useRouterStore();

    useEffect(() => {
        // useEffect 只在客户端运行，所以这里可以安全使用 window
        const handlePopState = () => {
            const path = window.location.pathname.slice(1) || '';
            setRoute(path as Route);
        };

        window.addEventListener('popstate', handlePopState);
        
        const initialPath = window.location.pathname.slice(1) || 'global';
        setRoute(initialPath as Route);

        return () => window.removeEventListener('popstate', handlePopState);
    }, [setRoute]);

    switch (currentRoute) {
        case 'global':
            return <GlobalSettingContent />;
        case 'lighting':
            return <LEDsSettingContent />;
        case 'buttons-performance':
            return <ButtonsPerformanceContent />;
        case 'keys':
            return <KeysSettingContent />;
        case 'firmware':
            return <FirmwareContent />;
        case 'switch-marking':
            return <SwitchMarkingContent />;
        // case 'websocket':
        //     return <WebSocketTest />;
        // case 'buttons-monitor':
        //     return <ButtonMonitorTest />;
        default:
            return <></>;
    }
} 