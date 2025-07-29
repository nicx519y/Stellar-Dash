"use client";

import {
    Flex,
    Center,
    Stack,
    Fieldset,
    SimpleGrid,
    Button,
    HStack,
    RadioCardLabel,
    Text,
    Card,
    VStack,
    Switch,
    Box,
} from "@chakra-ui/react";
import KeymappingFieldset from "@/components/keymapping-fieldset";
import { useEffect, useMemo, useState, useRef } from "react";
import {
    RadioCardItem,
    RadioCardRoot,
} from "@/components/ui/radio-card"
import {
    GameProfile,
    GameSocdMode,
    GameSocdModeLabelMap,
    GameControllerButton,
    KEYS_SETTINGS_INTERACTIVE_IDS,
    Platform,
} from "@/types/gamepad-config";
import { SegmentedControl } from "@/components/ui/segmented-control";
import HitboxKeys from "@/components/hitbox/hitbox-keys";
import HitboxEnableSetting from "@/components/hitbox/hitbox-enableSetting";
import { MdCleaningServices } from "react-icons/md";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import { useLanguage } from "@/contexts/language-context";
import { ContentActionButtons } from "@/components/content-action-buttons";
import { useColorMode } from "./ui/color-mode";
import { ProfileSelect } from "@/components/profile-select";

