'use client';

import { create } from 'zustand';
import { useEffect } from 'react';
import { GlobalSettingContent } from "@/components/global-setting-content";
import { KeysSettingContent } from "@/components/keys-setting-content";
import { LEDsSettingContent } from "@/components/leds-setting-content";
import { ButtonsPerformanceContent } from "@/components/buttons-performance-content";
import { FirmwareContent } from '@/components/firmware-content';
import { SwitchMarkingContent } from '@/components/switch-marking-content';
import WebSocketTest from '@/components/websocket-test';

export type Route = '' | 'global' | 'keys' | 'lighting' | 'buttons-performance' | 'switch-marking' | 'firmware' | 'websocket';

interface RouterState {
    currentRoute: Route;
    setRoute: (route: Route) => void;
}

export const useRouterStore = create<RouterState>((set) => ({
    currentRoute: '',
    setRoute: (route) => {
        set({ currentRoute: route });
        window.history.pushState(null, '', `/${route}`);
    },
}));

export function Router() {
    const { currentRoute, setRoute } = useRouterStore();

    useEffect(() => {
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
        case 'websocket':
            return <WebSocketTest />;
        default:
            return <></>;
    }
} 