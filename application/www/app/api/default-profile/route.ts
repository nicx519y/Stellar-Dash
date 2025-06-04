import { NextResponse } from 'next/server';
import { getConfig } from '@/app/api/data/store';

export async function GET() {
    try {
        const config = await getConfig();
        if (!config.profiles || !config.defaultProfileId) {
            return NextResponse.json({ errNo: 1, errorMessage: 'No profiles or default profile ID' });
        }
        return NextResponse.json({
            errNo: 0,
            data: {
                profileDetails: config.profiles.find(p => p.id === config.defaultProfileId),
            },
        });
    } catch {
        return NextResponse.json(
            { errNo: 1, errorMessage: 'Failed to fetch default profile' },
            { status: 500 }
        );
    }
} 