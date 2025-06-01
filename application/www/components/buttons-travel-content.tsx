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
} from "@chakra-ui/react";
import { Slider } from "@chakra-ui/react"
import { Switch } from "@/components/ui/switch";
import { useEffect, useState } from "react";
import { RAPID_TRIGGER_SETTINGS_INTERACTIVE_IDS, RapidTriggerConfig } from "@/types/gamepad-config";
import Hitbox from "@/components/hitbox";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import useUnsavedChangesWarning from "@/hooks/use-unsaved-changes-warning";
import { useLanguage } from "@/contexts/language-context";
import { ContentActionButtons } from "@/components/content-action-buttons";
import {
    DialogBody,
    DialogCloseTrigger,
    DialogContent,
    DialogHeader,
    DialogRoot,
    DialogTitle,
    DialogTrigger,
} from "@/components/ui/dialog"
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

export function ButtonsTravelContent() {
    const { defaultProfile, updateProfileDetails, resetProfileDetails } = useGamepadConfig();
    const [_isDirty, setIsDirty] = useUnsavedChangesWarning();
    const { t } = useLanguage();

    const [selectedButton, setSelectedButton] = useState<number | null>(0); // 当前选中的按钮
    const [triggerConfigs, setTriggerConfigs] = useState<RapidTriggerConfig[]>([]); // 按钮配置
    const [isAllBtnsConfiguring, setIsAllBtnsConfiguring] = useState<boolean>(true); // 是否同时配置所有按钮
    const [allBtnsConfig, setAllBtnsConfig] = useState<RapidTriggerConfig>({ ...defaultTriggerConfig });
    const [tableViewConfig, setTableViewConfig] = useState<RapidTriggerConfig[]>([]); // 表格视图配置

    // 所有按钮的键值
    const allKeys = RAPID_TRIGGER_SETTINGS_INTERACTIVE_IDS;

    /**
     * 加载触发配置
     */
    useEffect(() => {
        // Load trigger configs from gamepadConfig when it changes
        const triggerConfigs = { ...defaultProfile.triggerConfigs };
        setIsAllBtnsConfiguring(triggerConfigs.isAllBtnsConfiguring ?? false);
        setTriggerConfigs(allKeys.map(key => triggerConfigs.triggerConfigs?.[key] ?? defaultTriggerConfig));
        setAllBtnsConfig(triggerConfigs.triggerConfigs?.[0] ?? defaultTriggerConfig);
        setIsDirty?.(false);
    }, [defaultProfile]);

    useEffect(() => {
        if (isAllBtnsConfiguring) {
            setTableViewConfig(allKeys.map(() => allBtnsConfig));
        } else {
            setTableViewConfig(triggerConfigs);
        }
    }, [triggerConfigs, allBtnsConfig, isAllBtnsConfiguring]);

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
    const saveProfileDetailHandler = async (): Promise<void> => {
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
                    triggerConfigs: newTriggerConfigs
                }
            });
        } else {
            return await updateProfileDetails(profileId, {
                id: profileId,
                triggerConfigs: {
                    isAllBtnsConfiguring: isAllBtnsConfiguring,
                    triggerConfigs: triggerConfigs
                }
            });
        }
    };

    /**
     * 点击按钮
     */
    const handleButtonClick = (id: number) => {
        console.log("handleButtonClick: ", id);
        if (!isAllBtnsConfiguring && selectedButton !== id && id >= 0) {
            setSelectedButton(id);
        }
    };

    return (
        <>
            <Flex direction="row" width={"100%"} height={"100%"} padding={"18px"} >
                <Center >
                    <ProfileSelect />
                </Center>
                <Center flex={1} >
                    <Hitbox
                        onClick={(id) => handleButtonClick(id)}
                        highlightIds={!isAllBtnsConfiguring ? [selectedButton ?? -1] : allKeys}
                        interactiveIds={allKeys}
                    />
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
                                            <Switch colorPalette={"green"} checked={isAllBtnsConfiguring}
                                                onCheckedChange={() => {
                                                    switchAllBtnsConfiging(!isAllBtnsConfiguring);
                                                    setIsDirty?.(true);
                                                }}
                                            >
                                                <Text fontSize={"sm"} opacity={0.75} >{t.SETTINGS_RAPID_TRIGGER_CONFIGURE_ALL}</Text>
                                            </Switch>

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
                                                        } else {
                                                        updateConfig(key as keyof TriggerConfig, v);
                                                        }
                                                        setIsDirty?.(true);
                                                        
                                                    }}
                                                >
                                                    <HStack justifyContent={"space-between"} >
                                                        <Slider.Label>{label}</Slider.Label>
                                                        <Slider.ValueText />
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

                                            

                                            <DialogRoot lazyMount placement={"center"} unmountOnExit={true} >
                                                <DialogTrigger asChild >
                                                    <Box width={"120px"} >
                                                        <Button variant={"ghost"} colorPalette={"green"} size={"sm"} ><LuSheet />{t.BUTTON_PREVIEW_IN_TABLE_VIEW}</Button>
                                                    </Box>
                                                </DialogTrigger>
                                                <DialogContent maxWidth="650px" >
                                                    <DialogHeader>
                                                        <DialogTitle fontSize="sm" opacity={0.75} >{t.SETTINGS_RAPID_TRIGGER_TITLE}</DialogTitle>
                                                    </DialogHeader>
                                                    <DialogBody >
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
                                                    </DialogBody>
                                                    <DialogCloseTrigger />
                                                </DialogContent>
                                            </DialogRoot>

                                            {/* Buttons */}

                                        </VStack>
                                    </Fieldset.Content>
                              
                            </Fieldset.Root>
                        </Card.Body>
                        <Card.Footer justifyContent={"flex-start"} >
                            <ContentActionButtons
                                isDirty={_isDirty}
                                resetLabel={t.BUTTON_RESET}
                                saveLabel={t.BUTTON_SAVE}
                                resetHandler={resetProfileDetails}
                                saveHandler={saveProfileDetailHandler}
                            />
                        </Card.Footer>
                    </Card.Root>
                </Center>
            </Flex>
        </>

    );
} 