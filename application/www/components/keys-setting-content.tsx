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
import { useEffect, useMemo, useState } from "react";
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
import { LuInfo } from "react-icons/lu";
import { ToggleTip } from "@/components/ui/toggle-tip"
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import useUnsavedChangesWarning from "@/hooks/use-unsaved-changes-warning";
import { useLanguage } from "@/contexts/language-context";
import { ContentActionButtons } from "@/components/content-action-buttons";
import { useColorMode } from "./ui/color-mode";
import { ProfileSelect } from "@/components/profile-select";
import { useButtonMonitor } from "@/hooks/use-button-monitor";
import type { ButtonEvent } from "@/components/button-monitor-manager";

export function KeysSettingContent() {
    const {
        defaultProfile,
        updateProfileDetails,
        fetchDefaultProfile,
        globalConfig,
    } = useGamepadConfig();
    const [_isDirty, setIsDirty] = useUnsavedChangesWarning();
    const { t } = useLanguage();
    const { colorMode } = useColorMode();
    
    // 按键映射状态
    const [socdMode, setSocdMode] = useState<GameSocdMode>(GameSocdMode.SOCD_MODE_UP_PRIORITY);
    const [invertXAxis, setInvertXAxis] = useState<boolean>(false);
    const [invertYAxis, setInvertYAxis] = useState<boolean>(false);
    const [fourWayMode, setFourWayMode] = useState<boolean>(false);
    const [keyMapping, setKeyMapping] = useState<{ [key in GameControllerButton]?: number[] }>({});
    const [autoSwitch, setAutoSwitch] = useState<boolean>(true);
    const [inputKey, setInputKey] = useState<number>(-1);
    const [keysEnableSettingActive, setKeysEnableSettingActive] = useState<boolean>(false); // 按键启用/禁用设置状态
    const [keysEnableConfig, setKeysEnableConfig] = useState<boolean[]>([]); // 按键启用配置

    const disabledKeys = useMemo(() => {
        const keys = []
        for (let i = 0; i < keysEnableConfig.length; i++) {
            if (!keysEnableConfig[i]) {
                keys.push(i);
            }
        }
        return keys;
    }, [keysEnableConfig]);

    // 使用新的按键监控 hook
    const buttonMonitor = useButtonMonitor({
        pollingInterval: 500,
        onError: (error) => {
            console.error('按键监控错误:', error);
        },
    });

    useEffect(() => {
        if (defaultProfile.keysConfig) {
            setSocdMode(defaultProfile.keysConfig?.socdMode ?? GameSocdMode.SOCD_MODE_UP_PRIORITY);
            setInvertXAxis(defaultProfile.keysConfig?.invertXAxis ?? false);
            setInvertYAxis(defaultProfile.keysConfig?.invertYAxis ?? false);
            setFourWayMode(defaultProfile.keysConfig?.fourWayMode ?? false);
            setKeyMapping(defaultProfile.keysConfig?.keyMapping ?? {});
            // 初始化按键启用配置，默认所有按键都启用
            const enableConfig = defaultProfile.keysConfig?.keysEnableTag ?? Array(20).fill(true);
            setKeysEnableConfig(enableConfig);
            setIsDirty?.(false); // reset the unsaved changes warning 
        }
    }, [defaultProfile, setIsDirty]);

    // 处理监控状态切换
    useEffect(() => {
        buttonMonitor.startMonitoring();
        return () => {
            buttonMonitor.stopMonitoring();
        };
    }, []);

    // 监听设备按键事件
    useEffect(() => {
        const handleDeviceButtonEvent = (event: CustomEvent<ButtonEvent>) => {
            const buttonEvent = event.detail;
            
            console.log('Received device button event:', {
                type: buttonEvent.type,
                buttonIndex: buttonEvent.buttonIndex,
                timestamp: Date.now()
            });
            
            // 只处理按键按下事件，并且只在启用监控时
            if (buttonEvent.type === 'button-press') {
                // 设置输入按键，触发 KeymappingFieldset 的处理
                setInputKey(buttonEvent.buttonIndex);
            } else if (buttonEvent.type === 'button-release') {
                setInputKey(-1);
            }
        };
        
        // 添加事件监听器
        window.addEventListener('device-button-event', handleDeviceButtonEvent as EventListener);

        // 清理函数
        return () => {
            window.removeEventListener('device-button-event', handleDeviceButtonEvent as EventListener);
        };
    }, []);

    
    /**
     * set key mapping
     * @param key - game controller button
     * @param hitboxButtons - hitbox buttons
     */
    const setHitboxButtons = (key: string, hitboxButtons: number[]) => {
        setKeyMapping({
            ...keyMapping,
            [key as GameControllerButton]: hitboxButtons,
        });
    }

    const hitboxButtonClick = (keyId: number) => {
        setInputKey(keyId);
    }

    const hitboxEnableSettingClick = (keyId: number) => {
        if (keysEnableSettingActive && keyId >= 0 && keyId < keysEnableConfig.length) {
            const newConfig = [...keysEnableConfig];
            newConfig[keyId] = !newConfig[keyId]; // 切换按键启用状态
            setKeysEnableConfig(newConfig);
            setIsDirty?.(true);
        }
    }


    const saveProfileDetailHandler = (): Promise<void> => {
        const newProfile: GameProfile = {
            id: defaultProfile.id,
            keysConfig: {
                invertXAxis: invertXAxis,
                invertYAxis: invertYAxis,
                fourWayMode: fourWayMode,
                socdMode: socdMode,
                keyMapping: keyMapping,
                keysEnableTag: keysEnableConfig,
            },
        }

        return updateProfileDetails(defaultProfile.id, newProfile);
    }

    const clearKeyMappingHandler = () => {
        setKeyMapping({});
        setIsDirty?.(true);
    }

    return (
        <Flex direction="row" width={"100%"} height={"100%"} padding={"18px"} >
            <Center >
                <ProfileSelect disabled={keysEnableSettingActive} />
            </Center>
            <Center flex={1}  >
                <Center padding="80px 30px" position="relative"   >
                    <Box position="absolute" top="0px" >
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
                        />
                    ) : (
                        <HitboxKeys
                            onClick={hitboxButtonClick}
                            interactiveIds={KEYS_SETTINGS_INTERACTIVE_IDS}
                            disabledKeys={disabledKeys}
                        />
                    )}
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
                                        <Fieldset.Legend fontSize={"sm"} fontWeight={"bold"} color={keysEnableSettingActive ? "gray.500" : "white"} >{t.SETTINGS_KEYS_MAPPING_TITLE}</Fieldset.Legend>
                                        <HStack gap={4} >
                                            <SegmentedControl
                                                size={"sm"}
                                                defaultValue={autoSwitch ? t.SETTINGS_KEY_MAPPING_AUTO_SWITCH_LABEL : t.SETTINGS_KEY_MAPPING_MANUAL_SWITCH_LABEL}
                                                items={[t.SETTINGS_KEY_MAPPING_AUTO_SWITCH_LABEL, t.SETTINGS_KEY_MAPPING_MANUAL_SWITCH_LABEL]}
                                                onValueChange={(detail) => setAutoSwitch(detail.value === t.SETTINGS_KEY_MAPPING_AUTO_SWITCH_LABEL)}
                                                disabled={keysEnableSettingActive}
                                            />
                                            <Button 
                                                w="130px" 
                                                size="xs" 
                                                variant={"solid"} 
                                                colorPalette={"red"} 
                                                disabled={keysEnableSettingActive} 
                                                onClick={clearKeyMappingHandler}
                                            >
                                                {t.SETTINGS_KEY_MAPPING_CLEAR_MAPPING_BUTTON}
                                            </Button>
                                        </HStack>
                                        
                                        <KeymappingFieldset
                                            autoSwitch={autoSwitch}
                                            inputKey={inputKey}
                                            inputMode={globalConfig.inputMode ?? Platform.XINPUT}
                                            keyMapping={keyMapping}
                                            changeKeyMappingHandler={(key, hitboxButtons) => {
                                                setHitboxButtons(key, hitboxButtons);
                                                setIsDirty?.(true);
                                            }}
                                            disabled={keysEnableSettingActive}
                                        />
                                    </Stack>

                                    {/* SOCD Mode Choice */}
                                    <RadioCardRoot
                                        colorPalette={"green"}
                                        size={"sm"}
                                        variant={colorMode === "dark" ? "surface" : "outline"}
                                        value={socdMode?.toString() ?? GameSocdMode.SOCD_MODE_UP_PRIORITY.toString()}
                                        onValueChange={(detail) => {
                                            setSocdMode(parseInt(detail.value ?? GameSocdMode.SOCD_MODE_NEUTRAL.toString()) as GameSocdMode);
                                            setIsDirty?.(true);
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
                                                setIsDirty?.(true);
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
                                                setIsDirty?.(true);
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

                    <Card.Footer justifyContent={"flex-start"} >
                        <ContentActionButtons
                            isDirty={_isDirty}
                            resetLabel={t.BUTTON_RESET}
                            saveLabel={t.BUTTON_SAVE}
                            resetHandler={fetchDefaultProfile}
                            saveHandler={saveProfileDetailHandler}
                            disabled={keysEnableSettingActive}
                        />
                    </Card.Footer>
                </Card.Root>
            </Center>
        </Flex>
    )
}
