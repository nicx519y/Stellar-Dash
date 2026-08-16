import { Button } from "@chakra-ui/react";
import { openConfirm as openRebootConfirmDialog } from "@/components/dialog-confirm";
import { closeDialog as closeRebootDialog, openDialog as openRebootDialog } from "@/components/dialog-cannot-close";
import { useLanguage } from "@/contexts/language-context";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import { configuredTransportMode } from "@/lib/device-transport";
import { LuGamepad2 } from "react-icons/lu";


export function FinishConfigButton(
    props: {
        disabled?: boolean,
    }
) {
    const { t } = useLanguage();
    const { rebootSystem, setUserRebooting, flushQueue } = useGamepadConfig();
    return (
        <Button
            disabled={props.disabled}
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
                    // 清空设备请求队列后重启
                    try {
                        await flushQueue();
                        console.log('设备请求队列已清空，开始重启系统');
                        await rebootSystem();

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
                    } catch {
                        throw new Error('清空队列或重启失败');
                    }
                }

            }}
        >
            <LuGamepad2 />
            {t.BUTTON_REBOOT_WITH_SAVING}
        </Button>
    )
}
