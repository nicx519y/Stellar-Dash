'use client';

import { ExclusiveDialog } from '@/components/ui/exclusive-dialog';
import { OVERLAY_PRIORITY } from '@/lib/overlay-coordinator';
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
    closable?: boolean;
}

const useConfirmStore = create<ConfirmState>(() => ({
    isOpen: false,
    message: '',
    closable: true,
}));

export function DialogConfirm() {
    const { isOpen, title, message, resolve, closable } = useConfirmStore();

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

    return <ConfirmDialog open={isOpen} title={title} message={message}
        showCancel={!!closable} closable={closable ?? true}
        onCancel={handleCancel} onConfirm={handleConfirm} />;
}

/** Shared confirmation presentation for global prompts and feature-owned dialogs. */
export type ConfirmDialogTone = 'info' | 'success' | 'warning' | 'error' | 'danger';

export function ConfirmDialog({ open, title, message, showCancel = true, closable = true,
    upperMiddle = false, tone = 'warning', onCancel, onConfirm }: {
    open: boolean;
    title?: string;
    message: string;
    showCancel?: boolean;
    closable?: boolean;
    upperMiddle?: boolean;
    tone?: ConfirmDialogTone;
    onCancel: () => void;
    onConfirm: () => void;
}) {
    const { t } = useLanguage();
    return (
        <Portal>
            <ExclusiveDialog priority={OVERLAY_PRIORITY.confirmation}
                open={open}
                onOpenChange={(e) => !e.open && onCancel()}
                closeOnInteractOutside={closable}
                closeOnEscape={closable}
            >
                <Dialog.Backdrop backdropFilter="blur(4px)" />
                <Dialog.Positioner {...(upperMiddle ? { alignItems: 'flex-start', pt: { base: '12', md: '20' }, px: '4' } : {})}>
                    <Dialog.Content {...(upperMiddle ? { my: '0', width: 'calc(100vw - 32px)', maxH: 'calc(100dvh - 112px)', overflowY: 'auto' } : {})}>
                        <Dialog.Header>
                            <Dialog.Title fontSize="sm" opacity={0.75} >{title}</Dialog.Title>
                        </Dialog.Header>
                        <Dialog.Body>
                            <Alert fontSize="sm" status={tone === 'danger' ? 'warning' : tone}
                                colorPalette={{info:'blue', success:'green', warning:'yellow', error:'red', danger:'yellow'}[tone]}>
                                <Text whiteSpace="pre-wrap" lineHeight="1.5" >
                                    {message}
                                </Text>
                            </Alert>
                        </Dialog.Body>
                        <Dialog.Footer>
                            {showCancel && (
                                <Button
                                    width="100px"
                                    size="sm"
                                    colorPalette="teal"
                                    variant="surface"
                                    onClick={onCancel}
                                >
                                    {t.BUTTON_CANCEL}
                                </Button>
                            )}
                            <Button
                                width="100px"
                                size="sm"
                                colorPalette={tone === 'danger' ? 'red' : 'green'}
                                onClick={onConfirm}
                            >
                                {t.BUTTON_CONFIRM}
                            </Button>
                        </Dialog.Footer>
                    </Dialog.Content>
                </Dialog.Positioner>
            </ExclusiveDialog>
        </Portal>
    );
}

export function openConfirm(options: { title?: string; message: string; closable?: boolean }): Promise<boolean> {
    return new Promise((resolve) => {
        useConfirmStore.setState({
            isOpen: true,
            title: options.title,
            message: options.message,
            resolve,
            closable: options.closable ?? true,
        });
    });
}

/**
 * Resolve and close the active confirmation dialog without treating it as an
 * affirmative user action. Transport disconnects use this to invalidate a
 * prompt that belongs to the previous device session.
 */
export function cancelConfirm(): void {
    const { isOpen, resolve } = useConfirmStore.getState();
    if (!isOpen) {
        return;
    }
    resolve?.(false);
    useConfirmStore.setState({
        isOpen: false,
        resolve: undefined,
    });
}
