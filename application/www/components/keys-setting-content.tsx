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
import { MdCleaningServices } from "react-icons/md";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import useUnsavedChangesWarning from "@/hooks/use-unsaved-changes-warning";
import { useLanguage } from "@/contexts/language-context";
import { ContentActionButtons } from "@/components/content-action-buttons";
import { useColorMode } from "./ui/color-mode";
import { ProfileSelect } from "@/components/profile-select";

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

    useEffect(() => {
        console.log("initializing keys-setting-content: keyConfig: ", defaultProfile.keysConfig);
        if (defaultProfile.keysConfig) {

            const keyLength = Object.keys(defaultProfile.keysConfig?.keyMapping ?? {}).length;

            setSocdMode(defaultProfile.keysConfig?.socdMode ?? GameSocdMode.SOCD_MODE_UP_PRIORITY);
            setInvertXAxis(defaultProfile.keysConfig?.invertXAxis ?? false);
            setInvertYAxis(defaultProfile.keysConfig?.invertYAxis ?? false);
            setFourWayMode(defaultProfile.keysConfig?.fourWayMode ?? false);
            setKeyMapping(Object.assign({}, defaultProfile.keysConfig?.keyMapping ?? {}));
            // 初始化按键启用配置，默认所有按键都启用
            const enableConfig = defaultProfile.keysConfig?.keysEnableTag?.slice(0, keyLength - 1) ?? Array(keyLength).fill(true);
            setKeysEnableConfig(enableConfig);
            setIsDirty?.(false); // reset the unsaved changes warning 
        }
    }, [defaultProfile, setIsDirty]);

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
            <Flex flex={0} justifyContent={"flex-start"} height="fit-content" >
                <ProfileSelect disabled={keysEnableSettingActive} />
            </Flex>
            <Center flex={1} justifyContent={"center"} flexDirection={"column"}  >
                <Center padding="80px 30px" position="relative" flex={1}   >
                    <Box position="absolute" top="50%" left="50%" transform="translateX(-50%) translateY(-350px)" zIndex={10} >
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
                            isButtonMonitoringEnabled={!keysEnableSettingActive}
                        />
                    )}
                </Center>
                <Center flex={0}  >
                    <ContentActionButtons
                        isDirty={_isDirty}
                        resetHandler={fetchDefaultProfile}
                        saveHandler={saveProfileDetailHandler}
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
                                                setIsDirty?.(true);
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
                </Card.Root>
            </Center>
        </Flex>
    )
}
