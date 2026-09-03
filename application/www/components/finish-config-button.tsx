import { Button } from "@chakra-ui/react";
import { openConfirm as openFinishConfirmDialog } from "@/components/dialog-confirm";
import { closeDialog as closeFinishDialog, openDialog as openFinishDialog, updateDialogMessage as updateFinishDialogMessage } from "@/components/dialog-cannot-close";
import { useLanguage } from "@/contexts/language-context";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import { configuredTransportMode } from "@/lib/device-transport";
import { useEffect, useState } from "react";
import { LuGamepad2 } from "react-icons/lu";


export function FinishConfigButton(
    props: {
        disabled?: boolean,
    }
) {
    const { t } = useLanguage();
    const {
        exitWebConfig,
        setUserRebooting,
        flushDeferredConfig,
        terminateWebConfigActivities,
        deviceConnected,
        setError,
    } = useGamepadConfig();
    const [closing, setClosing] = useState(false);

    useEffect(() => {
        if (!deviceConnected) return;
        closeFinishDialog('finish-config-success');
    }, [deviceConnected]);

    return (
        <Button
            disabled={props.disabled || closing}
            loading={closing}
            colorPalette="green"
            variant="surface"
            size="xs"
            // width={"240px"}
            onClick={async () => {
                const confirmed = await openFinishConfirmDialog({
                    title: t.DIALOG_FINISH_CONFIRM_TITLE,
                    message: t.DIALOG_FINISH_CONFIRM_MESSAGE,
                });

                if (confirmed) {
                    setClosing(true);
                    const savingDialogId = openFinishDialog({
                        id: 'config-saving',
                        title: t.DIALOG_CONFIG_SAVING_TITLE,
                        status: "info",
                        message: t.DIALOG_CONFIG_SAVING_MESSAGE,
                        loading: true,
                    });
                    try {
                        await flushDeferredConfig(
                            async () => {
                                console.log('配置与设备请求队列已保存，正在退出 WebConfig 模式');
                                updateFinishDialogMessage(savingDialogId, t.DIALOG_CONFIG_EXITING_MESSAGE);
                                const expectsDeviceDisconnect = configuredTransportMode() !== 'mock';
                                if (expectsDeviceDisconnect) setUserRebooting(true);
                                try {
                                    await exitWebConfig();
                                } catch (error) {
                                    if (expectsDeviceDisconnect) setUserRebooting(false);
                                    throw error;
                                }
                            },
                            false,
                            terminateWebConfigActivities,
                        );
                        closeFinishDialog(savingDialogId);

                        if (configuredTransportMode() === 'mock') {
                            const dialogId = openFinishDialog({
                                id: 'finish-config-success',
                                title: t.DIALOG_FINISH_SUCCESS_TITLE,
                                status: "success",
                                message: t.DIALOG_FINISH_SUCCESS_MESSAGE,
                                buttons: [{
                                    text: t.BUTTON_CONFIRM,
                                    colorPalette: "green",
                                    onClick: () => closeFinishDialog(dialogId),
                                }],
                            });
                            return;
                        }

                        openFinishDialog({
                            id: 'finish-config-success',
                            title: t.DIALOG_FINISH_SUCCESS_TITLE,
                            status: "warning",
                            message: t.DIALOG_FINISH_SUCCESS_MESSAGE,
                        });
                    } catch (error) {
                        closeFinishDialog(savingDialogId);
                        setError(error instanceof Error ? error.message : '保存配置或退出 WebConfig 失败');
                    } finally {
                        setClosing(false);
                    }
                }

            }}
        >
            <LuGamepad2 />
            {t.BUTTON_FINISH_CONFIGURATION}
        </Button>
    )
}
