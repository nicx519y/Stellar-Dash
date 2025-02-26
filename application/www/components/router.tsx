'use client';

import { create } from 'zustand';
import { useEffect } from 'react';
import { HotkeysSettingContent } from "@/components/hotkeys-setting-content";
import { KeysSettingContent } from "@/components/keys-setting-content";
import { LEDsSettingContent } from "@/components/leds-setting-content";
import { RapidTriggerContent } from "@/components/rapid-trigger-content";
import { FirmwareContent } from '@/components/firmware-content';
import { SwitchMarkingContent } from '@/components/switch-marking-content';

export type Route = '' | 'keys' | 'hotkeys' | 'leds' | 'rapid-trigger' | 'switch-marking' | 'firmware';

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
            const path = window.location.pathname.slice(1) || 'keys';
            setRoute(path as Route);
        };

        window.addEventListener('popstate', handlePopState);
        
        const initialPath = window.location.pathname.slice(1) || 'keys';
        setRoute(initialPath as Route);

        return () => window.removeEventListener('popstate', handlePopState);
    }, [setRoute]);

    switch (currentRoute) {
        case 'hotkeys':
            return <HotkeysSettingContent />;
        case 'leds':
            return <LEDsSettingContent />;
        case 'rapid-trigger':
            return <RapidTriggerContent />;
        case 'keys':
            return <KeysSettingContent />;
        case 'firmware':
            return <FirmwareContent />;
        case 'switch-marking':
            return <SwitchMarkingContent />;
        default:
            return <></>;
    }
} 