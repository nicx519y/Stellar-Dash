import { NextResponse } from 'next/server';
import { startButtonMonitoring } from '../data/button_store';

export async function GET() {
    try {
        const result = startButtonMonitoring();
        
        return NextResponse.json({
            errNo: 0,
            data: result,
        });
    } catch {
        return NextResponse.json({
            errNo: 1,
            errorMessage: 'Failed to start button monitoring',
        }, { status: 500 });
    }
} 