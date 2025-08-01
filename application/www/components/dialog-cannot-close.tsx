'use client';

import { create } from 'zustand';
import { Alert } from "@/components/ui/alert"
import { Dialog, Text, Portal, Box, Button } from '@chakra-ui/react';

interface DialogConfig {
    id: string;
    isOpen: boolean;
    title?: string;
    status?: "success" | "warning" | "error" | "info";
    message: string;
    size?: 'xs' | 'sm' | 'md' | 'lg' | 'xl' | 'cover' | 'full';
    buttons?: {
        text: string | React.ReactNode;
        onClick: () => void;
        colorPalette?: "blue" | "red" | "green" | "yellow" | "gray";
        loading?: boolean;
        spinner?: React.ReactNode;
    }[];
}

interface DialogState {
    dialogs: Map<string, DialogConfig>;
}

const useDialogStore = create<DialogState>(() => ({
    dialogs: new Map(),
}));

export function DialogCannotClose() {
    const { dialogs } = useDialogStore();

    return (
        <>
            {Array.from(dialogs.values()).map((dialog) => {
                if (!dialog.isOpen) return null;

                return (


                    <Portal key={dialog.id} >

                        <Box
                            position="fixed"
                            zIndex={9999}
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
                                size={dialog.size ?? 'md'}
                                onPointerDownOutside={e => e.preventDefault()}
                                onEscapeKeyDown={e => e.preventDefault()}
                            >
                                <Dialog.Positioner>
                                    <Dialog.Content>
                                        <Dialog.Header>
                                            {dialog.title && (
                                                <Dialog.Title>{dialog.title}</Dialog.Title>
                                            )}
                                        </Dialog.Header>
                                        <Dialog.Body>
                                            <Alert colorPalette={dialog.status === "success" ? "green" : dialog.status === "error" ? "red" : dialog.status === "info" ? "blue" : "yellow"}>
                                                <Text whiteSpace="pre-wrap" lineHeight="1.5" >
                                                    {dialog.message}
                                                </Text>
                                            </Alert>
                                        </Dialog.Body>
                                        {dialog.buttons && (
                                            <Dialog.Footer>
                                                {dialog.buttons?.map((button, index) => (
                                                    <Button
                                                        key={index}
                                                        onClick={button.onClick}
                                                        colorPalette={button.colorPalette}
                                                        loading={button.loading}
                                                        spinner={button.spinner}
                                                    >
                                                        {button.text}
                                                    </Button>
                                                ))}
                                            </Dialog.Footer>
                                        )}
                                    </Dialog.Content>
                                </Dialog.Positioner>
                            </Dialog.Root>

                        </Box>


                    </Portal>
                );
            })}
        </>
    )
}

export function openDialog(options: {
    id?: string;
    title?: string;
    status?: "success" | "warning" | "error" | "info";
    message: string;
    size?: 'xs' | 'sm' | 'md' | 'lg' | 'xl' | 'cover' | 'full';
    buttons?: {
        text: string | React.ReactNode;
        onClick: () => void;
        colorPalette?: "blue" | "red" | "green" | "yellow" | "gray";
        loading?: boolean;
        spinner?: React.ReactNode;
    }[];
}): string {
    const dialogId = options.id || `dialog-${Date.now()}-${Math.random().toString(36).substr(2, 9)}`;

    console.log('openDialog called with:', options, 'dialogId:', dialogId);

    const currentState = useDialogStore.getState();
    const newDialogs = new Map(currentState.dialogs);

    newDialogs.set(dialogId, {
        id: dialogId,
        isOpen: true,
        title: options?.title ?? "",
        status: options?.status ?? "warning",
        size: options?.size ?? 'md',
        message: options.message,
        buttons: options?.buttons,
    });

    useDialogStore.setState({
        dialogs: newDialogs
    });

    console.log('Dialog state updated, dialogId:', dialogId);
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

export function updateButtonLoading(dialogId: string, buttonIndex: number, loading: boolean): void {
    const currentState = useDialogStore.getState();
    const dialog = currentState.dialogs.get(dialogId);

    if (dialog && dialog.buttons && dialog.buttons[buttonIndex]) {
        const newDialogs = new Map(currentState.dialogs);
        const updatedButtons = [...dialog.buttons];
        updatedButtons[buttonIndex] = {
            ...updatedButtons[buttonIndex],
            loading: loading
        };

        newDialogs.set(dialogId, {
            ...dialog,
            buttons: updatedButtons
        });

        useDialogStore.setState({
            dialogs: newDialogs
        });
    }
}

export function updateDialogMessage(dialogId: string, message: string): void {
    const currentState = useDialogStore.getState();
    const dialog = currentState.dialogs.get(dialogId);

    if (dialog) {
        const newDialogs = new Map(currentState.dialogs);
        newDialogs.set(dialogId, {
            ...dialog,
            message: message
        });

        useDialogStore.setState({
            dialogs: newDialogs
        });
    }
} 