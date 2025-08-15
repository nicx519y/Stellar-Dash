import { Button } from "@chakra-ui/react";
import { openConfirm as openRebootConfirmDialog } from "@/components/dialog-confirm";
import { openDialog as openRebootDialog } from "@/components/dialog-cannot-close";
import { useLanguage } from "@/contexts/language-context";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
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
            colorPalette="blue"
            variant="surface"
            size="sm"
            width={"240px"}
            onClick={async () => {
                const confirmed = await openRebootConfirmDialog({
                    title: t.DIALOG_REBOOT_CONFIRM_TITLE,
                    message: t.DIALOG_REBOOT_CONFIRM_MESSAGE,
                });

                if (confirmed) {
                    // 清空websocket队列后重启
                    try {
                        await flushQueue();
                        console.log('WebSocket队列已清空，开始重启系统');
                        await rebootSystem();
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