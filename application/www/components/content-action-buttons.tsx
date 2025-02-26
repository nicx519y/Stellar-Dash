import { Stack, Button } from "@chakra-ui/react";
import { openConfirm as openRebootConfirmDialog } from "@/components/dialog-confirm";
import { openDialog as openRebootDialog } from "@/components/dialog-cannot-close";
import { useLanguage } from "@/contexts/language-context";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";


export function ContentActionButtons(
    props: {
        resetLabel: string;
        saveLabel: string;
        resetHandler: () => Promise<void>;
        saveHandler: () => Promise<void>;
    }
) {
    const { t } = useLanguage();
    const { rebootSystem } = useGamepadConfig();
    const { resetLabel, saveLabel, resetHandler, saveHandler } = props;
    return (
        <Stack direction="row" gap={4} justifyContent="flex-start" padding="32px 0px">
            <Button
                colorPalette="teal"
                variant="surface"
                size="lg"
                width="140px"
                onClick={resetHandler}
            >
                {resetLabel}
            </Button>
            <Button
                colorPalette="green"
                size="lg"
                width="140px"
                onClick={saveHandler}
            >
                {saveLabel}
            </Button>
            <Button
                colorPalette="blue"
                variant="surface"
                size={"lg"}
                width={"180px"}
                onClick={async () => {
                    const confirmed = await openRebootConfirmDialog({
                        title: t.DIALOG_REBOOT_CONFIRM_TITLE,
                        message: t.DIALOG_REBOOT_CONFIRM_MESSAGE,
                    });
                    if (confirmed) {
                        saveHandler()
                            .then(() => rebootSystem())
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
                {t.BUTTON_REBOOT_WITH_SAVING}
            </Button>
        </Stack>
    )
}