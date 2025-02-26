import { NextResponse } from 'next/server';
import { getMarkingStatus } from '../data/adc_store';
import { ADCBtnsError } from '@/types/adc';

export async function GET() {
    try {
        const status = getMarkingStatus();
        if(!status) {
            return NextResponse.json({
                errNo: ADCBtnsError.MAPPING_NOT_FOUND,
                data: {
                    status: null
                }
            });
        } else {
            return NextResponse.json({
                errNo: ADCBtnsError.SUCCESS,
                data: {
                    status
                }
            });
        }
    } catch {
        return NextResponse.json(
            { errNo: 1, errorMessage : 'Internal server error' }, 
            { status: 500 }
        );
    }
} 