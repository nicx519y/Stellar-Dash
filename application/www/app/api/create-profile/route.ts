import { NextResponse } from "next/server";
import { getConfig, getProfileList, getInitialProfileDetails, updateConfig } from "@/app/api/data/store";

export async function POST(request: Request) {
    try {
        const { profileName } = await request.json();
        const newProfileId = `profile-${Date.now()}`;

        if (!profileName || profileName === "") {
            return NextResponse.json({ errNo: 1, errorMessage: 'Profile name is required' });
        }

        const config = await getConfig();
        if (!config.profiles || config.profiles.length >= (config.numProfilesMax ?? 0)) {
            return NextResponse.json({ errNo: 1, errorMessage: 'Max number of profiles reached' });
        }

        const newProfile = await getInitialProfileDetails(newProfileId, profileName);
        config.profiles.push(newProfile);
        config.defaultProfileId = newProfileId;
        await updateConfig(config);

        return NextResponse.json({
            errNo: 0,
            data: {
                profileList: await getProfileList(),
            },
        });
    } catch {
        return NextResponse.json({ errNo: 1, errorMessage: 'Failed to add profile' }, { status: 500 });
    }
} 