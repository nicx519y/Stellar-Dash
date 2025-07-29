'use client';

import { create } from 'zustand';
import { Alert } from "@/components/ui/alert"
import { Button } from "@/components/ui/button"
import { useLanguage } from "@/contexts/language-context";
import { Dialog, Text, Portal } from '@chakra-ui/react';

interface ConfirmState {
    isOpen: boolean;
    title?: string;
    message: string;
    resolve?: (value: boolean) => void;
}

const useConfirmStore = create<ConfirmState>(() => ({
    isOpen: false,
    message: '',
}));

export function DialogConfirm() {
    const { isOpen, title, message, resolve } = useConfirmStore();
    const { t } = useLanguage();

    const handleClose = () => {
        useConfirmStore.setState({ isOpen: false });
    };

    const handleCancel = () => {
        resolve?.(false);
        handleClose();
    };

    const handleConfirm = () => {
        resolve?.(true);
        handleClose();
    };

    return (
        <Portal>
            <Dialog.Root open={isOpen} onOpenChange={handleClose}>
                <Dialog.Positioner>
                    <Dialog.Content>
                        <Dialog.Header>
                            <Dialog.Title fontSize="sm" opacity={0.75} >{title}</Dialog.Title>
                        </Dialog.Header>
                        <Dialog.Body>
                            <Alert fontSize="sm" colorPalette={"yellow"}>
                                <Text whiteSpace="pre-wrap" lineHeight="1.5" >
                                    {message}
                                </Text>
                            </Alert>
                        </Dialog.Body>
                        <Dialog.Footer>
                            <Button
                                width="100px"
                                size="sm"
                                colorPalette="teal"
                                variant="surface"
                                onClick={handleCancel}
                            >
                                {t.BUTTON_CANCEL}
                            </Button>
                            <Button
                                width="100px"
                                size="sm"
                                colorPalette="green"
                                onClick={handleConfirm}
                            >
                                {t.BUTTON_CONFIRM}
                            </Button>
                        </Dialog.Footer>
                    </Dialog.Content>
                </Dialog.Positioner>
            </Dialog.Root>
        </Portal>
    );
}

export function openConfirm(options: { title?: string; message: string }): Promise<boolean> {
    return new Promise((resolve) => {
        useConfirmStore.setState({
            isOpen: true,
            title: options.title,
            message: options.message,
            resolve,
        });
    });
}
