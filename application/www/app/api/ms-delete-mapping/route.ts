import { NextResponse } from 'next/server';
import { deleteMapping, getDefaultMapping, getMappingList } from '../data/adc_store';

export async function POST(request: Request) {
    try {
        const body = await request.json();
        const { id } = body;

        // 参数验证
        if (!id) {
            return NextResponse.json(
                { errNo: 1, errorMessage: 'Invalid parameters' },
                { status: 400 }
            );
        }

        const errNo = deleteMapping(id);
        const mappingList = getMappingList();
        const defaultMappingId = getDefaultMapping();
        return NextResponse.json({ errNo, data: { mappingList, defaultMappingId } });
    } catch {
        return NextResponse.json(
            { errNo: 1, errorMessage: 'Internal server error' },
            { status: 500 }
        );
    }
} 