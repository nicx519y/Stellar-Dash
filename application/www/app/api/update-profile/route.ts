import { NextResponse } from 'next/server';
import { getConfig, getProfileDetails, updateConfig } from '@/app/api/data/store';
import { GameProfile } from '@/types/gamepad-config';

/**
 * Update profile config
 * @param request 
 * @returns 
 */
export async function POST(request: Request) {
    try {
        const { profileId, profileDetails }: { profileId: string, profileDetails: GameProfile } = await request.json();
        
        const config = await getConfig();
        if (!config.profiles) return NextResponse.json({ error: 'No profiles found' });

        let profile = config.profiles.find(profile => profile.id === profileId);
        if (!profile) return NextResponse.json({ error: 'Profile not found' });

        profile = {
            ...profile,
            ...profileDetails,
        };

        if (profileDetails.keysConfig) {
            profile.keysConfig = {
                ...profile.keysConfig,
                ...profileDetails.keysConfig,
            };
        }

        if (profileDetails.triggerConfigs) {
            profile.triggerConfigs = {
                ...profile.triggerConfigs,
                ...profileDetails.triggerConfigs,
            };
        }

        if (profileDetails.ledsConfigs) {
            profile.ledsConfigs = {
                ...profile.ledsConfigs,
                ...profileDetails.ledsConfigs,
            };
        }

        config.profiles = config.profiles.map(p => p.id === profileId ? profile : p);
        await updateConfig(config);

        return NextResponse.json({
            errNo: 0,
            data: {
                profileDetails: await getProfileDetails(profileId),
            },
        });
    } catch {
        return NextResponse.json({ errNo: 1, errorMessage: 'Failed to update config' }, { status: 500 });
    }
} 