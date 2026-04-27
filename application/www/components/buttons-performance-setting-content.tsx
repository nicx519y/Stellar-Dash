"use client";

import {
    Text,
    Button,
    Box,
    Table,
    VStack,
    HStack,
    Slider,
    Dialog,
    Fieldset,
    Portal,
} from "@chakra-ui/react";
import { useCallback, useEffect, useMemo, useState } from "react";
import { RapidTriggerConfig, RAPID_TRIGGER_SETTINGS_INTERACTIVE_IDS, ButtonPerformancePresetConfigs, ButtonPerformancePresetName, GameControllerButton } from "@/types/gamepad-config";
import { useLanguage } from "@/contexts/language-context";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import { LuSheet } from "react-icons/lu";
import { 
    SettingMainContentLayout, 
    MainContentHeader, 
    MainContentBody 
} from "@/components/setting-main-content-layout";

interface TriggerConfig {
    topDeadzone: number;
    bottomDeadzone: number;
    pressAccuracy: number;
    releaseAccuracy: number;
}

const defaultTriggerConfig: TriggerConfig = {
    topDeadzone: 0,
    bottomDeadzone: 0,
    pressAccuracy: 0,
    releaseAccuracy: 0
};

interface ButtonsPerformanceSettingContentProps {
    selectedButtons: number[];
    onSelectedButtonsChange: (selected: number[]) => void;
}

