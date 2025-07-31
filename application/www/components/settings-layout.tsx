'use client';

import { useRouterStore, Route } from './router';
import { Flex, Center, Box, Tabs, HStack } from '@chakra-ui/react';
import { useLanguage } from "@/contexts/language-context";
import {
    LuKeyboard, LuRocket, LuLightbulb, LuCpu, LuGamepad,
    //  LuWifi, LuMonitor, LuChartSpline 
} from 'react-icons/lu';
import { navigationEvents } from '@/lib/event-manager';
import { LanguageSwitcher } from '@/components/language-switcher'
// import { ColorModeSwitcher } from "@/components/color-mode-switcher";

export function SettingsLayout({ children }: { children: React.ReactNode }) {
    const { t } = useLanguage();
    const { currentRoute, setRoute } = useRouterStore();

    const tabs = [
        { id: 'global' as Route, label: t.SETTINGS_TAB_GLOBAL, icon: LuGamepad },
        { id: 'keys' as Route, label: t.SETTINGS_TAB_KEYS, icon: LuKeyboard },
        { id: 'lighting' as Route, label: t.SETTINGS_TAB_LEDS, icon: LuLightbulb },
        { id: 'buttons-performance' as Route, label: t.SETTINGS_TAB_BUTTONS_PERFORMANCE, icon: LuRocket },
        // { id: 'switch-marking' as Route, label: t.SETTINGS_TAB_SWITCH_MARKING, icon: LuChartSpline },
        { id: 'firmware' as Route, label: t.SETTINGS_TAB_FIRMWARE, icon: LuCpu },
        // { id: 'websocket' as Route, label: 'WebSocket测试', icon: LuWifi },
        // { id: 'button-monitor' as Route, label: '按键监控测试', icon: LuMonitor },
    ];

    const handleValueChange = async (details: { value: string }) => {
        const canNavigate = await navigationEvents.emit(details.value as Route);
        if (canNavigate) {
            await setRoute(details.value as Route);
        }
    };

    return (
        <Flex direction="column" height="100%" flex={1} >

            {/* 添加语言切换按钮 */}
            <HStack
                w="full"
                gap={4}
                top={"8px"}
                right={4}
                zIndex={1}
                justifyContent="space-between"
                bg="bg.muted"
                boxShadow="0 1px 3px rgba(0, 0, 0, 0.4)"
                padding="7px 10px 7px 7px"
            >

                <Tabs.Root
                    defaultValue={currentRoute}
                    value={currentRoute}
                    size="md"
                    variant="plain"
                    colorPalette="green"
                    onValueChange={handleValueChange}
                >
                    <Tabs.List justifyContent="start" bg="bg.muted" width="100%">
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
                
                <HStack justifyContent="flex-end" >
                    <LanguageSwitcher />
                    {/* <ColorModeSwitcher /> */}
                </HStack>
            </HStack>


            <Flex direction="column" flex={1} height="100%">
                <Center flex={1}>
                    {children}
                </Center>
            </Flex>
        </Flex>
    );
} 