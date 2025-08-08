"use client";

import {
    Text,
    Button,
    Box,
    Table,
    Card,
    VStack,
    HStack,
    Switch,
    RadioCard,
    Slider,
    Dialog,
    Fieldset,
    Portal,
} from "@chakra-ui/react";
import { useCallback, useEffect, useMemo, useState } from "react";
import { ADCButtonDebounceAlgorithm, RapidTriggerConfig, RAPID_TRIGGER_SETTINGS_INTERACTIVE_IDS } from "@/types/gamepad-config";
import { useLanguage } from "@/contexts/language-context";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import { LuSheet } from "react-icons/lu";
import { openConfirm } from "./dialog-confirm";

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
    selectedButton: number;
    selectAllButtonHandler: (result: boolean) => void;
}

export function ButtonsPerformanceSettingContent({
    selectedButton,
    selectAllButtonHandler,
}: ButtonsPerformanceSettingContentProps) {
    const { t } = useLanguage();
    const { defaultProfile, updateProfileDetails } = useGamepadConfig();
    const [isInit, setIsInit] = useState<boolean>(false);
    const [debounceAlgorithm, setDebounceAlgorithm] = useState<ADCButtonDebounceAlgorithm>(defaultProfile.triggerConfigs?.debounceAlgorithm as ADCButtonDebounceAlgorithm ?? ADCButtonDebounceAlgorithm.NONE);
    const [isAllBtnsConfiguring, setIsAllBtnsConfiguring] = useState<boolean>(defaultProfile.triggerConfigs?.isAllBtnsConfiguring ?? true);
    const [triggerConfigs, setTriggerConfigs] = useState<RapidTriggerConfig[]>([]);
    const [needUpdate, setNeedUpdate] = useState<boolean>(false);
    const [isTableDialogOpen, setIsTableDialogOpen] = useState<boolean>(false);
    const { sendPendingCommandImmediately } = useGamepadConfig();
    const allKeys = RAPID_TRIGGER_SETTINGS_INTERACTIVE_IDS;

    const tableViewConfig = useMemo(() => triggerConfigs, [triggerConfigs]);

    const debounceAlgorithmLabelMap = new Map<ADCButtonDebounceAlgorithm, { label: string, description: string }>([
        [ADCButtonDebounceAlgorithm.NONE, {
            label: t.SETTINGS_ADC_BUTTON_DEBOUNCE_LABEL_NONE,
            description: t.SETTINGS_ADC_BUTTON_DEBOUNCE_LABEL_NONE_DESC
        }],
        [ADCButtonDebounceAlgorithm.NORMAL, {
            label: t.SETTINGS_ADC_BUTTON_DEBOUNCE_LABEL_NORMAL,
            description: t.SETTINGS_ADC_BUTTON_DEBOUNCE_LABEL_NORMAL_DESC
        }],
        [ADCButtonDebounceAlgorithm.MAX, {
            label: t.SETTINGS_ADC_BUTTON_DEBOUNCE_LABEL_MAX,
            description: t.SETTINGS_ADC_BUTTON_DEBOUNCE_LABEL_MAX_DESC
        }]
    ]);

    const ADCButtonDebounceAlgorithmLabelList = Array.from(debounceAlgorithmLabelMap.entries()).map(([value, config]) => ({
        value,
        label: config.label,
        description: config.description
    }));

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
            setIsAllBtnsConfiguring(triggerConfigs.isAllBtnsConfiguring ?? false);
            setDebounceAlgorithm(triggerConfigs.debounceAlgorithm as ADCButtonDebounceAlgorithm ?? ADCButtonDebounceAlgorithm.NONE);
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
    const getCurrentConfig = () => {
        return triggerConfigs[selectedButton] ?? defaultTriggerConfig;
    };

    /**
     * 更新当前配置
     */
    const updateConfig = (key: keyof TriggerConfig, value: number) => {
        if (selectedButton === null) return;

        const newTriggerConfigs = [...triggerConfigs];
        newTriggerConfigs[selectedButton] = {
            ...getCurrentConfig(),
            [key]: value
        };

        setTriggerConfigs(newTriggerConfigs);
        setNeedUpdate(true);
    };

    /**
     * 更新所有按钮配置
     */
    const updateAllBtnsConfig = (key: keyof RapidTriggerConfig, value: number) => {
        const newTriggerConfigs = [...triggerConfigs];
        newTriggerConfigs.forEach(config => {
            config[key] = value;
        });
        setTriggerConfigs(newTriggerConfigs);
        setNeedUpdate(true);
    };

    /**`
     * 切换所有按钮配置
     */
    const switchAllBtnsConfiging =  async (n: boolean) => {
        if(n == true) {
            const result = await openConfirm({
                title: t.SETTINGS_RAPID_TRIGGER_CONFIGURE_ALL_TITLE,
                message: t.SETTINGS_RAPID_TRIGGER_CONFIGURE_ALL_MESSAGE,
            });

            if(result) {
                setIsAllBtnsConfiguring(n);
                setTriggerConfigs(allKeys.map(() => getCurrentConfig())); // 用当前选中的配置 填充所有按键配置
                selectAllButtonHandler(true);
                setNeedUpdate(true);
            }
        } else {
            setIsAllBtnsConfiguring(n);
            selectAllButtonHandler(false);
            setNeedUpdate(true);
        }
    };

    /**
     * 保存配置
     */
    const saveConfig = useCallback(() =>{
        const profileId = defaultProfile.id;
        updateProfileDetails(profileId, {
            id: profileId,
            triggerConfigs: {
                isAllBtnsConfiguring: isAllBtnsConfiguring,
                debounceAlgorithm: debounceAlgorithm as number,
                triggerConfigs: triggerConfigs
            }
        });
    }, [isAllBtnsConfiguring, defaultProfile, debounceAlgorithm, triggerConfigs]);

    /**
     * 处理防抖算法变化
     */
    const handleDebounceAlgorithmChange = (algorithm: ADCButtonDebounceAlgorithm) => {
        setDebounceAlgorithm(algorithm);
        setNeedUpdate(true);
    };

    return (
        <>
            <Card.Root w="778px" h="100%" >
                <Card.Header>
                    <Card.Title fontSize={"md"}  >
                        <Text fontSize={"32px"} fontWeight={"normal"} color={"green.600"} >{t.SETTINGS_RAPID_TRIGGER_TITLE}</Text>
                    </Card.Title>
                    <Card.Description fontSize={"sm"} pt={4} pb={4} whiteSpace="pre-wrap"  >
                        {t.SETTINGS_RAPID_TRIGGER_HELPER_TEXT}
                    </Card.Description>
                </Card.Header>
                <Card.Body>
                    <Fieldset.Root>
                        <Fieldset.Content  >
                            <VStack gap={8} alignItems={"flex-start"} >
                                {/* 防抖算法设置 */}
                                <RadioCard.Root variant={"subtle"} pb={4} value={debounceAlgorithm.toString()} colorPalette={"green"} onValueChange={(details) => {
                                    if (details.value !== null) {
                                        const parsedValue = parseInt(details.value);
                                        handleDebounceAlgorithmChange(parsedValue as ADCButtonDebounceAlgorithm);
                                    }
                                }} >
                                    <RadioCard.Label>{t.SETTINGS_ADC_BUTTON_DEBOUNCE_TITLE}</RadioCard.Label>
                                    <HStack align="stretch">
                                        {ADCButtonDebounceAlgorithmLabelList.map((item, index) => (
                                            <RadioCard.Item key={index} value={item.value.toString()} w="230px" >
                                                <RadioCard.ItemHiddenInput />
                                                <RadioCard.ItemControl>
                                                    <RadioCard.ItemContent>
                                                        <RadioCard.ItemText>{item.label}</RadioCard.ItemText>
                                                        <RadioCard.ItemDescription>
                                                            {item.description}
                                                        </RadioCard.ItemDescription>
                                                    </RadioCard.ItemContent>
                                                    <RadioCard.ItemIndicator />
                                                </RadioCard.ItemControl>
                                            </RadioCard.Item>
                                        ))}
                                    </HStack>
                                </RadioCard.Root>
                                

                                {/* 是否同时设置全部按键 */}
                                <Switch.Root colorPalette={"green"} checked={isAllBtnsConfiguring}
                                    onCheckedChange={() => {
                                        switchAllBtnsConfiging(!isAllBtnsConfiguring);
                                    }}
                                >
                                    <Switch.HiddenInput />
                                    <Switch.Control>
                                        <Switch.Thumb />
                                    </Switch.Control>
                                    <Switch.Label>
                                        <Text fontSize={"sm"} opacity={0.75} >{t.SETTINGS_RAPID_TRIGGER_CONFIGURE_ALL}</Text>
                                    </Switch.Label>
                                </Switch.Root>

                                {/* 提示文本 */}
                                <Text opacity={!isAllBtnsConfiguring ? "0.75" : "0.25"} fontSize={"sm"} >
                                    {(selectedButton !== null && !isAllBtnsConfiguring) ?
                                        t.SETTINGS_RAPID_TRIGGER_ONFIGURING_BUTTON
                                        : t.SETTINGS_RAPID_TRIGGER_SELECT_A_BUTTON_TO_CONFIGURE
                                    }
                                    {(selectedButton !== null && !isAllBtnsConfiguring) && (
                                        <Text as="span" fontWeight="bold">
                                            KEY-{(selectedButton ?? 0) + 1}
                                        </Text>
                                    )}
                                </Text>

                                {/* 滑块 */}
                                {[
                                    { key: 'topDeadzone', label: t.SETTINGS_RAPID_TRIGGER_TOP_DEADZONE_LABEL, min: 0, max: 1, step: 0.1, decimalPlaces: 1 },
                                    { key: 'pressAccuracy', label: t.SETTINGS_RAPID_TRIGGER_PRESS_ACCURACY_LABEL, min: 0.1, max: 1, step: 0.1, decimalPlaces: 1 },
                                    { key: 'bottomDeadzone', label: t.SETTINGS_RAPID_TRIGGER_BOTTOM_DEADZONE_LABEL, min: 0, max: 1, step: 0.01, decimalPlaces: 2 },
                                    { key: 'releaseAccuracy', label: t.SETTINGS_RAPID_TRIGGER_RELEASE_ACCURACY_LABEL, min: 0.01, max: 1, step: 0.01, decimalPlaces: 2 },
                                ].map(({ key, label, min, max, step, decimalPlaces }) => (
                                    <Slider.Root
                                        size="sm"
                                        key={key}
                                        width="680px"
                                        min={0}
                                        max={max}
                                        step={step}
                                        colorPalette={"green"}
                                        disabled={selectedButton === null && !isAllBtnsConfiguring}
                                        value={[getCurrentConfig()[key as keyof TriggerConfig]]}
                                        onValueChange={(details) => {
                                            let v = 0;
                                            if (details.value[0] < min) {
                                                v = min;
                                            } else if (details.value[0] > max) {
                                                v = max;
                                            } else {
                                                v = details.value[0];
                                            }
                                            if (isAllBtnsConfiguring) {
                                                updateAllBtnsConfig(key as keyof RapidTriggerConfig, v);
                                            } else {
                                                updateConfig(key as keyof TriggerConfig, v);
                                            }
                                        }}
                                    >
                                        <HStack justifyContent={"space-between"} >
                                            <Slider.Label>{label}</Slider.Label>
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
                                ))}

                                
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
                </Card.Body>
            </Card.Root>
            
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