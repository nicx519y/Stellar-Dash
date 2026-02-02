'use client';

import { MAX_NUM_BUTTON_COMBINATION, Platform, KeyCombination } from "@/types/gamepad-config";
import {
    GameControllerButton,
    GameControllerButtonList,
    XInputButtonMap,
    PS4ButtonMap,
    SwitchButtonMap,
    NUM_BIND_KEY_PER_BUTTON_MAX,
} from "@/types/gamepad-config";
import KeymappingField from "@/components/keymapping-field";
import { showToast } from "@/components/ui/toaster";
import { useEffect, useMemo, useState, forwardRef, useImperativeHandle } from "react";
import { Center, HStack, Separator, VStack, Text, Box } from "@chakra-ui/react";
import { useLanguage } from "@/contexts/language-context";
import { CombinationField } from "./combination-field";

export interface KeymappingFieldsetRef {
    setActiveButton: (button: GameControllerButton) => void;
}

const KeymappingFieldset = forwardRef<KeymappingFieldsetRef, {
    inputMode: Platform,
    inputKey: number,
    keyMapping: { [key in GameControllerButton]?: number[] },
    combinationKeyMapping: KeyCombination[],
    autoSwitch: boolean,
    disabled?: boolean,
    changeKeyMappingHandler: (keyMapping: { [key in GameControllerButton]?: number[] }) => void,
    changeCombinationKeyMappingHandler: (combinationKeyMapping: KeyCombination[]) => void,
}>((props, ref) => {

    const { inputMode, inputKey, keyMapping, combinationKeyMapping, autoSwitch, disabled, changeKeyMappingHandler, changeCombinationKeyMappingHandler } = props;
    const { t } = useLanguage();
    const [activeButton, setActiveButton] = useState<string>(GameControllerButtonList[0].toString());
    const buttonsList = GameControllerButtonList.map(button => button.toString()).concat(Array(MAX_NUM_BUTTON_COMBINATION).fill(0).map((_, index) => `COM${index + 1}`));

    // 暴露方法给父组件
    useImperativeHandle(ref, () => ({
        setActiveButton: (button: string) => {
            setActiveButton(button);
        }
    }), []);

    const isDisabled = useMemo(() => {
        return disabled ?? false;
    }, [disabled]);

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

    const activeButtonIsGameControllerButton = () => {
        return GameControllerButtonList.some(button => button.toString() === activeButton);
    }

    /**
     * change key mapping when input key is changed
     */
    useEffect(() => {

        // input key is valid
        if (inputKey >= 0) {

            // 判断activeButton是否是游戏控制器按钮
            const isGameControllerButton = activeButtonIsGameControllerButton();
            // 可以是游戏控制器按钮，也可以是组合键
            const activeKeyMapping = isGameControllerButton ? keyMapping[activeButton as GameControllerButton] ?? [] : combinationKeyMapping[parseInt(activeButton.replace("COM", "")) - 1]?.keyIndexes ?? [];

            if (activeKeyMapping.indexOf(inputKey) !== -1) { // key already binded
                return;
            } else if (activeKeyMapping.length >= NUM_BIND_KEY_PER_BUTTON_MAX) { // key not binded, and reach max number of key binding per button
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
                    newKeyMapping[activeButton as GameControllerButton] = [...activeKeyMapping, inputKey];

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
                        keyIndexes: [...activeKeyMapping, inputKey]
                    };

                    // 批量更新整个keyMapping和combinationKeyMapping
                    changeKeyMappingHandler(newKeyMapping);
                    changeCombinationKeyMappingHandler(newCombinationKeyMapping);
                }

                if (autoSwitch) {
                    const nextButton = buttonsList[buttonsList.indexOf(activeButton) + 1] ?? buttonsList[0];
                    setActiveButton(nextButton);
                }

            }
        }
    }, [inputKey]);

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


    const TitleLabel = ({ title, mt }: { title: string, mt?: string }) => {
        return (
            <HStack w="full" margin="2px 0" marginTop={mt ?? "2px"} >
                <Separator flex="1" />
                <Text flexShrink="0" fontSize="sm" >{title}</Text>
                <Separator flex="1" />
            </HStack>
        )
    }

    const ButtonField = ({ button }: { button: GameControllerButton }) => {
        return (
            <KeymappingField
                onClick={() => !isDisabled && setActiveButton(button.toString())}
                label={buttonLabelMap.get(button) ?? ""}
                value={keyMapping[button] ?? []}
                changeValue={(v: number[]) => handleSingleKeyMappingChange(button, v)}
                isActive={activeButton === button.toString()}
                disabled={isDisabled}
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
                <TitleLabel title={t.KEYS_MAPPING_TITLE_CUSTOM_COMBINATION} />
                <Box w="full" h="10px" />
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
                            onClick={() => !isDisabled && setActiveButton(`COM${index + 1}`)}
                        />
                    ))}
                </VStack>
            </VStack>
        </Center>
    )
});

KeymappingFieldset.displayName = 'KeymappingFieldset';

export default KeymappingFieldset;