"use client";

import {
    Flex,
    Center,
    Fieldset,
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
} from "@chakra-ui/react";
import { useEffect, useState, useRef } from "react";
import { ADCButtonDebounceAlgorithm, RAPID_TRIGGER_SETTINGS_INTERACTIVE_IDS, RapidTriggerConfig } from "@/types/gamepad-config";
import HitboxPerformance from "@/components/hitbox/hitbox-performance";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import { useLanguage } from "@/contexts/language-context";
import { ContentActionButtons } from "@/components/content-action-buttons";
import { LuSheet } from "react-icons/lu";
import { ProfileSelect } from "./profile-select";

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



export function ButtonsPerformanceContent() {

    const [isInit, setIsInit] = useState<boolean>(false);
    const [needUpdate, setNeedUpdate] = useState<boolean>(false);
    const { defaultProfile, updateProfileDetails, dataIsReady, sendPendingCommandImmediately } = useGamepadConfig();
    
    // 添加容器引用和宽度状态
    const containerRef = useRef<HTMLDivElement>(null);
    const [containerWidth, setContainerWidth] = useState<number>(0);
    
    const [ defaultProfileId, setDefaultProfileId ] = useState<string>(defaultProfile.id);
    const { t } = useLanguage();

    const [selectedButton, setSelectedButton] = useState<number | null>(0); // 当前选中的按钮
    const [triggerConfigs, setTriggerConfigs] = useState<RapidTriggerConfig[]>([]); // 按钮配置
    const [isAllBtnsConfiguring, setIsAllBtnsConfiguring] = useState<boolean>(true); // 是否同时配置所有按钮
    const [debounceAlgorithm, setDebounceAlgorithm] = useState<ADCButtonDebounceAlgorithm>(ADCButtonDebounceAlgorithm.NONE); // 防抖算法
    const [allBtnsConfig, setAllBtnsConfig] = useState<RapidTriggerConfig>({ ...defaultTriggerConfig });
    const [tableViewConfig, setTableViewConfig] = useState<RapidTriggerConfig[]>([]); // 表格视图配置

    // 所有按钮的键值
    const allKeys = RAPID_TRIGGER_SETTINGS_INTERACTIVE_IDS;

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

    /**
     * 加载触发配置
     */
    useEffect(() => {
        // Load trigger configs from gamepadConfig when it changes

        if(isInit && defaultProfileId === defaultProfile.id) {
            return;
        }

        if(dataIsReady && defaultProfile.triggerConfigs) {
            const triggerConfigs = { ...defaultProfile.triggerConfigs };
            setIsAllBtnsConfiguring(triggerConfigs.isAllBtnsConfiguring ?? false);
            setDebounceAlgorithm(triggerConfigs.debounceAlgorithm as ADCButtonDebounceAlgorithm ?? ADCButtonDebounceAlgorithm.NONE);
            setTriggerConfigs(allKeys.map(key => triggerConfigs.triggerConfigs?.[key] ?? defaultTriggerConfig));
            setAllBtnsConfig(triggerConfigs.triggerConfigs?.[0] ?? defaultTriggerConfig);
            setIsInit(true);
            setDefaultProfileId(defaultProfile.id);
        }

    }, [dataIsReady, defaultProfile]);

    useEffect(() => {
        if (isAllBtnsConfiguring) {
            setTableViewConfig(allKeys.map(() => allBtnsConfig));
        } else {
            setTableViewConfig(triggerConfigs);
        }
    }, [triggerConfigs, allBtnsConfig, isAllBtnsConfiguring]);

    // 监听容器宽度变化
    useEffect(() => {
        const updateWidth = () => {
            if (containerRef.current) {
                const width = containerRef.current.offsetWidth;
                console.log("buttons-performance containerWidth: ", width);
                setContainerWidth(width);
            }
        };

        // 初始获取宽度
        updateWidth();

        // 监听窗口大小变化
        const resizeObserver = new ResizeObserver(updateWidth);
        if (containerRef.current) {
            resizeObserver.observe(containerRef.current);
        }

        // 清理
        return () => {
            resizeObserver.disconnect();
        };
    }, []);


    /**
     * 获取当前配置
     */
    const getCurrentConfig = () => {
        if (selectedButton === null) return defaultTriggerConfig;
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
    };

    /**
     * 更新所有按钮配置
     */
    const updateAllBtnsConfig = (key: keyof RapidTriggerConfig, value: number) => {
        setAllBtnsConfig({
            ...allBtnsConfig,
            [key]: value
        });
    };

    /**
     * 切换所有按钮配置
     */
    const switchAllBtnsConfiging = (n: boolean) => {
        if (n === true) {
            setAllBtnsConfig({
                ...allBtnsConfig,
                ...getCurrentConfig()
            });
        }
        setIsAllBtnsConfiguring(n);
    }

    /**
     * 保存配置
     */
    const updateProfileDetailHandler = async (): Promise<void> => {
        const profileId = defaultProfile.id;

        if (isAllBtnsConfiguring) {
            const newTriggerConfigs: RapidTriggerConfig[] = [];
            allKeys.forEach((key, index) => {
                newTriggerConfigs[index] = allBtnsConfig;
            });

            return await updateProfileDetails(profileId, {
                id: profileId,
                triggerConfigs: {
                    isAllBtnsConfiguring: isAllBtnsConfiguring,
                    debounceAlgorithm: debounceAlgorithm as number,
                    triggerConfigs: newTriggerConfigs
                }
            });
        } else {
            return await updateProfileDetails(profileId, {
                id: profileId,
                triggerConfigs: {
                    isAllBtnsConfiguring: isAllBtnsConfiguring,
                    debounceAlgorithm: debounceAlgorithm as number,
                    triggerConfigs: triggerConfigs
                }
            });
        }
    };

    /**
     * 点击按钮
     */
    const handleButtonClick = (id: number) => {
        if (!isAllBtnsConfiguring && selectedButton !== id && id >= 0) {
            setSelectedButton(id);
        }
    };


    useEffect(() => {
        if(needUpdate) {
            updateProfileDetailHandler();
            setNeedUpdate(false);
        }
    }, [needUpdate]);

    useEffect(() => {
        return () => {
            try {
                sendPendingCommandImmediately('update_profile');
            } catch (error) {
                console.warn('页面关闭前发送 update_keys_config 命令失败:', error);
            }
        }
    }, [sendPendingCommandImmediately]);

    return (
        <>
            <Flex direction="row" width={"100%"} height={"100%"} padding={"18px"} >
                <Flex flex={0} justifyContent={"flex-start"} height="fit-content" >
                    <ProfileSelect />
                </Flex>
                <Center ref={containerRef} flex={1} justifyContent={"center"} flexDirection={"column"}>
                    <Center padding="80px 30px" position="relative" flex={1}  >
                        <HitboxPerformance
                            onClick={(id) => handleButtonClick(id)}
                            highlightIds={!isAllBtnsConfiguring ? [selectedButton ?? -1] : allKeys}
                            interactiveIds={allKeys}
                            containerWidth={containerWidth}
                        />
                    </Center>
                    <Center flex={0}  >
                        <ContentActionButtons />
                    </Center>
                </Center>
                <Center>
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

                                        <RadioCard.Root variant={"subtle"} pb={4} value={debounceAlgorithm.toString()} colorPalette={"green"} onValueChange={(details) => {
                                            if (details.value !== null) {
                                                const parsedValue = parseInt(details.value);
                                                setDebounceAlgorithm(parsedValue as ADCButtonDebounceAlgorithm);
                                                setNeedUpdate(true);
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

                                        {/* Sliders */}
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
                                                value={[isAllBtnsConfiguring ? allBtnsConfig[key as keyof RapidTriggerConfig] : getCurrentConfig()[key as keyof TriggerConfig]]}
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
                                                        setNeedUpdate(true);
                                                    } else {
                                                        updateConfig(key as keyof TriggerConfig, v);
                                                        setNeedUpdate(true);
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



                                        <Dialog.Root lazyMount placement={"center"} unmountOnExit={true} >
                                            <Dialog.Trigger asChild >
                                                <Box width={"120px"} >
                                                    <Button variant={"ghost"} colorPalette={"green"} size={"sm"} ><LuSheet />{t.BUTTON_PREVIEW_IN_TABLE_VIEW}</Button>
                                                </Box>
                                            </Dialog.Trigger>
                                            <Dialog.Content maxWidth="650px" >
                                                <Dialog.Header>
                                                    <Dialog.Title fontSize="sm" opacity={0.75} >{t.SETTINGS_RAPID_TRIGGER_TITLE}</Dialog.Title>
                                                </Dialog.Header>
                                                <Dialog.Body >
                                                    <Table.Root fontSize="sm" colorPalette={"green"} interactive opacity={0.9} >
                                                        <Table.Header fontSize="xs" >
                                                            <Table.Row>
                                                                <Table.ColumnHeader width="12%" textAlign="center" >Key</Table.ColumnHeader>
                                                                <Table.ColumnHeader width="22%" textAlign="end" >{t.SETTINGS_RAPID_TRIGGER_TOP_DEADZONE_LABEL}</Table.ColumnHeader>
                                                                <Table.ColumnHeader width="22%" textAlign="end" >{t.SETTINGS_RAPID_TRIGGER_BOTTOM_DEADZONE_LABEL}</Table.ColumnHeader>
                                                                <Table.ColumnHeader width="22%" textAlign="end" >{t.SETTINGS_RAPID_TRIGGER_PRESS_ACCURACY_LABEL}</Table.ColumnHeader>
                                                                <Table.ColumnHeader width="22%" textAlign="end" >{t.SETTINGS_RAPID_TRIGGER_RELEASE_ACCURACY_LABEL}</Table.ColumnHeader>
                                                            </Table.Row>
                                                        </Table.Header>
                                                        <Table.Body>
                                                            {tableViewConfig.map((config, index) => (
                                                                <Table.Row key={index}>
                                                                    <Table.Cell textAlign="center" >{index + 1}</Table.Cell>
                                                                    <Table.Cell textAlign="end" >{config.topDeadzone}</Table.Cell>
                                                                    <Table.Cell textAlign="end" >{config.bottomDeadzone}</Table.Cell>
                                                                    <Table.Cell textAlign="end" >{config.pressAccuracy}</Table.Cell>
                                                                    <Table.Cell textAlign="end" >{config.releaseAccuracy}</Table.Cell>
                                                                </Table.Row>
                                                            ))}
                                                        </Table.Body>
                                                    </Table.Root>
                                                </Dialog.Body>
                                                <Dialog.CloseTrigger />
                                            </Dialog.Content>
                                        </Dialog.Root>

                                        {/* Buttons */}

                                    </VStack>
                                </Fieldset.Content>

                            </Fieldset.Root>
                        </Card.Body>

                    </Card.Root>
                </Center>
            </Flex>
        </>

    );
} 