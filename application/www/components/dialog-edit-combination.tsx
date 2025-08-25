'use client';

import { create } from 'zustand';
import { Dialog, Text, Portal, Box, Button, Select, VStack, createListCollection, Center } from '@chakra-ui/react';
import { useMemo, useState, useEffect, useCallback } from 'react';
import { GameControllerButton, SwitchButtonMap, Platform, PS4ButtonMap, XInputButtonMap, GameControllerButtonList, COMBINATION_KEY_MAX_LENGTH } from '@/types/gamepad-config';
import { useLanguage } from '@/contexts/language-context';

interface DialogEditCombinationConfig {
    id: string;
    isOpen: boolean;
    inputMode: Platform;
    combinationIndex: number;
    initialValues?: GameControllerButton[];
    onChange?: (values: GameControllerButton[]) => void;
    combinationValues?: string[];
}

interface DialogState {
    dialogs: Map<string, DialogEditCombinationConfig>;
}

const useDialogStore = create<DialogState>(() => ({
    dialogs: new Map(),
}));

export function DialogEditCombination() {
    const { dialogs } = useDialogStore();
    const { t } = useLanguage();

    return (
        <>
            {Array.from(dialogs.values()).map((dialog) => {
                if (!dialog.isOpen) return null;

                return (
                    <Portal key={dialog.id}>
                        <Box
                            position="fixed"
                            zIndex={9998}
                            top={0}
                            left={0}
                            right={0}
                            bottom={0}
                            display="flex"
                            alignItems="center"
                            justifyContent="center"
                        >
                            <Box
                                position="absolute"
                                top={0}
                                left={0}
                                right={0}
                                bottom={0}
                                bg="blackAlpha.100"
                                backdropFilter="blur(4px)"
                            />

                            <Dialog.Root
                                open={dialog.isOpen}
                                modal
                                size="md"
                                onPointerDownOutside={e => e.preventDefault()}
                                onEscapeKeyDown={e => e.preventDefault()}
                            >
                                <Dialog.Positioner>
                                    <Dialog.Content>
                                        <Dialog.Header>
                                            <Dialog.Title fontSize="sm" opacity={0.75} >{t?.COMBINATION_DIALOG_TITLE} [ COM{dialog.combinationIndex + 1} ]</Dialog.Title>
                                        </Dialog.Header>
                                        <Dialog.Body>
                                            <DialogEditCombinationContent dialog={dialog} />
                                        </Dialog.Body>
                                        <Dialog.Footer>
                                            <Button w="140px" size="sm" colorPalette={"green"} onClick={() => confirmDialog(dialog.id)} >{t?.COMBINATION_DIALOG_BUTTON_CONFIRM}</Button>
                                        </Dialog.Footer>
                                    </Dialog.Content>
                                </Dialog.Positioner>
                            </Dialog.Root>
                        </Box>
                    </Portal>
                );
            })}
        </>
    );
}

