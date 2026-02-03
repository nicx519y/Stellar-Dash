import { Button } from "@chakra-ui/react";
import { useLanguage } from "@/contexts/language-context";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import { LuDownload } from "react-icons/lu";
import { showToast } from "@/components/ui/toaster";

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
            a.download = `StDashHitbox_Config_${formatCurrentTimeToYmdHms()}.json`;
            document.body.appendChild(a);
            a.click();
            
        } catch (error) {
            console.error("Export failed:", error);
            showToast({
                title: t.EXPORT_CONFIG_FAILED_TITLE,
                description: t.EXPORT_CONFIG_FAILED_MESSAGE,
                type: "error",
            });
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

    function formatCurrentTimeToYmdHms() {
        // 1. 获取当前时间的Date对象
        const now = new Date();
        
        // 2. 提取各时间部分，注意补全前置0（关键：不足两位的数字前面加0）
        const year = now.getFullYear(); // 四位年份（如2026，不是26）
        const month = String(now.getMonth() + 1).padStart(2, '0'); // 月份：0-11 → 1-12，补0
        const day = String(now.getDate()).padStart(2, '0'); // 日期：1-31，补0
        const hours = String(now.getHours()).padStart(2, '0'); // 小时：0-23，补0
        const minutes = String(now.getMinutes()).padStart(2, '0'); // 分钟：0-59，补0
        const seconds = String(now.getSeconds()).padStart(2, '0'); // 秒：0-59，补0
        
        // 3. 拼接成目标格式并返回
        return `${year}${month}${day}${hours}${minutes}${seconds}`;
    }

    return (
        <Button
            disabled={props.disabled}
            colorPalette="blue"
            variant="surface"
            size="xs"
            // width={"120px"}
            onClick={handleExport}
        >
            <LuDownload />
            {t.BUTTON_EXPORT_CONFIG}
        </Button>
    )
}