export function ButtonsPerformanceSettingContent({
    selectedButtons,
    onSelectedButtonsChange,
}: ButtonsPerformanceSettingContentProps) {
    const { t } = useLanguage();
    const { defaultProfile, updateProfileDetails } = useGamepadConfig();
    const [isInit, setIsInit] = useState<boolean>(false);
    const [triggerConfigs, setTriggerConfigs] = useState<RapidTriggerConfig[]>([]);
    const [needUpdate, setNeedUpdate] = useState<boolean>(false);
    const [isTableDialogOpen, setIsTableDialogOpen] = useState<boolean>(false);
    const { sendPendingCommandImmediately } = useGamepadConfig();
    const allKeys = RAPID_TRIGGER_SETTINGS_INTERACTIVE_IDS;

    const tableViewConfig = useMemo(() => triggerConfigs, [triggerConfigs]);

    useEffect(() => {
        return () => {
            sendPendingCommandImmediately('update_profile');
        }
    }, []);

    /**
     * 加载触发配置
     */
    useEffect(() => {
        if (defaultProfile.triggerConfigs && !isInit) {
            const triggerConfigs = { ...defaultProfile.triggerConfigs };
            setTriggerConfigs(allKeys.map(key => triggerConfigs.triggerConfigs?.[key] ?? defaultTriggerConfig));
            setIsInit(true);
            setNeedUpdate(false);
        }
    }, [defaultProfile]);

    useEffect(() => {
        if(needUpdate) {
            saveConfig();
            setNeedUpdate(false);
        }
    }, [needUpdate]);

    /**
     * 获取当前配置
     */
    const dpadMappedButtons = useMemo(() => {
        const keyMapping = defaultProfile.keysConfig?.keyMapping ?? {};
        const dpadKeys = [
            GameControllerButton.DPAD_UP,
            GameControllerButton.DPAD_DOWN,
            GameControllerButton.DPAD_LEFT,
            GameControllerButton.DPAD_RIGHT
        ];
        const mappedIds = dpadKeys.flatMap((key) => keyMapping[key] ?? []);
        const filtered = mappedIds
            .map((id) => Number(id))
            .filter((id) => Number.isFinite(id) && allKeys.includes(id));
        return Array.from(new Set(filtered));
    }, [defaultProfile.keysConfig?.keyMapping, allKeys]);

    const mappedRapidTriggerButtons = useMemo(() => {
        const keyMapping = defaultProfile.keysConfig?.keyMapping ?? {};
        const mappedIds = Object.values(keyMapping)
            .flatMap((ids) => ids ?? [])
            .map((id) => Number(id))
            .filter((id) => Number.isFinite(id) && allKeys.includes(id));
        return Array.from(new Set(mappedIds));
    }, [defaultProfile.keysConfig?.keyMapping, allKeys]);

    const normalizeIds = useCallback((ids: number[]) => {
        const unique = Array.from(new Set(ids.map((id) => Number(id))))
            .filter((id) => Number.isFinite(id) && allKeys.includes(id));
        unique.sort((a, b) => a - b);
        return unique;
    }, [allKeys]);

    const isSameSet = useCallback((a: number[], b: number[]) => {
        const na = normalizeIds(a);
        const nb = normalizeIds(b);
        if (na.length !== nb.length) return false;
        for (let i = 0; i < na.length; i++) {
            if (na[i] !== nb[i]) return false;
        }
        return true;
    }, [normalizeIds]);

    const scopeAllIds = useMemo(
        () => (mappedRapidTriggerButtons.length > 0 ? mappedRapidTriggerButtons : allKeys),
        [mappedRapidTriggerButtons, allKeys]
    );

    const scopeNonDpadIds = useMemo(
        () => scopeAllIds.filter((id) => !dpadMappedButtons.includes(id)),
        [scopeAllIds, dpadMappedButtons]
    );

    const activeButtonIds = useMemo(
        () => Array.from(new Set(selectedButtons.map((id) => Number(id)))).filter((id) => Number.isFinite(id) && allKeys.includes(id)),
        [selectedButtons, allKeys]
    );

    const getUnifiedValue = useCallback((key: keyof TriggerConfig) => {
        if (activeButtonIds.length === 0) return { isUnified: true, value: 0 };
        const firstId = activeButtonIds[0];
        const firstCfg = triggerConfigs[firstId] ?? defaultTriggerConfig;
        const firstValue = Number(firstCfg[key] ?? 0);
        const isUnified = activeButtonIds.every((id) => {
            const cfg = triggerConfigs[id] ?? defaultTriggerConfig;
            return Math.abs(Number(cfg[key] ?? 0) - firstValue) < 1e-6;
        });
        return { isUnified, value: isUnified ? firstValue : 0 };
    }, [activeButtonIds, triggerConfigs]);

    /**
     * 更新当前配置
     */
    const updateConfig = (key: keyof TriggerConfig, value: number) => {
        if (activeButtonIds.length === 0) return;
        const newTriggerConfigs = [...triggerConfigs];
        activeButtonIds.forEach((id) => {
            newTriggerConfigs[id] = {
                ...(newTriggerConfigs[id] ?? defaultTriggerConfig),
                [key]: value
            };
        });

        setTriggerConfigs(newTriggerConfigs);
        setNeedUpdate(true);
    };

    const applySelection = (ids: number[]) => {
        const next = Array.from(new Set(ids.map((id) => Number(id)))).filter((id) => Number.isFinite(id) && allKeys.includes(id));
        onSelectedButtonsChange(next);
    };

    const isScopeAllSelected = useMemo(() => isSameSet(activeButtonIds, scopeAllIds), [activeButtonIds, scopeAllIds, isSameSet]);
    const isScopeDpadSelected = useMemo(() => dpadMappedButtons.length > 0 && isSameSet(activeButtonIds, dpadMappedButtons), [activeButtonIds, dpadMappedButtons, isSameSet]);
    const isScopeNonDpadSelected = useMemo(() => scopeNonDpadIds.length > 0 && isSameSet(activeButtonIds, scopeNonDpadIds), [activeButtonIds, scopeNonDpadIds, isSameSet]);

    const approxEqual = (a: number, b: number) => Math.abs(a - b) < 1e-6;

    const getPresetConfig = (preset: ButtonPerformancePresetName) => {
        const found = ButtonPerformancePresetConfigs.find((p) => p.name === preset);
        return found?.configs ?? null;
    };

    const isPresetMatched = useCallback((preset: ButtonPerformancePresetName) => {
        const presetConfig = getPresetConfig(preset);
        if (!presetConfig || activeButtonIds.length === 0) return false;

        return activeButtonIds.every((id) => {
            const cfg = triggerConfigs[id] ?? defaultTriggerConfig;
            return (
                approxEqual(cfg.topDeadzone, presetConfig.topDeadzone) &&
                approxEqual(cfg.bottomDeadzone, presetConfig.bottomDeadzone) &&
                approxEqual(cfg.pressAccuracy, presetConfig.pressAccuracy) &&
                approxEqual(cfg.releaseAccuracy, presetConfig.releaseAccuracy)
            );
        });
    }, [activeButtonIds, triggerConfigs]);

    const matchedPreset = useMemo(() => {
        const candidates: ButtonPerformancePresetName[] = [
            ButtonPerformancePresetName.FASTEST,
            ButtonPerformancePresetName.BALANCED,
            ButtonPerformancePresetName.STABILITY
        ];
        for (const preset of candidates) {
            if (isPresetMatched(preset)) return preset;
        }
        return null;
    }, [isPresetMatched]);

    const applyPresetToSelection = (preset: ButtonPerformancePresetName) => {
        const presetConfig = getPresetConfig(preset);
        if (!presetConfig || activeButtonIds.length === 0) return;

        const newTriggerConfigs = [...triggerConfigs];
        activeButtonIds.forEach((id) => {
            newTriggerConfigs[id] = {
                topDeadzone: presetConfig.topDeadzone,
                bottomDeadzone: presetConfig.bottomDeadzone,
                pressAccuracy: presetConfig.pressAccuracy,
                releaseAccuracy: presetConfig.releaseAccuracy
            };
        });
        setTriggerConfigs(newTriggerConfigs);
        setNeedUpdate(true);
    };

    /**
     * 保存配置
     */
    const saveConfig = useCallback(() =>{
        const profileId = defaultProfile.id;
        const preserveAllFlag = defaultProfile.triggerConfigs?.isAllBtnsConfiguring ?? true;
        updateProfileDetails(profileId, {
            id: profileId,
            triggerConfigs: {
                isAllBtnsConfiguring: preserveAllFlag,
                triggerConfigs: triggerConfigs
            }
        });
    }, [defaultProfile, triggerConfigs]);

    return (
        <>
            <SettingMainContentLayout width={778}>
                <MainContentHeader 
                    title={t.SETTINGS_RAPID_TRIGGER_TITLE}
                    description={t.SETTINGS_RAPID_TRIGGER_HELPER_TEXT}
                />
                <MainContentBody>
                    <Fieldset.Root>
                        <Fieldset.Content>
                            <VStack gap={8} alignItems={"flex-start"}>
                                {/* 配置范围 */}
                                <VStack gap={2} alignItems={"flex-start"}>
                                    <Text fontSize={"sm"} opacity={0.75}>{t.SETTINGS_RAPID_TRIGGER_SCOPE_TITLE}</Text>
                                    <HStack align="stretch" gap={2}>
                                        <Button
                                            colorPalette={"green"}
                                            size={"sm"}
                                            variant={isScopeAllSelected ? "solid" : "outline"}
                                            onClick={() => applySelection(scopeAllIds)}
                                        >
                                            {t.SETTINGS_RAPID_TRIGGER_SCOPE_ALL}
                                        </Button>
                                        <Button
                                            colorPalette={"green"}
                                            size={"sm"}
                                            variant={isScopeDpadSelected ? "solid" : "outline"}
                                            onClick={() => applySelection(dpadMappedButtons)}
                                        >
                                            {t.SETTINGS_RAPID_TRIGGER_SCOPE_DPAD}
                                        </Button>
                                        <Button
                                            colorPalette={"green"}
                                            size={"sm"}
                                            variant={isScopeNonDpadSelected ? "solid" : "outline"}
                                            onClick={() => applySelection(scopeNonDpadIds)}
                                        >
                                            {t.SETTINGS_RAPID_TRIGGER_SCOPE_NON_DPAD}
                                        </Button>
                                    </HStack>
                                </VStack>

                                {/* 预设按钮（快捷设置） */}
                                <VStack gap={2} alignItems={"flex-start"}>
                                    <Text fontSize={"sm"} opacity={0.75}>{t.SETTING_BUTTON_PERFORMANCE_PRESET_TITLE}</Text>
                                    <HStack align="stretch" gap={2}>
                                        <Button
                                            colorPalette={"green"}
                                            size={"sm"}
                                            variant={matchedPreset === ButtonPerformancePresetName.FASTEST ? "solid" : "outline"}
                                            disabled={activeButtonIds.length === 0}
                                            onClick={() => applyPresetToSelection(ButtonPerformancePresetName.FASTEST)}
                                        >
                                            {t.SETTING_BUTTON_PERFORMANCE_PRESET_FASTEST_LABEL}
                                        </Button>
                                        <Button
                                            colorPalette={"green"}
                                            size={"sm"}
                                            variant={matchedPreset === ButtonPerformancePresetName.BALANCED ? "solid" : "outline"}
                                            disabled={activeButtonIds.length === 0}
                                            onClick={() => applyPresetToSelection(ButtonPerformancePresetName.BALANCED)}
                                        >
                                            {t.SETTING_BUTTON_PERFORMANCE_PRESET_BALANCED_LABEL}
                                        </Button>
                                        <Button
                                            colorPalette={"green"}
                                            size={"sm"}
                                            variant={matchedPreset === ButtonPerformancePresetName.STABILITY ? "solid" : "outline"}
                                            disabled={activeButtonIds.length === 0}
                                            onClick={() => applyPresetToSelection(ButtonPerformancePresetName.STABILITY)}
                                        >
                                            {t.SETTING_BUTTON_PERFORMANCE_PRESET_STABILITY_LABEL}
                                        </Button>
                                    </HStack>
                                </VStack>

                                {/* 提示文本 */}
                                <Text opacity={activeButtonIds.length > 0 ? "0.75" : "0.4"} fontSize={"sm"} >
                                    {activeButtonIds.length > 0
                                        ? `${t.SETTINGS_RAPID_TRIGGER_SCOPE_CUSTOM_SELECTED_PREFIX} ${activeButtonIds.length} ${t.SETTINGS_RAPID_TRIGGER_SCOPE_CUSTOM_SELECTED_SUFFIX}KEY-${activeButtonIds.map((id) => id + 1).join(", KEY-")}`
                                        : t.SETTINGS_RAPID_TRIGGER_SCOPE_CUSTOM_EMPTY}
                                </Text>

                                {/* 滑块 */}
                                {[
                                    { key: 'topDeadzone', label: t.SETTINGS_RAPID_TRIGGER_TOP_DEADZONE_LABEL, min: 0, max: 1, step: 0.1, decimalPlaces: 1 },
                                    { key: 'pressAccuracy', label: t.SETTINGS_RAPID_TRIGGER_PRESS_ACCURACY_LABEL, min: 0.1, max: 1, step: 0.1, decimalPlaces: 1 },
                                    { key: 'bottomDeadzone', label: t.SETTINGS_RAPID_TRIGGER_BOTTOM_DEADZONE_LABEL, min: 0, max: 1, step: 0.01, decimalPlaces: 2 },
                                    { key: 'releaseAccuracy', label: t.SETTINGS_RAPID_TRIGGER_RELEASE_ACCURACY_LABEL, min: 0.01, max: 1, step: 0.01, decimalPlaces: 2 },
                                ].map(({ key, label, min, max, step, decimalPlaces }) => {
                                    const unified = getUnifiedValue(key as keyof TriggerConfig);
                                    return (
                                    <Slider.Root
                                        size="sm"
                                        key={key}
                                        width="680px"
                                        min={0}
                                        max={max}
                                        step={step}
                                        colorPalette={unified.isUnified ? "green" : "gray"}
                                        disabled={activeButtonIds.length === 0}
                                        value={[unified.value]}
                                        onValueChange={(details) => {
                                            let v = 0;
                                            if (details.value[0] < min) {
                                                v = min;
                                            } else if (details.value[0] > max) {
                                                v = max;
                                            } else {
                                                v = details.value[0];
                                            }
                                            updateConfig(key as keyof TriggerConfig, v);
                                        }}
                                    >
                                        <HStack justifyContent={"space-between"} >
                                            <Slider.Label>
                                                <HStack gap={1}>
                                                    <Text as="span">{label}</Text>
                                                    {!unified.isUnified && <Text as="span">{t.SETTINGS_RAPID_TRIGGER_PARAM_NOT_UNIFIED}</Text>}
                                                </HStack>
                                            </Slider.Label>
                                            <HStack>
                                                <Slider.ValueText />
                                                <Text fontSize={"sm"} opacity={0.75} >(mm)</Text>
                                            </HStack>
                                        </HStack>
                                        <Slider.Control>
                                            <Slider.Track>
                                                <Slider.Range />
                                            </Slider.Track>
                                            <Slider.Thumb index={0} >
                                                <Slider.DraggingIndicator
                                                    layerStyle="fill.solid"
                                                    top="6"
                                                    rounded="sm"
                                                    px="1.5"
                                                >
                                                    <Slider.ValueText />
                                                </Slider.DraggingIndicator>
                                            </Slider.Thumb>
                                            <Slider.Marks marks={Array.from({ length: Math.floor(max / 0.2) + 1 }, (_, i) => ({
                                                value: i * 0.2,
                                                label: (i * 0.2).toFixed(decimalPlaces)
                                            }))} />
                                        </Slider.Control>
                                    </Slider.Root>
                                )})}

                                
                                <Box width={"120px"} >
                                    <Button 
                                        variant={"ghost"} 
                                        colorPalette={"green"} 
                                        size={"sm"} 
                                        onClick={() => setIsTableDialogOpen(true)}
                                    >
                                        <LuSheet />{t.BUTTON_PREVIEW_IN_TABLE_VIEW}
                                    </Button>
                                </Box>

                                {/* Buttons */}
                            </VStack>
                        </Fieldset.Content>
                    </Fieldset.Root>
                </MainContentBody>
            </SettingMainContentLayout>
            
            {/* Portal Dialog for Table View */}
            <Portal>
                <Dialog.Root open={isTableDialogOpen} onOpenChange={(details) => setIsTableDialogOpen(details.open)}>
                    <Dialog.Positioner>
                        <Dialog.Content maxWidth="650px">
                            <Dialog.Header>
                                <Dialog.Title fontSize="sm" opacity={0.75}>{t.SETTINGS_RAPID_TRIGGER_TITLE}</Dialog.Title>
                            </Dialog.Header>
                            <Dialog.Body>
                                <Table.Root fontSize="sm" colorPalette={"green"} interactive opacity={0.9}>
                                    <Table.Header fontSize="xs">
                                        <Table.Row>
                                            <Table.ColumnHeader width="12%" textAlign="center">Key</Table.ColumnHeader>
                                            <Table.ColumnHeader width="22%" textAlign="end">{t.SETTINGS_RAPID_TRIGGER_TOP_DEADZONE_LABEL}</Table.ColumnHeader>
                                            <Table.ColumnHeader width="22%" textAlign="end">{t.SETTINGS_RAPID_TRIGGER_BOTTOM_DEADZONE_LABEL}</Table.ColumnHeader>
                                            <Table.ColumnHeader width="22%" textAlign="end">{t.SETTINGS_RAPID_TRIGGER_PRESS_ACCURACY_LABEL}</Table.ColumnHeader>
                                            <Table.ColumnHeader width="22%" textAlign="end">{t.SETTINGS_RAPID_TRIGGER_RELEASE_ACCURACY_LABEL}</Table.ColumnHeader>
                                        </Table.Row>
                                    </Table.Header>
                                    <Table.Body>
                                        {tableViewConfig.map((config, index) => (
                                            <Table.Row key={index}>
                                                <Table.Cell textAlign="center">{index + 1}</Table.Cell>
                                                <Table.Cell textAlign="end">{config.topDeadzone}</Table.Cell>
                                                <Table.Cell textAlign="end">{config.bottomDeadzone}</Table.Cell>
                                                <Table.Cell textAlign="end">{config.pressAccuracy}</Table.Cell>
                                                <Table.Cell textAlign="end">{config.releaseAccuracy}</Table.Cell>
                                            </Table.Row>
                                        ))}
                                    </Table.Body>
                                </Table.Root>
                            </Dialog.Body>
                            <Dialog.CloseTrigger />
                        </Dialog.Content>
                    </Dialog.Positioner>
                </Dialog.Root>
            </Portal>
        </>
    );
} 
