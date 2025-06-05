import { NextResponse } from 'next/server';
import { startManualCalibration } from '../data/calibration_store';

export async function GET() {
    try {
        const calibrationStatus = await startManualCalibration();
        
        return NextResponse.json({
            errNo: 0,
            data: {
                message: "Manual calibration started",
                calibrationStatus: calibrationStatus,
            },
        });
    } catch {
        return NextResponse.json({
            errNo: 1,
            errorMessage: 'Failed to start manual calibration',
        }, { status: 500 });
    }
}

export async function POST() {
    try {
        const calibrationStatus = await startManualCalibration();
        
        return NextResponse.json({
            errNo: 0,
            data: {
                message: "Manual calibration started",
                calibrationStatus: calibrationStatus,
            },
        });
    } catch {
        return NextResponse.json({
            errNo: 1,
            errorMessage: 'Failed to start manual calibration',
        }, { status: 500 });
    }
} 