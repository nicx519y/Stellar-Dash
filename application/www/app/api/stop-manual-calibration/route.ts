import { NextResponse } from 'next/server';
import { stopManualCalibration } from '../data/calibration_store';

export async function POST() {
    try {
        const calibrationStatus = await stopManualCalibration();
        
        return NextResponse.json({
            errNo: 0,
            data: {
                message: "Manual calibration stopped",
                calibrationStatus: calibrationStatus,
            },
        });
    } catch {
        return NextResponse.json({
            errNo: 1,
            errorMessage: 'Failed to stop manual calibration',
        }, { status: 500 });
    }
} 