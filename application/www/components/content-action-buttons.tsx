import { Stack, Button, Center } from "@chakra-ui/react";
import { openConfirm as openRebootConfirmDialog } from "@/components/dialog-confirm";
import { openDialog as openRebootDialog } from "@/components/dialog-cannot-close";
import { useLanguage } from "@/contexts/language-context";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import { LuGamepad2 } from "react-icons/lu";


export function ContentActionButtons(
    props: {
        disabled?: boolean,
    }
) {
    const { t } = useLanguage();
    const { rebootSystem } = useGamepadConfig();
    return (
        <Center height="100px" pb="60px" >
            <Stack direction="row" gap={4}>
                <Button
                    disabled={props.disabled}
                    colorPalette="blue"
                    variant="solid"
                    size="md"
                    width={"280px"}
                    onClick={async () => {
                        const confirmed = await openRebootConfirmDialog({
                            title: t.DIALOG_REBOOT_CONFIRM_TITLE,
                            message: t.DIALOG_REBOOT_CONFIRM_MESSAGE,
                        });
                        if (confirmed) {
                            rebootSystem()
                                .then(() => {
                                    openRebootDialog({
                                        title: t.DIALOG_REBOOT_SUCCESS_TITLE,
                                        status: "success",
                                        message: t.DIALOG_REBOOT_SUCCESS_MESSAGE,
                                    });
                                });
                        }
                    }}
                >
                    <LuGamepad2 />
                    {t.BUTTON_REBOOT_WITH_SAVING}
                </Button>
            </Stack>
        </Center>
    )
}