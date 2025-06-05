import { NextResponse } from 'next/server';
import { stopButtonMonitoring } from '../data/button_store';

export async function GET() {
    try {
        const result = stopButtonMonitoring();
        
        return NextResponse.json({
            errNo: 0,
            data: result,
        });
    } catch {
        return NextResponse.json({
            errNo: 1,
            errorMessage: 'Failed to stop button monitoring',
        }, { status: 500 });
    }
} 