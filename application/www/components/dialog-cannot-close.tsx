'use client';

import { create } from 'zustand';
import { Alert } from "@/components/ui/alert"
import { Dialog, Text, Portal } from '@chakra-ui/react';

interface DialogState {
    isOpen: boolean;
    title?: string;
    status?: "success" | "warning" | "error" | "info";
    message: string;
}

const useDialogStore = create<DialogState>(() => ({
    isOpen: false,
    message: '',
}));

export function DialogCannotClose() {
    const { isOpen, title, status, message } = useDialogStore();

    if (!isOpen) return null;

    return (
        <Portal>
            <Dialog.Root
                open={isOpen}
                modal
                onPointerDownOutside={e => e.preventDefault()}
                onEscapeKeyDown={e => e.preventDefault()}
            >
                <Dialog.Positioner>
                    <Dialog.Content>
                        <Dialog.Header>
                            {title && (
                                <Dialog.Title>{title}</Dialog.Title>
                            )}
                        </Dialog.Header>
                        <Dialog.Body>
                            <Alert colorPalette="yellow" status={status} >
                                <Text whiteSpace="pre-wrap" lineHeight="1.5" >
                                    {message}
                                </Text>
                            </Alert>
                        </Dialog.Body>
                    </Dialog.Content>
                </Dialog.Positioner>
            </Dialog.Root>
        </Portal>
    )
}

export function openDialog(options: { title?: string; status?: "success" | "warning" | "error" | "info"; message: string }): void {
    useDialogStore.setState({
        isOpen: true,
        title: options?.title ?? "",
        status: options?.status ?? "warning",
        message: options.message,
    });
}

export function closeDialog(): void {
    useDialogStore.setState({
        isOpen: false,
    });
} 