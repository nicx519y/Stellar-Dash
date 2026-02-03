import { Button } from "@chakra-ui/react";
import { useLanguage } from "@/contexts/language-context";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import { LuUpload } from "react-icons/lu";
import { useRef } from "react";
import { showToast } from "@/components/ui/toaster";
import { openConfirm } from "@/components/dialog-confirm";

export function ImportConfigButton(
    props: {
        disabled?: boolean,
    }
) {
    const { t } = useLanguage();
    const { importAllConfig } = useGamepadConfig();
    const fileInputRef = useRef<HTMLInputElement>(null);

    const handleImportClick = () => {
        fileInputRef.current?.click();
    };

    const handleFileChange = async (event: React.ChangeEvent<HTMLInputElement>) => {
        const file = event.target.files?.[0];
        if (!file) return;

        try {
            const text = await file.text();
            const configData = JSON.parse(text);
            await importAllConfig(configData);
            
            const confirmed = await openConfirm({
                title: t.IMPORT_CONFIG_SUCCESS_TITLE,
                message: t.IMPORT_CONFIG_SUCCESS_MESSAGE
            });

            if (confirmed) {
                window.location.reload();
            }
        } catch (error) {
            console.error("Import failed:", error);
            showToast({
                title: t.IMPORT_CONFIG_FAILED_TITLE,
                description: t.IMPORT_CONFIG_FAILED_MESSAGE,
                type: "error",
            });
        } finally {
            // Reset input so same file can be selected again
            if (fileInputRef.current) {
                fileInputRef.current.value = '';
            }
        }
    };

    return (
        <>
            <input
                type="file"
                ref={fileInputRef}
                style={{ display: 'none' }}
                accept=".json"
                onChange={handleFileChange}
            />
            <Button
                disabled={props.disabled}
                colorPalette="blue"
                variant="surface"
                size="xs"
                // width={"120px"}
                onClick={handleImportClick}
            >
                <LuUpload />
                {t.BUTTON_IMPORT_CONFIG}
            </Button>
        </>
    )
}
