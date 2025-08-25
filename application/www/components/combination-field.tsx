import { Text, Separator, Button, HStack } from "@chakra-ui/react";
import { KeyField } from "./key-field";
import { openEditCombinationDialog } from "./dialog-edit-combination";
import { GameControllerButton, Platform, XInputButtonMap, PS4ButtonMap, SwitchButtonMap, KeyCombination } from "@/types/gamepad-config";
import { useMemo, useEffect } from "react";
import { useLanguage } from "@/contexts/language-context";

export const CombinationField = (
    props: {
        value: KeyCombination,
        changeValue: ( newValue: KeyCombination ) => void,
        label: string,
        inputMode: Platform,
        isActive?: boolean,
        disabled?: boolean,
        onClick?: () => void,
    }
) => {

    const { t } = useLanguage();

    const handleEditClick = () => {
        if (!props.disabled) {
            const combinationIndex = parseInt(props.label.replace('COM', '')) - 1;
            openEditCombinationDialog({
                combinationIndex,
                inputMode: props.inputMode,
                initialValues: props.value.gameControllerButtons,
                onChange: (newValues) => {
                    const newCombination = {
                        keyIndexes: props.value.keyIndexes,
                        gameControllerButtons: newValues,
                    }
                    props.changeValue(newCombination);
                }
            });
        }
    };

    const buttonLabelMap = useMemo(() => {
        switch (props.inputMode) {
            case Platform.XINPUT: return XInputButtonMap;
            case Platform.PS4: return PS4ButtonMap;
            case Platform.PS5: return PS4ButtonMap;
            case Platform.SWITCH: return SwitchButtonMap;
            default: return new Map<GameControllerButton, string>();
        }
    }, [props.inputMode]);

    const handleKeyIndexesChange = (newKeyIndexes: string[]) => {
        const newCombination = {
            keyIndexes: newKeyIndexes.map(index => parseInt(index) - 1),
            gameControllerButtons: props.value.gameControllerButtons,
        }
        props.changeValue(newCombination);
    }

    useEffect(() => {
        console.log("combination-field : props.value", props.value);
    }, [props.value]);

    return (
        <HStack gap={2} >
            <Text 
                fontSize="10px"
                color="gray.400"
                fontWeight="bold"
                lineHeight="1rem"
            >
                {`[ ${props.label} ]`}
            </Text>
            <KeyField 
                value={props.value.keyIndexes.map(index => (index + 1).toString())} 
                isActive={props.isActive ?? false} 
                disabled={props.disabled ?? false} 
                onClick={props.onClick ?? undefined}
                changeValue={handleKeyIndexesChange}
                width={170} 
            />
            <Separator orientation="vertical" height="5" />
            <HStack gap={2} >
                <KeyField 
                    prefix=" "
                    joiner=" + "
                    value={props.value.gameControllerButtons.filter(button => button !== undefined || button !== null).map(button => buttonLabelMap.get(button) ?? "")} 
                    changeValue={() => {}} 
                    isActive={false} 
                    disabled={props.disabled ?? false} 
                    width={300} 
                    onClick={handleEditClick}
                />
                <Button
                    size="xs"
                    colorPalette="blue"
                    variant="surface"
                    onClick={handleEditClick}
                    disabled={props.disabled}
                    style={{ 
                        minWidth: '60px',
                        height: '29px',
                    }}
                >
                    {t?.COMBINATION_FIELD_EDIT_BUTTON}
                </Button>
            </HStack>
        </HStack>
    );
};

CombinationField.displayName = 'CombinationField';