export function KeysSettingContent() {
    const {
        defaultProfile,
        updateProfileDetails,
        globalConfig,
        dataIsReady,
        sendPendingCommandImmediately,
    } = useGamepadConfig();
    const { t } = useLanguage();
    const { colorMode } = useColorMode();

    const [isInit, setIsInit] = useState<boolean>(false);
    const [needUpdate, setNeedUpdate] = useState<boolean>(false);
    
    // 添加容器引用和宽度状态
    const containerRef = useRef<HTMLDivElement>(null);
    const [containerWidth, setContainerWidth] = useState<number>(0);
    
    // 按键映射状态
    const keyLength = useMemo(() => Object.keys(defaultProfile.keysConfig?.keyMapping ?? {}).length, [defaultProfile?.keysConfig?.keyMapping]);
    const [defaultProfileId, setDefaultProfileId] = useState<string>(defaultProfile.id);
    const [socdMode, setSocdMode] = useState<GameSocdMode>(GameSocdMode.SOCD_MODE_UP_PRIORITY);
    const [invertXAxis, setInvertXAxis] = useState<boolean>(defaultProfile?.keysConfig?.invertXAxis ?? false);
    const [invertYAxis, setInvertYAxis] = useState<boolean>(defaultProfile?.keysConfig?.invertYAxis ?? false);
    const [fourWayMode, setFourWayMode] = useState<boolean>(defaultProfile?.keysConfig?.fourWayMode ?? false);
    const [keyMapping, setKeyMapping] = useState<{ [key in GameControllerButton]?: number[] }>(defaultProfile?.keysConfig?.keyMapping ?? {});
    const [keysEnableConfig, setKeysEnableConfig] = useState<boolean[]>(defaultProfile?.keysConfig?.keysEnableTag?.slice(0, keyLength - 1) ?? []); // 按键启用配置

    const [inputKey, setInputKey] = useState<number>(-1);
    const [keysEnableSettingActive, setKeysEnableSettingActive] = useState<boolean>(false); // 按键启用/禁用设置状态
    const [autoSwitch, setAutoSwitch] = useState<boolean>(true);

    

    useEffect(() => {
        if(isInit && defaultProfileId === defaultProfile.id) {
            return;
        }

        if(dataIsReady && defaultProfile.keysConfig) {
            setSocdMode(defaultProfile.keysConfig?.socdMode ?? GameSocdMode.SOCD_MODE_UP_PRIORITY);
            setInvertXAxis(defaultProfile.keysConfig?.invertXAxis ?? false);
            setInvertYAxis(defaultProfile.keysConfig?.invertYAxis ?? false);
            setFourWayMode(defaultProfile.keysConfig?.fourWayMode ?? false);
            setKeyMapping(Object.assign({}, defaultProfile.keysConfig?.keyMapping ?? {}));
            // 初始化按键启用配置，默认所有按键都启用
            const enableConfig = defaultProfile.keysConfig?.keysEnableTag?.slice(0, keyLength - 1) ?? Array(keyLength).fill(true);
            setKeysEnableConfig(enableConfig);

            setIsInit(true);
            setDefaultProfileId(defaultProfile.id);
        }
    }, [dataIsReady, defaultProfile]);

    const updateKeysConfigHandler = () => {

        const newConfig = Object.assign({ invertXAxis, invertYAxis, fourWayMode, socdMode, keyMapping, keysEnableTag: keysEnableConfig });

        const newProfile: GameProfile = {
            id: defaultProfile.id,
            keysConfig: newConfig,
        }
        updateProfileDetails(defaultProfile.id, newProfile);

    };

    const disabledKeys = useMemo(() => keysEnableConfig.map((_, index) => index).filter((_, index) => !keysEnableConfig[index]), [keysEnableConfig]);

    const hitboxButtonClick = (keyId: number) => {
        setInputKey(keyId);
    }

    const hitboxEnableSettingClick = (keyId: number) => {
        if (keysEnableSettingActive && keyId >= 0 && keyId < keysEnableConfig.length) {
            const newConfig = [...keysEnableConfig];
            newConfig[keyId] = !newConfig[keyId]; // 切换按键启用状态
            setKeysEnableConfig(newConfig);
            setNeedUpdate(true);
        }
    }

    const clearKeyMappingHandler = () => {
        const clearMapping = Object.fromEntries(Object.keys(keyMapping).map(key => [key, []]));
        setKeyMapping(clearMapping);
        setNeedUpdate(true);
    }

    useEffect(() => {
        if(needUpdate) {
            updateKeysConfigHandler();
            setNeedUpdate(false);
        }
    }, [needUpdate]);

    useEffect(() => {
        return () => {
            try {
                console.log("KeysSettingContent unmount");
                sendPendingCommandImmediately('update_profile');
            } catch (error) {
                console.warn('页面关闭前发送 update_keys_config 命令失败:', error);
            }
        }
    }, [sendPendingCommandImmediately]);

    // 监听容器宽度变化
    useEffect(() => {
        const updateWidth = () => {
            if (containerRef.current) {
                const width = containerRef.current.offsetWidth;
                console.log("keys-setting containerWidth: ", width);
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

    // 计算缩放比例
    const calculateScale = (): number => {
        if (!containerWidth) return 1;
        
        const hitboxWidth = 829; // StyledSvg的原始宽度
        const margin = 80; // 左右边距
        const availableWidth = containerWidth - (margin * 2);
        
        if (availableWidth <= 0) return 0.1; // 最小缩放比例
        
        const scale = availableWidth / hitboxWidth;
        return Math.min(scale, 1.3); // 最大不超过1.3，避免过度放大
    };

    const scale = calculateScale();

    // 计算动态位置偏移
    const calculateDynamicOffset = (): number => {
        const baseOffset = 280; // 原始偏移量
        const margin = 80; // 保持80px边距
        return (baseOffset * scale) + margin;
    };

    const dynamicOffset = calculateDynamicOffset();

    return (
        <Flex direction="row" width={"100%"} height={"100%"} padding={"18px"} >
            <Flex flex={0} justifyContent={"flex-start"} height="fit-content" >
                <ProfileSelect disabled={keysEnableSettingActive} />
            </Flex>
            <Center ref={containerRef} flex={1} justifyContent={"center"} flexDirection={"column"}  >
                <Center padding="80px 30px" position="relative" flex={1}   >
                    <Box position="absolute" top="50%" left="50%" transform={`translateX(-50%) translateY(-${dynamicOffset}px)`} zIndex={10} >
                        <Card.Root w="100%" h="100%" >
                            <Card.Body p="10px" >
                                <Button w="240px" size="xs" variant="solid" colorPalette={keysEnableSettingActive ? "blue" : "green"} onClick={() => setKeysEnableSettingActive(!keysEnableSettingActive)} >
                                    {keysEnableSettingActive ? t.KEYS_ENABLE_STOP_BUTTON_LABEL : t.KEYS_ENABLE_START_BUTTON_LABEL}
                                </Button>
                            </Card.Body>
                        </Card.Root>
                    </Box>
                    {keysEnableSettingActive ? (
                        <HitboxEnableSetting
                            onClick={hitboxEnableSettingClick}
                            interactiveIds={keysEnableConfig.map((_, index) => index)}
                            buttonsEnableConfig={keysEnableConfig}
                            containerWidth={containerWidth}
                        />
                    ) : (
                        <HitboxKeys
                            onClick={hitboxButtonClick}
                            interactiveIds={KEYS_SETTINGS_INTERACTIVE_IDS}
                            disabledKeys={disabledKeys}
                            isButtonMonitoringEnabled={!keysEnableSettingActive}
                            containerWidth={containerWidth}
                        />
                    )}
                </Center>
                <Center flex={0}  >
                    <ContentActionButtons
                        disabled={keysEnableSettingActive}
                    />
                </Center>
            </Center>
            <Center>
                <Card.Root w="778px" h="100%" >
                    <Card.Header>
                        <Card.Title fontSize={"md"}  >
                            <Text fontSize={"32px"} fontWeight={"normal"} color={"green.600"} >{t.SETTINGS_KEYS_TITLE}</Text>
                        </Card.Title>
                        <Card.Description fontSize={"sm"} pt={4} pb={4} whiteSpace="pre-wrap"  >
                            {t.SETTINGS_KEYS_HELPER_TEXT}
                        </Card.Description>
                    </Card.Header>
                    <Card.Body>
                        <Fieldset.Root  >
                            <Fieldset.Content >
                                <VStack gap={8} alignItems={"flex-start"} >
                                    {/* Key Mapping */}
                                    <Stack direction={"column"}>
                                        <Fieldset.Legend fontSize={"sm"} color={keysEnableSettingActive ? "gray.500" :  colorMode === "dark" ? "white" : "black"} >{t.SETTINGS_KEYS_MAPPING_TITLE}</Fieldset.Legend>
                                        <HStack gap={4} >
                                            <SegmentedControl
                                                size={"sm"}
                                                defaultValue={autoSwitch ? t.SETTINGS_KEY_MAPPING_AUTO_SWITCH_LABEL : t.SETTINGS_KEY_MAPPING_MANUAL_SWITCH_LABEL}
                                                items={[t.SETTINGS_KEY_MAPPING_AUTO_SWITCH_LABEL, t.SETTINGS_KEY_MAPPING_MANUAL_SWITCH_LABEL]}
                                                onValueChange={(detail) => setAutoSwitch(detail.value === t.SETTINGS_KEY_MAPPING_AUTO_SWITCH_LABEL)}
                                                disabled={keysEnableSettingActive}
                                            />
                                            <Button
                                                w="150px"
                                                size="xs"
                                                variant={"solid"}
                                                colorPalette={"red"}
                                                disabled={keysEnableSettingActive}
                                                onClick={clearKeyMappingHandler}
                                            >
                                                <MdCleaningServices />
                                                {t.SETTINGS_KEY_MAPPING_CLEAR_MAPPING_BUTTON}
                                            </Button>
                                        </HStack>

                                        <KeymappingFieldset
                                            autoSwitch={autoSwitch}
                                            inputKey={inputKey}
                                            inputMode={globalConfig.inputMode ?? Platform.XINPUT}
                                            keyMapping={keyMapping}
                                            changeKeyMappingHandler={(map) => { 
                                                setKeyMapping(map);
                                                setNeedUpdate(true);
                                            }}
                                            disabled={keysEnableSettingActive}
                                        />
                                    </Stack>

                                    {/* SOCD Mode Choice */}
                                    <RadioCardRoot
                                        colorPalette={"green"}
                                        size={"sm"}
                                        variant={ colorMode === "dark" ? "subtle" : "solid"}
                                        value={socdMode?.toString() ?? GameSocdMode.SOCD_MODE_UP_PRIORITY.toString()}
                                        onValueChange={(detail) => {
                                            const socd = parseInt(detail.value ?? GameSocdMode.SOCD_MODE_NEUTRAL.toString()) as GameSocdMode;
                                            setSocdMode(socd);
                                            setNeedUpdate(true);
                                        }}
                                        disabled={keysEnableSettingActive}
                                    >
                                        <RadioCardLabel>{t.SETTINGS_KEYS_SOCD_MODE_TITLE}</RadioCardLabel>
                                        <SimpleGrid gap={1} columns={5} >
                                            {Array.from({ length: GameSocdMode.SOCD_MODE_NUM_MODES }, (_, index) => (
                                                <RadioCardItem
                                                    fontSize={"xs"}
                                                    indicator={false}
                                                    key={index}
                                                    value={index.toString()}
                                                    label={GameSocdModeLabelMap.get(index as GameSocdMode)?.label ?? ""}

                                                />
                                            ))}
                                        </SimpleGrid>
                                    </RadioCardRoot>

                                    {/* Invert Axis Choice & Invert Y Axis Choice & FourWay Mode Choice */}
                                    <HStack gap={5}>
                                        <Switch.Root
                                            colorPalette={"green"}
                                            checked={invertXAxis}
                                            onCheckedChange={() => {
                                                setInvertXAxis(!invertXAxis);
                                                setNeedUpdate(true);
                                            }}
                                            disabled={keysEnableSettingActive}
                                        >
                                            <Switch.HiddenInput />
                                            <Switch.Control>
                                                <Switch.Thumb />
                                            </Switch.Control>
                                            <Switch.Label>{t.SETTINGS_KEYS_INVERT_X_AXIS}</Switch.Label>
                                        </Switch.Root>

                                        <Switch.Root
                                            colorPalette={"green"}
                                            checked={invertYAxis}
                                            onCheckedChange={() => {
                                                setInvertYAxis(!invertYAxis);
                                                setNeedUpdate(true);
                                            }}
                                            disabled={keysEnableSettingActive}
                                        >
                                            <Switch.HiddenInput />
                                            <Switch.Control>
                                                <Switch.Thumb />
                                            </Switch.Control>
                                            <Switch.Label>{t.SETTINGS_KEYS_INVERT_Y_AXIS}</Switch.Label>
                                        </Switch.Root>
                                    </HStack>
                                </VStack>
                            </Fieldset.Content>
                        </Fieldset.Root>
                    </Card.Body>
                </Card.Root>
            </Center>
        </Flex>
    )
}
