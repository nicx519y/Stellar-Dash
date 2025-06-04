import { NextResponse } from "next/server";
import { getConfig, getProfileList, updateConfig } from "@/app/api/data/store";
import { GameProfile } from "@/types/gamepad-config";

export async function POST(request: Request) {
    try {
        const { profileId } = await request.json();

        if (!profileId) {
            return NextResponse.json({ errNo: 1, errorMessage: 'Profile ID is required' });
        }

        const config = await getConfig();

        if (!config.profiles || config.profiles.length <= 1) {
            return NextResponse.json({ errNo: 1, errorMessage: 'At least one profile is required' });
        }

        config.profiles = config.profiles.filter((p: GameProfile) => p.id !== profileId);

        
        config.defaultProfileId = config.profiles[0]?.id ?? "";
        await updateConfig(config);

        return NextResponse.json({
            errNo: 0,
            data: {
                profileList: await getProfileList(),
            },
        });
    } catch {
        return NextResponse.json({ errNo: 1, errorMessage: 'Failed to delete profile' }, { status: 500 });
    }
} 