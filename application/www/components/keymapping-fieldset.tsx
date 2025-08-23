'use client';

import { Platform } from "@/types/gamepad-config";
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

export interface KeymappingFieldsetRef {
    setActiveButton: (button: GameControllerButton) => void;
}

const KeymappingFieldset = forwardRef<KeymappingFieldsetRef, {
    inputMode: Platform,
    inputKey: number,
    keyMapping: { [key in GameControllerButton]?: number[] },
    autoSwitch: boolean,
    disabled?: boolean,
    changeKeyMappingHandler: (keyMapping: { [key in GameControllerButton]?: number[] }) => void,
}>((props, ref) => {

    const { inputMode, inputKey, keyMapping, autoSwitch, disabled, changeKeyMappingHandler } = props;
    const { t } = useLanguage();

    const [activeButton, setActiveButton] = useState<GameControllerButton>(GameControllerButtonList[0]);

    // 暴露方法给父组件
    useImperativeHandle(ref, () => ({
        setActiveButton: setActiveButton
    }), []);

    const isDisabled = useMemo(() => {
        return disabled ?? false;
    }, [disabled]);

    /**
     * change key mapping when input key is changed
     */
    useEffect(() => {

        // input key is valid
        if (inputKey >= 0) {

            const activeKeyMapping = keyMapping[activeButton] ?? [];
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

                // remove input key from other button
                Object.entries(newKeyMapping).forEach(([key, value]) => {
                    if (key !== activeButton && value.indexOf(inputKey) !== -1) {
                        newKeyMapping[key as GameControllerButton] = value.filter(v => v !== inputKey);
                    }
                });

                // add input key to active button
                newKeyMapping[activeButton] = [...activeKeyMapping, inputKey];

                // 批量更新整个keyMapping
                changeKeyMappingHandler(newKeyMapping);

                if (autoSwitch) {
                    const nextButton = GameControllerButtonList[GameControllerButtonList.indexOf(activeButton) + 1] ?? GameControllerButtonList[0];
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
                onClick={() => !isDisabled && setActiveButton(button)}
                label={buttonLabelMap.get(button) ?? ""}
                value={keyMapping[button] ?? []}
                changeValue={(v: number[]) => handleSingleKeyMappingChange(button, v)}
                isActive={activeButton === button}
                disabled={isDisabled}
            />
        )
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
            </VStack>
        </Center>
    )
});

KeymappingFieldset.displayName = 'KeymappingFieldset';

export default KeymappingFieldset;