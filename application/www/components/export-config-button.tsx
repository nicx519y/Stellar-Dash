import { Button } from "@chakra-ui/react";
import { useLanguage } from "@/contexts/language-context";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import { LuDownload } from "react-icons/lu";

export function ExportConfigButton(
    props: {
        disabled?: boolean,
    }
) {
    const { t } = useLanguage();
    const { exportAllConfig } = useGamepadConfig();

    const handleExport = async () => {
        let url = '';
        let a: HTMLAnchorElement | null = null;

        try {
            const data = await exportAllConfig();
            
            // Create JSON blob
            const jsonString = JSON.stringify(data, null, 4);
            const blob = new Blob([jsonString], { type: 'application/json' });
            
            // Create download link
            url = URL.createObjectURL(blob);
            a = document.createElement('a');
            a.href = url;
            a.download = `xbox_config_${new Date().toISOString().slice(0, 10)}.json`;
            document.body.appendChild(a);
            a.click();
            
        } catch (error) {
            console.error("Export failed:", error);
            // Optionally show error toast/dialog
        } finally {
            // Cleanup guaranteed
            if (a) {
                document.body.removeChild(a);
            }
            if (url) {
                URL.revokeObjectURL(url);
            }
        }
    };

    return (
        <Button
            disabled={props.disabled}
            colorPalette="blue"
            variant="surface"
            size="sm"
            width={"120px"}
            onClick={handleExport}
        >
            <LuDownload />
            {t.BUTTON_EXPORT_CONFIG}
        </Button>
    )
}
