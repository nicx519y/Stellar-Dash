import { Stack, Button } from "@chakra-ui/react";
import { openConfirm as openRebootConfirmDialog } from "@/components/dialog-confirm";
import { openDialog as openRebootDialog } from "@/components/dialog-cannot-close";
import { useLanguage } from "@/contexts/language-context";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import { LuGamepad2 } from "react-icons/lu";


export function ContentActionButtons(
    props: {
        disabled?: boolean,
        resetLabel: string;
        saveLabel: string;
        isDirty: boolean;
        resetHandler: () => Promise<void>;
        saveHandler: () => Promise<void>;
    }
) {
    const { t } = useLanguage();
    const { rebootSystem } = useGamepadConfig();
    const { resetLabel, saveLabel, resetHandler, saveHandler, isDirty } = props;
    return (
        <Stack direction="row" gap={4}>
            <Button
                colorPalette="teal"
                variant="surface"
                size="md"
                width="140px"
                onClick={resetHandler}
                disabled={!isDirty || props.disabled}
            >
                {resetLabel}
            </Button>
            <Button
                colorPalette="green"
                size="md"
                width="140px"
                onClick={saveHandler}
                disabled={!isDirty || props.disabled}
            >
                {saveLabel}
            </Button>
            <Button
                disabled={props.disabled}
                colorPalette="blue"
                variant="solid"
                size="md"
                width={"340px"}
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
                <LuGamepad2 />
                {t.BUTTON_REBOOT_WITH_SAVING}
            </Button>
        </Stack>
    )
}