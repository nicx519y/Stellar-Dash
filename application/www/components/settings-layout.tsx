'use client';

import { useRouterStore } from './router';
import { Flex, Center, Box, Tabs } from '@chakra-ui/react';
import { useLanguage } from "@/contexts/language-context";
import { LuKeyboard, LuRocket, LuLightbulb, LuCpu, LuChartSpline, LuGamepad, LuWifi } from 'react-icons/lu';
import type { Route } from './router';
import { navigationEvents } from '@/lib/event-manager';

export function SettingsLayout({ children }: { children: React.ReactNode }) {
    const { t } = useLanguage();
    const { currentRoute, setRoute } = useRouterStore();

    const tabs = [
        { id: 'global' as Route, label: t.SETTINGS_TAB_GLOBAL, icon: LuGamepad },
        { id: 'keys' as Route, label: t.SETTINGS_TAB_KEYS, icon: LuKeyboard },
        { id: 'lighting' as Route, label: t.SETTINGS_TAB_LEDS, icon: LuLightbulb },
        { id: 'buttons-performance' as Route, label: t.SETTINGS_TAB_BUTTONS_PERFORMANCE, icon: LuRocket },
        { id: 'switch-marking' as Route, label: t.SETTINGS_TAB_SWITCH_MARKING, icon: LuChartSpline },
        { id: 'firmware' as Route, label: t.SETTINGS_TAB_FIRMWARE, icon: LuCpu },
        { id: 'websocket' as Route, label: 'WebSocket测试', icon: LuWifi },
    ];

    const handleValueChange = async (details: { value: string }) => {
        const canNavigate = await navigationEvents.emit(details.value as Route);
        if (canNavigate) {
            await setRoute(details.value as Route);
        }
    };

    return (
        <Flex direction="column" height="100%" flex={1} >
            <Tabs.Root
                defaultValue={currentRoute}
                value={currentRoute}
                size="md"
                variant="plain"
                colorPalette="green"
                boxShadow="0 1px 3px rgba(0, 0, 0, 0.4)"    
                
                onValueChange={handleValueChange}
            >
                <Tabs.List justifyContent="center" bg="bg.muted" width="100%">
                    {tabs.map((tab) => (
                        <Tabs.Trigger
                            key={tab.id}
                            value={tab.id}
                            width="200px"
                            justifyContent="center"
                        >
                            <Box as={tab.icon} mr={0} />
                            <span>{tab.label}</span>
                        </Tabs.Trigger>
                    ))}
                    <Tabs.Indicator rounded="l2" />
                </Tabs.List>
            </Tabs.Root>
            
            <Flex direction="column" flex={1} height="100%">
                <Center flex={1}>
                    {children}
                </Center>
            </Flex>
        </Flex>
    );
} 