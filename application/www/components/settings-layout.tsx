'use client';

import { useRouterStore } from './router';
import { Flex, Center, Box, Tabs } from '@chakra-ui/react';
import { ProfileSelect } from '@/components/profile-select';
import { useLanguage } from "@/contexts/language-context";
import { LuKeyboard, LuRocket, LuLightbulb, LuKeyRound, LuCpu, LuChartSpline,   } from 'react-icons/lu';
import type { Route } from './router';
import { navigationEvents } from '@/lib/event-manager';

export function SettingsLayout({ children }: { children: React.ReactNode }) {
    const { t } = useLanguage();
    const { currentRoute, setRoute } = useRouterStore();

    const tabs = [
        { id: 'keys' as Route, label: t.SETTINGS_TAB_KEYS, icon: LuKeyboard },
        { id: 'leds' as Route, label: t.SETTINGS_TAB_LEDS, icon: LuLightbulb },
        { id: 'rapid-trigger' as Route, label: t.SETTINGS_TAB_RAPID_TRIGGER, icon: LuRocket },
        { id: 'hotkeys' as Route, label: t.SETTINGS_TAB_HOTKEYS, icon: LuKeyRound },
        { id: 'switch-marking' as Route, label: t.SETTINGS_TAB_SWITCH_MARKING, icon: LuChartSpline },
        { id: 'firmware' as Route, label: t.SETTINGS_TAB_FIRMWARE, icon: LuCpu },
    ];

    const handleValueChange = async (details: { value: string }) => {
        const canNavigate = await navigationEvents.emit(details.value as Route);
        if (canNavigate) {
            await setRoute(details.value as Route);
        }
    };

    return (
        <Flex direction="column" height="100%" flex={1}>
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
                            width="180px"
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
                <Center pt={4} height="50px">
                    {['keys', 'leds', 'rapid-trigger'].includes(currentRoute) && <ProfileSelect />}
                </Center>
                <Center pt={4} flex={1}>
                    {children}
                </Center>
            </Flex>
        </Flex>
    );
} 