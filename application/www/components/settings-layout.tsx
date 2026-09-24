'use client';

import React from 'react';
import { useRouterStore, Route } from './router';
import { Flex, Center, Box, Tabs, HStack, Separator, Image } from '@chakra-ui/react';
import { useLanguage } from "@/contexts/language-context";
import {
    LuKeyboard, LuRocket, LuLightbulb, LuCpu, LuGamepad,
    LuChartSpline,
    //  LuWifi, LuMonitor
} from 'react-icons/lu';
import { navigationEvents } from '@/lib/event-manager';
import { LanguageSwitcher } from '@/components/language-switcher'
import { FinishConfigButton } from './finish-config-button';
import { useGamepadConfig } from '@/contexts/gamepad-config-context';
import { BuildVariantBadge } from '@hbox/build-variant-badge';
import { UserAuthControl } from '@/components/user-auth-control';
// import { ColorModeSwitcher } from "@/components/color-mode-switcher";

export function SettingsLayout({ children }: { children: React.ReactNode }) {
    const { t } = useLanguage();
    const { currentRoute, setRoute } = useRouterStore();

    const { finishConfigDisabled, configEditingBlocked } = useGamepadConfig();
    const tabs = [
        { id: 'global' as Route, label: t.SETTINGS_TAB_GLOBAL, icon: LuGamepad },
        { id: 'keys' as Route, label: t.SETTINGS_TAB_KEYS, icon: LuKeyboard },
        { id: 'buttons-performance' as Route, label: t.SETTINGS_TAB_BUTTONS_PERFORMANCE, icon: LuRocket },
        { id: 'lighting' as Route, label: t.SETTINGS_TAB_LEDS, icon: LuLightbulb },
        { id: 'switch-marking' as Route, label: t.SETTINGS_TAB_SWITCH_MARKING, icon: LuChartSpline },
        { id: 'firmware' as Route, label: t.SETTINGS_TAB_FIRMWARE, icon: LuCpu },
        // { id: 'button-monitor' as Route, label: '按键监控测试', icon: LuMonitor },
    ];

    const handleValueChange = async (details: { value: string }) => {
        if (details.value === currentRoute) return;
        const canNavigate = await navigationEvents.emit(details.value as Route);
        if (canNavigate) {
            // Route first so a slow STM32 QSPI commit can never hold the tab UI
            // on the previous page. The global draft survives the unmount and
            // the shared monitor lease pauses/resumes the destination page.
            setRoute(details.value as Route);
        }
    };

    return (
        <Flex direction="column" height="100%" flex={1} >

            {/* 添加语言切换按钮 */}
            <HStack
                w="full"
                gap={4}
                flexWrap="wrap"
                top={"8px"}
                right={4}
                zIndex={1}
                justifyContent="space-between"
                bg="bg.muted"
                boxShadow="0 1px 3px rgba(0, 0, 0, 0.4)"
                padding="7px 10px 7px 7px"
            >
                <HStack justifyContent="flex-start" position="relative" minW={0} maxW="100%">
                    <Image
                        src="/images/xora-mono-slate.svg"
                        alt="XORA 星溯"
                        width="165px"
                        height="44px"
                        objectFit="contain"
                        transform="scale(0.666667)"
                        transformOrigin="center center"
                        flexShrink={0}
                        display="block"
                    />
                    <Tabs.Root
                        defaultValue={currentRoute}
                        value={currentRoute}
                        size="md"
                        variant="plain"
                        colorPalette="green"
                        onValueChange={handleValueChange}
                    >
                        <Tabs.List bg="bg.muted" width="100%"  >
                            {tabs.map((tab, index) => (
                                <React.Fragment key={tab.id}>
                                    <Tabs.Trigger
                                        value={tab.id}
                                        w="fit-content"
                                        padding="0 18px"
                                        fontWeight={"bold"}
                                        justifyContent="center"
                                    >
                                        <Box as={tab.icon} mr={0} />
                                        <span>{tab.label}</span>
                                    </Tabs.Trigger>

                                    {index < tabs.length - 1 && (
                                        <Separator orientation="vertical" height="6" alignSelf={"center"} />
                                    )}
                                </React.Fragment>
                            ))}
                            <Tabs.Indicator rounded="l2" />
                        </Tabs.List>
                    </Tabs.Root>
                </HStack>
                <HStack justifyContent="flex-end" gap="10px" ml="auto" flexShrink={0}>
                    <BuildVariantBadge />
                    <FinishConfigButton disabled={finishConfigDisabled || configEditingBlocked} />
                    <UserAuthControl />
                    <LanguageSwitcher />
                    {/* <ColorModeSwitcher /> */}
                </HStack>
            </HStack>


            <Flex direction="column" flex={1} minHeight={0} inert={configEditingBlocked ? true : undefined}>
                <Center
                    flex={1}
                    minHeight={0}
                    overflow={currentRoute === 'switch-marking' ? 'hidden' : undefined}
                >
                    {children}
                </Center>
            </Flex>
        </Flex>
    );
} 
