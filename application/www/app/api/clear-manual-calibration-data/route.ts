import { NextResponse } from 'next/server';
import { clearManualCalibrationData } from '../data/calibration_store';

export async function POST() {
    try {
        const calibrationStatus = await clearManualCalibrationData();
        
        return NextResponse.json({
            errNo: 0,
            data: {
                message: "Manual calibration data cleared successfully",
                calibrationStatus: calibrationStatus,
            },
        });
    } catch (error) {
        return NextResponse.json({
            errNo: 1,
            errorMessage: 'Failed to clear manual calibration data',
        }, { status: 500 });
    }
} 