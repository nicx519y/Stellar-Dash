import { NextResponse } from 'next/server';
import { getButtonStates } from '../data/button_store';

export async function GET() {
    try {
        const result = getButtonStates();
        
        return NextResponse.json({
            errNo: 0,
            data: result,
        });
    } catch {
        return NextResponse.json({
            errNo: 1,
            errorMessage: 'Failed to get button states',
        }, { status: 500 });
    }
} 