// 对话框内容组件
function DialogEditCombinationContent({ dialog }: { dialog: DialogEditCombinationConfig }) {

    const { t } = useLanguage();

    const buttonLabelMap = useMemo(() => {
        switch (dialog.inputMode) {
            case Platform.XINPUT: return XInputButtonMap;
            case Platform.PS4: return PS4ButtonMap;
            case Platform.PS5: return PS4ButtonMap;
            case Platform.SWITCH: return SwitchButtonMap;
            default: return new Map<GameControllerButton, string>();
        }
    }, [dialog.inputMode]);

    const availableKeys = useMemo(() => {
        return createListCollection({
            items: [
                ...GameControllerButtonList.map((key) => ({
                    value: key.toString(),
                    label: buttonLabelMap.get(key) ?? "",
                })),
            ],
        });
    }, [buttonLabelMap]);

    const [combinationValues, setCombinationValues] = useState<string[]>(dialog.initialValues?.map(value => value.toString()) ?? []);


    const onValueChange = useCallback((index: number, value: string) => {
        const newValues = [...combinationValues];
        newValues[index] = value;
        setCombinationValues(newValues);
    }, [combinationValues, dialog.id]);

    useEffect(() => {
        const newValues = [...combinationValues];
        const currentState = useDialogStore.getState();
        const newDialogs = new Map(currentState.dialogs);
        const currentDialog = newDialogs.get(dialog.id);
        if (currentDialog) {
            newDialogs.set(dialog.id, { ...currentDialog, combinationValues: newValues });
            useDialogStore.setState({ dialogs: newDialogs });
        }
    }, [combinationValues, dialog.id]);

    return (
        <Box>
            <Text fontSize="14px" color="gray.400" mb="16px">
                {t?.COMBINATION_DIALOG_MESSAGE}
            </Text>
            <Center >
                <VStack gap="0px" w="full" >
                    {Array.from({ length: COMBINATION_KEY_MAX_LENGTH }).map((_, index) => (
                        <Box key={index} w="100%">
                            <Select.Root
                                collection={availableKeys}
                                variant="subtle"
                                size="sm"
                                value={combinationValues[index] ? [combinationValues[index]] : []}
                                onValueChange={e => onValueChange(index, e.value[0])}
                            >
                                <Select.HiddenSelect />
                                <Select.Control>
                                    <Select.Trigger>
                                        <Select.ValueText fontSize="xs" textAlign={"center"} placeholder={t?.COMBINATION_SELECT_PLACEHOLDER} />
                                    </Select.Trigger>
                                    <Select.IndicatorGroup>
                                        <Select.ClearTrigger />
                                        <Select.Indicator />
                                    </Select.IndicatorGroup>
                                </Select.Control>
                                <Portal>
                                    <Select.Positioner>
                                        <Select.Content fontSize="xs" zIndex={9999} textAlign={"center"} >
                                            {availableKeys.items.map((item: { value: string, label: string }) => (
                                                <Select.Item item={item} key={item.value}>
                                                    {item.label}
                                                    <Select.ItemIndicator />
                                                </Select.Item>
                                            ))}
                                        </Select.Content>
                                    </Select.Positioner>
                                </Portal>
                            </Select.Root>
                            {index < COMBINATION_KEY_MAX_LENGTH - 1 && <Center w="full" h="30px" > + </Center>}
                        </Box>
                    ))}
                </VStack>
            </Center>
        </Box>
    );
}

export function openEditCombinationDialog(options: {
    id?: string;
    combinationIndex: number;
    inputMode: Platform;
    initialValues?: GameControllerButton[];
    onChange?: (values: GameControllerButton[]) => void;
}): string {
    const dialogId = options.id || `edit-combination-${Date.now()}-${Math.random().toString(36).substr(2, 9)}`;

    const currentState = useDialogStore.getState();
    const newDialogs = new Map(currentState.dialogs);

    newDialogs.set(dialogId, {
        id: dialogId,
        isOpen: true,
        inputMode: options.inputMode,
        combinationIndex: options.combinationIndex,
        initialValues: options.initialValues,
        onChange: options.onChange,
    });

    useDialogStore.setState({
        dialogs: newDialogs
    });

    return dialogId;
}

export function closeDialog(dialogId?: string): void {
    const currentState = useDialogStore.getState();
    const newDialogs = new Map(currentState.dialogs);

    if (dialogId) {
        // 关闭指定的对话框
        const dialog = newDialogs.get(dialogId);
        if (dialog) {
            newDialogs.set(dialogId, { ...dialog, isOpen: false });
        }
    } else {
        // 关闭所有对话框
        newDialogs.forEach((dialog, id) => {
            newDialogs.set(id, { ...dialog, isOpen: false });
        });
    }

    useDialogStore.setState({
        dialogs: newDialogs
    });
}

export function confirmDialog(dialogId: string): void {
    console.log("dialog-edit-combination : confirmDialog");
    const currentState = useDialogStore.getState();
    const newDialogs = new Map(currentState.dialogs);
    const dialog = newDialogs.get(dialogId);
    if (dialog && dialog.combinationValues) {
        const newValues = dialog.combinationValues
            .filter(value => value !== undefined && value !== null && value !== "")
            .filter((value, index, self) => self.indexOf(value) === index) // 去重
            .map(value => GameControllerButtonList.find(item => item.toString() === value) as GameControllerButton);

        // 检查是否有值改变（检查每一项元素是否一致，顺序不同也算）
        const hasChanged = (() => {
            if (!dialog.initialValues || dialog.initialValues.length !== newValues.length) {
                return true;
            }

            // 创建两个数组的副本并排序，然后比较每个元素
            const sortedNewValues = [...newValues].sort();
            const sortedInitialValues = [...dialog.initialValues].sort();

            for (let i = 0; i < sortedNewValues.length; i++) {
                if (sortedNewValues[i] !== sortedInitialValues[i]) {
                    return true;
                }
            }
            return false;
        })();

        if (hasChanged) {
            dialog.onChange?.(newValues);
        }
        closeDialog(dialogId);
    }
}

DialogEditCombination.displayName = 'DialogEditCombination';