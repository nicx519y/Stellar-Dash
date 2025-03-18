import { NextResponse } from 'next/server';
import { getConfig } from '../data/store';

export async function GET() {
    const config = await getConfig();
    if(!config.inputMode) {
        return NextResponse.json({
            errNo: 1,
            errorMessage: 'No input mode',
        });
    }
    return NextResponse.json({
        errNo: 0,
        data: {
            globalConfig: {
                inputMode: config.inputMode,
            },
        },
    });
}


