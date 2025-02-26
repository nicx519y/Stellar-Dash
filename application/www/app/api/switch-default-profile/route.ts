import { NextResponse } from 'next/server';
import { getConfig, getProfileDetails, getProfileList, updateConfig } from '@/app/api/data/store';

export async function POST(request: Request) {
    try {
        const { profileId }: { profileId: string } = await request.json();

        if (!profileId || profileId === "") return NextResponse.json({ errNo: 1, errorMessage: 'Profile ID is required' });

        const config = await getConfig();
        if (!config.profiles || config.profiles.length === 0 || !getProfileDetails(profileId)) {
            return NextResponse.json({ errNo: 1, errorMessage: 'No profiles found' });
        }
        
        config.defaultProfileId = profileId;
        await updateConfig(config);

        return NextResponse.json({
            errNo: 0,
            data: {
                profileList: await getProfileList(),
            },
        });
    } catch {
        return NextResponse.json({ errNo: 1, errorMessage: 'Failed to switch default profile' }, { status: 500 });
    }
} 