import { NextResponse } from 'next/server';
import { getConfig } from '../data/store';
import { getAutoCalibrationEnabled, getCalibrationStatus } from '../data/calibration_store';

export async function GET() {
    const config = await getConfig();
    if(!config.inputMode) {
        return NextResponse.json({
            errNo: 1,
            errorMessage: 'No input mode',
        });
    }
    
    // 获取校准相关状态
    const autoCalibrationEnabled = await getAutoCalibrationEnabled();
    const calibrationStatus = await getCalibrationStatus();
    
    return NextResponse.json({
        errNo: 0,
        data: {
            globalConfig: {
                inputMode: config.inputMode,
                autoCalibrationEnabled: autoCalibrationEnabled,
                manualCalibrationActive: calibrationStatus.isActive,
            },
        },
    });
}


