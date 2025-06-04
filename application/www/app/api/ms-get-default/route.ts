import { NextResponse } from 'next/server';
import { getDefaultMapping } from '../data/adc_store';
import { ADCBtnsError } from '@/types/adc';

export async function GET() {
    try {
        const id = getDefaultMapping();
        if(!id) {
            return NextResponse.json({
                errNo: ADCBtnsError.SUCCESS,
                data: { 
                    id: ""
                 }
            });
        } else {
            return NextResponse.json({
                errNo: ADCBtnsError.SUCCESS,
                data: { 
                    id
                }
            });
        }
    } catch {
        return NextResponse.json(
            { errNo: 1, errorMessage: 'Internal server error' },
            { status: 500 }
        );
    }
} 