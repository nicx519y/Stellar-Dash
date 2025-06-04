import { NextResponse } from 'next/server';
import { getDefaultMapping, getMappingList } from '../data/adc_store';
import { ADCBtnsError } from '@/types/adc';

export async function GET() {
    try {
        const mappings = getMappingList();
        return NextResponse.json({
            errNo: ADCBtnsError.SUCCESS,
            data: { mappingList: mappings, defaultMappingId: getDefaultMapping() }
        });
    } catch {
        return NextResponse.json(
            { errNo: 1, errorMessage: 'Internal server error' },
            { status: 500 }
        );
    }
} 