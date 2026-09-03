'use client';

import { create } from 'zustand';
import { useEffect, useRef } from 'react';
import { useGamepadConfig } from '@/contexts/gamepad-config-context';
import { GlobalSettingContent } from '@/components/global-setting-content';
import { KeysSettingContent } from '@/components/keys-setting-content';
import { LEDsSettingContent } from '@/components/leds-setting-content';
import { ButtonsPerformanceContent } from '@/components/buttons-performance-content';
import { FirmwareContent } from '@/components/firmware-content';
import { ViewLogsContent } from '@/components/view-logs-content';
import { SwitchMarkingContent } from '@/components/switch-marking-content';
import {
    pathnameForRoute,
    popstateHistoryMode,
    routeFromPathname,
    normalizeRouteBasePath,
    type Route,
} from '@/lib/router-path';

export type { Route } from '@/lib/router-path';
type HistoryMode = 'push' | 'replace' | 'none';

interface RouterState {
    currentRoute: Route;
    basePath: string;
    setRoute: (route: Route, historyMode?: HistoryMode) => void;
    setBasePath: (basePath: string) => void;
}

export const useRouterStore = create<RouterState>((set, get) => ({
    currentRoute: '',
    basePath: '/',
    setRoute: (route, historyMode = 'push') => {
        set({ currentRoute: route });
        if (typeof window !== 'undefined' && historyMode !== 'none') {
            const pathname = pathnameForRoute(route, get().basePath);
            if (window.location.pathname !== pathname) {
                if (historyMode === 'replace') {
                    window.history.replaceState(null, '', pathname);
                } else {
                    window.history.pushState(null, '', pathname);
                }
            }
        }
    },
    setBasePath: (basePath) => {
        set({ basePath: normalizeRouteBasePath(basePath) });
    },
}));

export function Router() {
    const { currentRoute, basePath, setRoute, setBasePath } = useRouterStore();
    const { deviceSession, flushDeferredConfig } = useGamepadConfig();
    const flushDeferredConfigRef = useRef(flushDeferredConfig);
    flushDeferredConfigRef.current = flushDeferredConfig;

    useEffect(() => {
        // useEffect 只在客户端运行，所以这里可以安全使用 window
        const handlePopState = () => {
            const route = routeFromPathname(window.location.pathname, basePath);
            setRoute(
                route,
                popstateHistoryMode(window.location.pathname, basePath),
            );
            window.setTimeout(() => {
                void flushDeferredConfigRef.current(undefined, true).catch((error) => {
                    console.error('浏览器导航后的后台保存失败:', error);
                });
            }, 0);
        };

        window.addEventListener('popstate', handlePopState);
        
        const initialRoute = routeFromPathname(window.location.pathname, basePath);
        setRoute(initialRoute, 'replace');

        return () => window.removeEventListener('popstate', handlePopState);
    }, [basePath, setRoute]);

    useEffect(() => {
        const nextBasePath = deviceSession?.webConfigBasePath ?? '/';
        if (nextBasePath === basePath) {
            return;
        }
        setBasePath(nextBasePath);
        // Keep the selected settings section while moving into the profile-
        // specific URL namespace selected by authenticated device identity.
        setRoute(currentRoute || 'global', 'replace');
    }, [basePath, currentRoute, deviceSession, setBasePath, setRoute]);

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
        case 'view-logs':
            return <ViewLogsContent />;
        // case 'buttons-monitor':
        //     return <ButtonMonitorTest />;

        default:
            return <></>;
    }
} 
