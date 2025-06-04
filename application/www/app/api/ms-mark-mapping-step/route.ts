import { NextResponse } from 'next/server';
import { getMarkingStatus, stepMarking } from '../data/adc_store';

export async function GET() {
    try {
        const error = stepMarking();
        return NextResponse.json({ errNo: error, data: {
            status: getMarkingStatus()
        } });
    } catch {
        return NextResponse.json(
            { errNo: 1, errorMessage: 'Internal server error' },
            { status: 500 }
        );
    }
} 