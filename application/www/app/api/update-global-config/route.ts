import { PlatformList } from "@/types/gamepad-config";
import { getConfig, updateConfig } from "@/app/api/data/store";
import { getCalibrationStatus } from "../data/calibration_store";
import { NextRequest, NextResponse } from "next/server";

export async function POST(request: NextRequest) {
    const { globalConfig } = await request.json();

    const { inputMode, autoCalibrationEnabled } = globalConfig;

    if(inputMode) {
        const inputModeList = PlatformList;

        if (!inputModeList.includes(inputMode)) {
            return NextResponse.json({ error: "Invalid input mode" }, { status: 400 });
        }
        
        // 更新基础配置
        await updateConfig({ inputMode });
    }
    
    // 更新校准模式配置
    if (typeof autoCalibrationEnabled === 'boolean') {
        await updateConfig({ autoCalibrationEnabled });
    }
    
    // 获取最新的校准状态
    const calibrationStatus = await getCalibrationStatus();
    const config = await getConfig();
    const currentInputMode = config.inputMode;
    const currentAutoCalibrationEnabled = config.autoCalibrationEnabled;

    return NextResponse.json({
        errNo: 0,
        data: {
            globalConfig: { 
                inputMode: currentInputMode,
                autoCalibrationEnabled: currentAutoCalibrationEnabled ?? true,
                manualCalibrationActive: calibrationStatus.isActive,
            },
        },
    });
}
