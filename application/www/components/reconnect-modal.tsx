'use client';

import { openDialog, closeDialog, updateButtonLoading } from '@/components/dialog-cannot-close';
import { BeatLoader } from "react-spinners"
import { PiPlugsConnectedFill } from "react-icons/pi";

let reconnectDialogId: string | null = null;

export function openReconnectModal(options: {
    title?: string;
    message: string;
    buttonText: string;
    onReconnect: () => void;
    isLoading?: boolean;
}): void {
    console.log('openReconnectModal called with:', options);
    console.log('About to call openDialog...');
    
    reconnectDialogId = openDialog({
        id: 'reconnect-modal',
        title: options.title,
        status: "warning",
        size: 'lg',
        message: options.message,
        buttons: [
            {
                text: (
                    <>
                        <PiPlugsConnectedFill />
                        {options.buttonText}
                    </>
                ),
                onClick: options.onReconnect,
                colorPalette: "blue",
                loading: options.isLoading ?? false,
                spinner: <BeatLoader size={8} color="white" />
            }
        ]
    });
    console.log('openDialog called from openReconnectModal, dialogId:', reconnectDialogId);
}

export function closeReconnectModal(): void {
    if (reconnectDialogId) {
        closeDialog(reconnectDialogId);
        reconnectDialogId = null;
    }
}

export function setReconnectModalLoading(loading: boolean): void {
    if (reconnectDialogId) {
        // 更新第一个按钮（重连按钮）的 loading 状态
        updateButtonLoading(reconnectDialogId, 0, loading);
    }
} 