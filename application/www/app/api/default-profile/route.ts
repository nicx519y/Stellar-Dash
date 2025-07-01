import { NextResponse } from 'next/server';
import { getConfig } from '@/app/api/data/store';

export async function GET() {
    try {
        const config = await getConfig();
        if (!config.profiles || !config.defaultProfileId) {
            return NextResponse.json({ errNo: 1, errorMessage: 'No profiles or default profile ID' });
        }

        const defaultProfile = config.profiles.find(p => p.id === config.defaultProfileId);
        if (!defaultProfile) {
            return NextResponse.json({ errNo: 1, errorMessage: 'Default profile not found' });
        }

        if (defaultProfile.ledsConfigs) { // 默认包含氛围灯功能
            defaultProfile.ledsConfigs.hasAroundLed = true;
        }

        return NextResponse.json({
            errNo: 0,
            data: {
                profileDetails: defaultProfile,
            },
        });
    } catch {
        return NextResponse.json(
            { errNo: 1, errorMessage: 'Failed to fetch default profile' },
            { status: 500 }
        );
    }
} 