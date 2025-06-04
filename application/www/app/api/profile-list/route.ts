import { NextResponse } from 'next/server';
import { getConfig } from '@/app/api/data/store';

export async function GET() {
    try {
        const config = await getConfig();
        return NextResponse.json({
            errNo: 0,
            data: {
                profileList: {
                    items: config.profiles,
                    defaultId: config.defaultProfileId,
                },
            },
        });
    } catch {
        return NextResponse.json(
            { errNo: 1, errorMessage: 'Failed to fetch profile list' },
            { status: 500 }
        );
    }
} 