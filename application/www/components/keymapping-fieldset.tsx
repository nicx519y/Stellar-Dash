'use client';

import { MAX_NUM_BUTTON_COMBINATION, MAX_NUM_MACROS, Platform, KeyCombination, MacroConfig } from "@/types/gamepad-config";
import {
    GameControllerButton,
    GameControllerButtonList,
    XInputButtonMap,
    PS4ButtonMap,
    SwitchButtonMap,
} from "@/types/gamepad-config";
import KeymappingField from "@/components/keymapping-field";
import { showToast } from "@/components/ui/toaster";
import { useEffect, useMemo, useState, forwardRef, useImperativeHandle, useCallback } from "react";
import { Center, HStack, VStack, Box, Tabs, Separator } from "@chakra-ui/react";
import { useLanguage } from "@/contexts/language-context";
import { CombinationField } from "./combination-field";
import { TitleLabel } from "@/components/ui/title-label";
import { MacroField } from "./macro-field";

export interface KeymappingFieldsetRef {
    setActiveButton: (button: GameControllerButton) => void;
}

const KeymappingFieldset = forwardRef<KeymappingFieldsetRef, {
    inputMode: Platform,
    inputKey: number,
    keyMapping: { [key in GameControllerButton]?: number[] },
    combinationKeyMapping: KeyCombination[],
    macros: MacroConfig[],
    autoSwitch: boolean,
    maxBindKeysPerButton?: number,
    lockAdvancedBindings?: boolean,
    disabled?: boolean,
    changeKeyMappingHandler: (keyMapping: { [key in GameControllerButton]?: number[] }) => void,
    changeCombinationKeyMappingHandler: (combinationKeyMapping: KeyCombination[]) => void,
    changeMacrosHandler: (macros: MacroConfig[]) => void,
    updateMacrosHandler: (macros: MacroConfig[]) => void | Promise<void>,
    onMacroRecordingChange?: (recording: boolean) => void,
}>((props, ref) => {

    const { inputMode, inputKey, keyMapping, combinationKeyMapping, autoSwitch, disabled, changeKeyMappingHandler, changeCombinationKeyMappingHandler } = props;
    const { t } = useLanguage();
    const [activeButton, setActiveButton] = useState<string>(GameControllerButtonList[0].toString());
    const [activeTab, setActiveTab] = useState<string>("combination");
    const [activeMacroIndex, setActiveMacroIndex] = useState<number | null>(null);
    const [isMacroRecording, setIsMacroRecording] = useState(false);

    // 暴露方法给父组件
    useImperativeHandle(ref, () => ({
        setActiveButton: (button: string) => {
            setActiveButton(button);
            setActiveMacroIndex(null);
        }
    }), []);

    const isDisabled = useMemo(() => {
        return disabled ?? false;
    }, [disabled]);

    const maxBindKeysPerButton = useMemo(() => {
        return props.maxBindKeysPerButton && props.maxBindKeysPerButton > 0 ? props.maxBindKeysPerButton : 3;
    }, [props.maxBindKeysPerButton]);

    const lockAdvancedBindings = useMemo(() => {
        return props.lockAdvancedBindings ?? false;
    }, [props.lockAdvancedBindings]);

    useEffect(() => {
        if (maxBindKeysPerButton !== 1) return;
        let mappingChanged = false;
        const nextKeyMapping = { ...keyMapping };
        Object.entries(nextKeyMapping).forEach(([key, value]) => {
            if ((value?.length ?? 0) > 1) {
                nextKeyMapping[key as GameControllerButton] = value ? value.slice(0, 1) : [];
                mappingChanged = true;
            }
        });
        if (mappingChanged) {
            changeKeyMappingHandler(nextKeyMapping);
        }
    }, [maxBindKeysPerButton, keyMapping]);

    const useCombinationKeyMapping = useMemo(() => {

        const newCombinationKeyMapping = [];
        for (let i = 0; i < MAX_NUM_BUTTON_COMBINATION; i++) {
            if (combinationKeyMapping[i] === undefined) {
                newCombinationKeyMapping[i] = {
                    keyIndexes: [],
                    gameControllerButtons: [],
                }
            } else {
                newCombinationKeyMapping[i] = combinationKeyMapping[i];
            }
        }

        return newCombinationKeyMapping;
    }, [combinationKeyMapping]);

    const macrosByIndex = useMemo(() => {
        const m = new Map<number, MacroConfig>();
        for (const item of props.macros ?? []) {
            if (typeof item?.index !== "number") continue;
            if (item.index < 0 || item.index >= MAX_NUM_MACROS) continue;
            m.set(item.index, item);
        }
        return m;
    }, [props.macros]);

    const nonEmptyCombinationCount = useMemo(() => {
        return useCombinationKeyMapping.filter((c) => (c.keyIndexes?.length ?? 0) > 0).length;
    }, [useCombinationKeyMapping]);

    const handleMacroRecordingChange = useCallback((recording: boolean) => {
        setIsMacroRecording(recording);
        props.onMacroRecordingChange?.(recording);
    }, [props.onMacroRecordingChange]);

    const changeMacroAtIndex = (index: number, macro: MacroConfig) => {
        const next = new Map(macrosByIndex);
        const changedMacroIndices = new Set<number>();
        changedMacroIndices.add(index);
        const hasSteps = (macro.steps?.length ?? 0) > 0;
        const hasTriggers = (macro.triggerKeys?.length ?? 0) > 0;
        if (!hasSteps && !hasTriggers) {
            next.delete(index);
        } else {
            const triggerKeys = (macro.triggerKeys ?? []).filter(k => typeof k === "number" && k >= 0);
            next.set(index, { ...macro, index, triggerKeys });
            if (triggerKeys.length > 0) {
                for (const [mi, m] of next.entries()) {
                    if (mi === index) continue;
                    const filtered = (m.triggerKeys ?? []).filter(k => triggerKeys.indexOf(k) === -1);
                    if (filtered.length !== (m.triggerKeys ?? []).length) {
                        changedMacroIndices.add(mi);
                        next.set(mi, { ...m, index: mi, triggerKeys: filtered });
                    }
                }
            }
        }

        const nextMacros = Array.from(next.values()).sort((a, b) => a.index - b.index);
        props.changeMacrosHandler(nextMacros);
        void props.updateMacrosHandler(nextMacros);

        const activeMacro = next.get(index);
        const triggerKeys = (activeMacro?.triggerKeys ?? []).filter(k => typeof k === "number" && k >= 0);
        if (triggerKeys.length > 0) {
            const nextKeyMapping = { ...keyMapping };
            let keyMappingChanged = false;
            Object.entries(nextKeyMapping).forEach(([key, value]) => {
                const filtered = (value ?? []).filter(v => triggerKeys.indexOf(v) === -1);
                if (filtered.length !== (value ?? []).length) {
                    nextKeyMapping[key as GameControllerButton] = filtered;
                    keyMappingChanged = true;
                }
            });
            if (keyMappingChanged) changeKeyMappingHandler(nextKeyMapping);

            const nextCombinationKeyMapping = [...useCombinationKeyMapping].map((c) => {
                const filtered = (c.keyIndexes ?? []).filter(v => triggerKeys.indexOf(v) === -1);
                if (filtered.length === (c.keyIndexes ?? []).length) return c;
                return { ...c, keyIndexes: filtered };
            });
            const combinationChanged = nextCombinationKeyMapping.some((c, i) => (c.keyIndexes?.length ?? 0) !== (useCombinationKeyMapping[i]?.keyIndexes?.length ?? 0));
            if (combinationChanged) changeCombinationKeyMappingHandler(nextCombinationKeyMapping);
        }
    };

    const activeButtonIsGameControllerButton = () => {
        return GameControllerButtonList.some(button => button.toString() === activeButton);
    }

    /**
     * change key mapping when input key is changed
     */
    useEffect(() => {

        // input key is valid
        if (inputKey >= 0) {
            if (isMacroRecording) {
                return;
            }
            if (activeTab === "macros" && activeMacroIndex !== null) {
                return;
            }

            // 判断activeButton是否是游戏控制器按钮
            const isGameControllerButton = activeButtonIsGameControllerButton();
            const isCombinationButton = activeButton.startsWith("COM") && Number.isFinite(parseInt(activeButton.replace("COM", "")));
            if (!isGameControllerButton && !isCombinationButton) {
                return;
            }
            // 可以是游戏控制器按钮，也可以是组合键
            const activeKeyMapping = isGameControllerButton ? keyMapping[activeButton as GameControllerButton] ?? [] : combinationKeyMapping[parseInt(activeButton.replace("COM", "")) - 1]?.keyIndexes ?? [];

            if (activeKeyMapping.indexOf(inputKey) !== -1) { // key already binded
                return;
            } else if (activeKeyMapping.length >= maxBindKeysPerButton) {
                showToast({
                    title: t.KEY_MAPPING_ERROR_MAX_KEYS_TITLE,
                    description: t.KEY_MAPPING_ERROR_MAX_KEYS_DESC,
                    type: "warning",
                });
                return;
            } else { // key not binded, and not reach max number of key binding per button

                // 创建新的keyMapping对象
                const newKeyMapping = { ...keyMapping };
                const newCombinationKeyMapping = [...useCombinationKeyMapping];

                if (isGameControllerButton) {
                    // 处理普通游戏控制器按钮
                    // remove input key from other button
                    Object.entries(newKeyMapping).forEach(([key, value]) => {
                        if (key !== activeButton && value.indexOf(inputKey) !== -1) {
                            newKeyMapping[key as GameControllerButton] = value.filter(v => v !== inputKey);
                        }
                    });

                    // remove input key from combination buttons
                    newCombinationKeyMapping.forEach((combination, index) => {
                        if (combination.keyIndexes.indexOf(inputKey) !== -1) {
                            newCombinationKeyMapping[index] = {
                                ...combination,
                                keyIndexes: combination.keyIndexes.filter(v => v !== inputKey)
                            };
                        }
                    });

                    // add input key to active button
                    newKeyMapping[activeButton as GameControllerButton] = maxBindKeysPerButton === 1
                        ? [inputKey]
                        : [...activeKeyMapping, inputKey];

                    // 批量更新整个keyMapping
                    changeKeyMappingHandler(newKeyMapping);
                    changeCombinationKeyMappingHandler(newCombinationKeyMapping);
                } else {
                    // 处理组合键按钮
                    const combinationIndex = parseInt(activeButton.replace("COM", "")) - 1;

                    // remove input key from other combination buttons
                    newCombinationKeyMapping.forEach((combination, index) => {
                        if (index !== combinationIndex && combination.keyIndexes.indexOf(inputKey) !== -1) {
                            newCombinationKeyMapping[index] = {
                                ...combination,
                                keyIndexes: combination.keyIndexes.filter(v => v !== inputKey)
                            };
                        }
                    });

                    // remove input key from game controller buttons
                    Object.entries(newKeyMapping).forEach(([key, value]) => {
                        if (value.indexOf(inputKey) !== -1) {
                            newKeyMapping[key as GameControllerButton] = value.filter(v => v !== inputKey);
                        }
                    });

                    // add input key to active combination button
                    newCombinationKeyMapping[combinationIndex] = {
                        ...newCombinationKeyMapping[combinationIndex],
                        keyIndexes: maxBindKeysPerButton === 1 ? [inputKey] : [...activeKeyMapping, inputKey]
                    };

                    // 批量更新整个keyMapping和combinationKeyMapping
                    changeKeyMappingHandler(newKeyMapping);
                    changeCombinationKeyMappingHandler(newCombinationKeyMapping);
                }

                if (props.macros && props.macros.length > 0) {
                    const nextMacrosMap = new Map<number, MacroConfig>(macrosByIndex);
                    let changed = false;
                    const changedMacroIndices = new Set<number>();
                    for (const [mi, m] of nextMacrosMap.entries()) {
                        const filtered = (m.triggerKeys ?? []).filter(k => k !== inputKey);
                        if (filtered.length !== (m.triggerKeys ?? []).length) {
                            nextMacrosMap.set(mi, { ...m, index: mi, triggerKeys: filtered });
                            changed = true;
                            changedMacroIndices.add(mi);
                        }
                    }
                    if (changed) {
                        const nextMacros = Array.from(nextMacrosMap.values()).sort((a, b) => a.index - b.index);
                        props.changeMacrosHandler(nextMacros);
                        void props.updateMacrosHandler(nextMacros);
                    }
                }

                if (autoSwitch && isGameControllerButton) {
                    const controllerButtonsList = GameControllerButtonList.map(button => button.toString());
                    const currentIndex = controllerButtonsList.indexOf(activeButton);
                    const nextButton = controllerButtonsList[currentIndex + 1] ?? controllerButtonsList[0];
                    setActiveButton(nextButton);
                }

            }
        }
    }, [inputKey, isMacroRecording, maxBindKeysPerButton]);

    /**
     * get button label map by input mode
     */
    const buttonLabelMap = useMemo(() => {
        switch (inputMode) {
            case Platform.XINPUT: return XInputButtonMap;
            case Platform.PS4: return PS4ButtonMap;
            case Platform.PS5: return PS4ButtonMap;
            case Platform.XBOX: return XInputButtonMap;
            case Platform.SWITCH: return SwitchButtonMap;
            default: return new Map<GameControllerButton, string>();
        }
    }, [inputMode]);

    // 处理单个按键映射变化的辅助函数
    const handleSingleKeyMappingChange = (key: GameControllerButton, value: number[]) => {
        const newKeyMapping = { ...keyMapping };
        newKeyMapping[key] = value;
        changeKeyMappingHandler(newKeyMapping);
    };

    const ButtonField = ({ button }: { button: GameControllerButton }) => {
        return (
            <KeymappingField
                onClick={() => {
                    if (isDisabled) return;
                    setActiveMacroIndex(null);
                    setActiveButton(button.toString());
                }}
                label={buttonLabelMap.get(button) ?? ""}
                value={keyMapping[button] ?? []}
                changeValue={(v: number[]) => handleSingleKeyMappingChange(button, v)}
                isActive={activeButton === button.toString()}
                disabled={isDisabled}
                maxKeys={maxBindKeysPerButton}
            />
        )
    }

    // 组合键值变化处理
    const combinationValueChangeHandler = (newCombination: KeyCombination, index: number) => {
        const newCombinationKeyMapping = [...useCombinationKeyMapping];
        // 保留原有的 gameControllerButtons，但清空 keyIndexes
        newCombinationKeyMapping[index] = {
            ...newCombinationKeyMapping[index],
            keyIndexes: newCombination.keyIndexes,
            gameControllerButtons: newCombination.gameControllerButtons
        };
        changeCombinationKeyMappingHandler(newCombinationKeyMapping);
    }


    return (
        <Center w="full">
            <VStack w="full" gap={"4px"} >
                <HStack gap="22px" >
                    <Box w="50%" >
                        <TitleLabel title={t.KEYS_MAPPING_TITLE_DPAD} mt="0px" />
                        <ButtonField button={GameControllerButton.DPAD_UP} />
                        <HStack>
                            <ButtonField button={GameControllerButton.DPAD_LEFT} />
                            <ButtonField button={GameControllerButton.DPAD_RIGHT} />
                        </HStack>
                        <ButtonField button={GameControllerButton.DPAD_DOWN} />
                    </Box>
                    <Box w="50%" >
                        <TitleLabel title={t.KEYS_MAPPING_TITLE_CORE} mt="0px" />
                        <ButtonField button={GameControllerButton.B4} />
                        <HStack>
                            <ButtonField button={GameControllerButton.B3} />
                            <ButtonField button={GameControllerButton.B2} />
                        </HStack>
                        <ButtonField button={GameControllerButton.B1} />
                    </Box>
                </HStack>
                <Box w="full" h="10px" />
                <TitleLabel title={t.KEYS_MAPPING_TITLE_SHOULDER} />
                <HStack gap="200px" >
                    <ButtonField button={GameControllerButton.L3} />
                    <ButtonField button={GameControllerButton.R3} />
                </HStack>
                <HStack gap="200px" >
                    <ButtonField button={GameControllerButton.L2} />
                    <ButtonField button={GameControllerButton.R2} />
                </HStack>
                <HStack gap="200px" >
                    <ButtonField button={GameControllerButton.L1} />
                    <ButtonField button={GameControllerButton.R1} />
                </HStack>
                <Box w="full" h="10px" />
                <TitleLabel title={t.KEYS_MAPPING_TITLE_FUNCTION} />
                <HStack>
                    <ButtonField button={GameControllerButton.S1} />
                    <ButtonField button={GameControllerButton.S2} />
                    <ButtonField button={GameControllerButton.A1} />
                    <ButtonField button={GameControllerButton.A2} />
                </HStack>
                <Box w="full" h="10px" />
                <Tabs.Root
                    w="full"
                    value={activeTab}
                    size="sm"
                    variant="plain"
                    colorPalette="green"
                    onValueChange={(details) => {
                        setActiveTab(details.value);
                        if (lockAdvancedBindings) {
                            return;
                        }
                        if (details.value === "macros") {
                            setActiveButton("");
                            setActiveMacroIndex(prev => prev ?? 0);
                        } else {
                            setActiveMacroIndex(null);
                            if (!activeButton) {
                                setActiveButton(GameControllerButtonList[0].toString());
                            }
                        }
                    }}
                >
                    <HStack w="full" margin="2px 0" marginTop={"2px"} >
                        <Separator flex="1" />
                        <Tabs.List>
                            <Tabs.Trigger value="combination" fontWeight="bold" disabled={lockAdvancedBindings}>
                                {t.KEYS_MAPPING_TITLE_CUSTOM_COMBINATION} ({nonEmptyCombinationCount})
                            </Tabs.Trigger>
                            <Tabs.Trigger value="macros" fontWeight="bold" disabled={lockAdvancedBindings}>
                                {t.KEYS_MAPPING_TITLE_MACROS} ({props.macros?.length ?? 0})
                            </Tabs.Trigger>
                        </Tabs.List>
                        <Separator flex="1" />
                    </HStack>
                </Tabs.Root>
                <Box w="full" h="10px" />
                {activeTab === "combination" && !lockAdvancedBindings ? (
                    <VStack>
                        {[...Array(MAX_NUM_BUTTON_COMBINATION)].map((_, index) => (
                            <CombinationField
                                key={index}
                                value={useCombinationKeyMapping?.[index] ?? { keyIndexes: [], gameControllerButtons: [] }}
                                changeValue={(newCombination: KeyCombination) => { combinationValueChangeHandler(newCombination, index) }}
                                label={`COM${index + 1}`}
                                isActive={activeButton === `COM${index + 1}`}
                                disabled={disabled}
                                inputMode={inputMode}
                                onClick={() => {
                                    if (isDisabled) return;
                                    setActiveMacroIndex(null);
                                    setActiveButton(`COM${index + 1}`);
                                }}
                            />
                        ))}
                    </VStack>
                ) : !lockAdvancedBindings ? (
                    <VStack>
                        {[...Array(MAX_NUM_MACROS)].map((_, index) => (
                            <MacroField
                                key={index}
                                value={macrosByIndex.get(index) ?? { index, triggerKeys: [], steps: [] }}
                                changeValue={(newValue) => changeMacroAtIndex(index, newValue)}
                                label={`MAC${index + 1}`}
                                keyMapping={keyMapping}
                                inputKey={props.inputKey}
                                inputMode={inputMode}
                                suspendInputListening={isMacroRecording}
                                isActive={activeTab === "macros" && activeMacroIndex === index}
                                onClick={() => {
                                    if (isDisabled) return;
                                    setActiveTab("macros");
                                    setActiveMacroIndex(index);
                                    setActiveButton("");
                                }}
                                onRecordingChange={(recording) => {
                                    handleMacroRecordingChange(recording);
                                }}
                                disabled={disabled}
                            />
                        ))}
                    </VStack>
                ) : null}
            </VStack>
        </Center>
    )
});

KeymappingFieldset.displayName = 'KeymappingFieldset';

export default KeymappingFieldset;
