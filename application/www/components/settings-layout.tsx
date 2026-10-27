'use client';

import { publicAssetUrl } from '@/lib/public-asset';

import React from 'react';
import { useRouterStore, Route } from './router';
import { Flex, Center, Box, Tabs, HStack, Separator, Image, Grid } from '@chakra-ui/react';
import { useLanguage } from "@/contexts/language-context";
import {
    LuKeyboard, LuRocket, LuLightbulb, LuCpu, LuGamepad,
    LuChartSpline, LuScrollText,
    //  LuWifi, LuMonitor
} from 'react-icons/lu';
import { navigationEvents } from '@/lib/event-manager';
import { LanguageSwitcher } from '@/components/language-switcher'
import { FinishConfigButton } from './finish-config-button';
import { useGamepadFields } from '@/contexts/gamepad-config-context';
import { BuildVariantBadge } from '@hbox/build-variant-badge';
import { UserAuthControl } from '@/components/user-auth-control';
import { useUserAuth } from '@/contexts/user-auth-context';
import { useLogOperation } from '@/lib/log-operation';
// import { ColorModeSwitcher } from "@/components/color-mode-switcher";

export function SettingsLayout({ children }: { children: React.ReactNode }) {
    const logOperation = useLogOperation(state => state.operation);
    const { t } = useLanguage();
    const { currentRoute, setRoute } = useRouterStore();
    const { session, loading: authLoading } = useUserAuth();
    const isAdmin = !authLoading && session.authenticated && session.user?.role === 'admin';

    const { finishConfigDisabled, configEditingBlocked, dataIsReady } = useGamepadFields("finishConfigDisabled", "configEditingBlocked", "dataIsReady");
    const tabs = [
        { id: 'global' as Route, label: t.SETTINGS_TAB_GLOBAL, icon: LuGamepad },
        { id: 'keys' as Route, label: t.SETTINGS_TAB_KEYS, icon: LuKeyboard },
        { id: 'buttons-performance' as Route, label: t.SETTINGS_TAB_BUTTONS_PERFORMANCE, icon: LuRocket },
        { id: 'lighting' as Route, label: t.SETTINGS_TAB_LEDS, icon: LuLightbulb },
        { id: 'firmware' as Route, label: t.SETTINGS_TAB_FIRMWARE, icon: LuCpu },
        { id: 'view-logs' as Route, label: t.SETTINGS_TAB_VIEW_LOGS, icon: LuScrollText },
        ...(isAdmin ? [
            { id: 'switch-marking' as Route, label: t.SETTINGS_TAB_SWITCH_MARKING, icon: LuChartSpline },
        ] : []),
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
            <Grid
                data-testid="settings-header"
                templateColumns={{ base: "minmax(0, 1fr)", "2xl": "minmax(0, 1fr) auto" }}
                alignItems="center"
                w="full"
                gap={2}
                top={"8px"}
                right={4}
                zIndex={1}
                justifyContent="space-between"
                bg="bg.muted"
                boxShadow="0 1px 3px rgba(0, 0, 0, 0.4)"
                padding="7px 10px 7px 7px"
            >
                <HStack justifyContent="flex-start" position="relative" minW={0} maxW="100%">
                    <Box position="relative" flexShrink={0} w="165px" h="44px">
                    <Image
                        src={publicAssetUrl('/images/xora-mono-slate.svg')}
                        alt="XORA 星溯"
                        width="165px"
                        height="44px"
                        objectFit="contain"
                        transform="scale(0.666667)"
                        transformOrigin="center center"
                        flexShrink={0}
                        display="block"
                    />
                    <Box position="absolute" bottom="-4px" right="0"><BuildVariantBadge /></Box>
                    </Box>
                    <Tabs.Root
                        defaultValue={currentRoute}
                        value={currentRoute}
                        size="md"
                        variant="plain"
                        colorPalette="green"
                        onValueChange={handleValueChange}
                        minW={0}
                        overflowX="auto"
                    >
                        <Tabs.List bg="bg.muted" width="100%">
                            {tabs.map((tab, index) => (
                                <React.Fragment key={tab.id}>
                                    <Tabs.Trigger
                                        disabled={logOperation === 'clear'}
                                        value={tab.id}
                                        w="fit-content"
                                        padding="0 18px"
                                        fontWeight={"bold"}
                                        flexShrink={0}
                                        whiteSpace="nowrap"
                                        justifyContent="center"
                                        _selected={{ bg: 'bg', boxShadow: 'xs' }}
                                    >
                                        <Box as={tab.icon} mr={0} />
                                        <span>{tab.label}</span>
                                    </Tabs.Trigger>

                                    {index < tabs.length - 1 && (
                                        <Separator orientation="vertical" height="6" alignSelf={"center"} />
                                    )}
                                </React.Fragment>
                            ))}
                        </Tabs.List>
                    </Tabs.Root>
                </HStack>
                <HStack inert={logOperation ? true : undefined} opacity={logOperation ? 0.5 : 1} justifyContent="flex-end" gap="10px" w="full" minW={0}>
                    <Box flex={{ base: 1, sm: "0 0 300px" }} minW={0} visibility={dataIsReady ? "visible" : "hidden"} aria-hidden={!dataIsReady}>
                        <FinishConfigButton disabled={!dataIsReady || finishConfigDisabled || configEditingBlocked} />
                    </Box>
                    <UserAuthControl />
                    <LanguageSwitcher />
                    {/* <ColorModeSwitcher /> */}
                </HStack>
            </Grid>


            {/* Firmware owns its operation controls, including authorization,
                cancellation before activation and recovery while config is locked. */}
            <Flex direction="column" flex={1} minHeight={0} inert={configEditingBlocked && currentRoute !== 'firmware' ? true : undefined}>
                <Center
                    flex={1}
                    minHeight={0}
                    alignItems={currentRoute === 'firmware' || currentRoute === 'view-logs' ? 'flex-start' : 'center'}
                    overflow={currentRoute === 'switch-marking' ? 'hidden' : undefined}
                >
                    {children}
                </Center>
            </Flex>
        </Flex>
    );
} 
