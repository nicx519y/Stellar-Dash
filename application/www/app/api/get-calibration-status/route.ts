import { NextResponse } from 'next/server';
import { updateCalibrationProgress } from '@/app/api/data/calibration_store';

export async function GET() {
    try {
        // 获取校准状态，如果正在校准中则模拟进度更新
        const calibrationStatus = await updateCalibrationProgress();
        
        return NextResponse.json({
            errNo: 0,
            data: {
                calibrationStatus: calibrationStatus,
            },
        });
    } catch {
        return NextResponse.json({
            errNo: 1,
            errorMessage: 'Failed to fetch calibration status',
        }, { status: 500 });
    }
}

export async function POST() {
    // 支持POST方法，与前端保持一致
    return GET();
} 