import { Button } from "@chakra-ui/react";
import { openConfirm as openRebootConfirmDialog } from "@/components/dialog-confirm";
import { closeDialog as closeRebootDialog, openDialog as openRebootDialog } from "@/components/dialog-cannot-close";
import { useLanguage } from "@/contexts/language-context";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import { configuredTransportMode } from "@/lib/device-transport";
import { useState } from "react";
import { LuGamepad2 } from "react-icons/lu";


export function FinishConfigButton(
    props: {
        disabled?: boolean,
    }
) {
    const { t } = useLanguage();
    const {
        rebootSystem,
        setUserRebooting,
        flushDeferredConfig,
        deferredConfigDirty,
        deferredConfigSaving,
        setError,
    } = useGamepadConfig();
    const [closing, setClosing] = useState(false);
    return (
        <Button
            disabled={props.disabled || closing}
            loading={closing}
            colorPalette="green"
            variant="surface"
            size="xs"
            // width={"240px"}
            onClick={async () => {
                const confirmed = await openRebootConfirmDialog({
                    title: t.DIALOG_REBOOT_CONFIRM_TITLE,
                    message: t.DIALOG_REBOOT_CONFIRM_MESSAGE,
                });

                if (confirmed) {
                    setClosing(true);
                    const savingDialogId = deferredConfigDirty || deferredConfigSaving
                        ? openRebootDialog({
                            id: 'config-saving',
                            title: t.DIALOG_CONFIG_SAVING_TITLE,
                            status: "info",
                            message: t.DIALOG_CONFIG_SAVING_MESSAGE,
                            loading: true,
                        })
                        : undefined;
                    try {
                        await flushDeferredConfig(async () => {
                            console.log('配置与设备请求队列已保存，开始重启系统');
                            await rebootSystem();
                        });
                        if (savingDialogId) closeRebootDialog(savingDialogId);

                        if (configuredTransportMode() === 'mock') {
                            const dialogId = openRebootDialog({
                                id: 'reboot-success',
                                title: t.DIALOG_REBOOT_SUCCESS_TITLE,
                                status: "success",
                                message: t.DIALOG_REBOOT_SUCCESS_MESSAGE,
                                buttons: [{
                                    text: t.BUTTON_CONFIRM,
                                    colorPalette: "green",
                                    onClick: () => closeRebootDialog(dialogId),
                                }],
                            });
                            return;
                        }

                        setUserRebooting(true); // 标示用户主动重启
                        openRebootDialog({
                            id: 'reboot-success',
                            title: t.DIALOG_REBOOT_SUCCESS_TITLE,
                            status: "warning",
                            message: t.DIALOG_REBOOT_SUCCESS_MESSAGE,
                        });
                    } catch (error) {
                        if (savingDialogId) closeRebootDialog(savingDialogId);
                        setError(error instanceof Error ? error.message : '保存配置或重启失败');
                    } finally {
                        setClosing(false);
                    }
                }

            }}
        >
            <LuGamepad2 />
            {t.BUTTON_REBOOT_WITH_SAVING}
        </Button>
    )
}
