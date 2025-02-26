'use client';

import { create } from 'zustand';
import {
    DialogRoot,
    DialogBody,
    DialogContent,
    DialogHeader,
    DialogTitle,
} from "@/components/ui/dialog"
import { Alert } from "@/components/ui/alert"

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
        <DialogRoot 
            open={isOpen} 
            modal 
            onPointerDownOutside={e => e.preventDefault()} 
            onEscapeKeyDown={e => e.preventDefault()}
        >
            <DialogContent>
                <DialogHeader>
                    {title && (
                        <DialogTitle>{title}</DialogTitle>
                    )}
                </DialogHeader>
                <DialogBody>
                    <Alert colorPalette="yellow" status={status} >
                        {message}
                    </Alert>
                </DialogBody>
            </DialogContent>
        </DialogRoot>
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