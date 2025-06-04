import { NextResponse } from 'next/server';
import { renameMapping, getMappingList, getDefaultMapping } from '../data/adc_store';

export async function POST(request: Request) {
    try {
        const { id, name } = await request.json();
        const error = renameMapping(id, name);
        return NextResponse.json({ errNo: error, data: {
            mappingList: getMappingList(),
            defaultMappingId: getDefaultMapping()
        } });
    } catch {
        return NextResponse.json(
            { errNo: 1, errorMessage: 'Internal server error' },
            { status: 500 }
        );
    }
} 