import { PlatformList } from "@/types/gamepad-config";
import { updateConfig } from "@/app/api/data/store";
import { NextRequest, NextResponse } from "next/server";

export async function POST(request: NextRequest) {
    const { globalConfig } = await request.json();

    const { inputMode } = globalConfig;

    if (!inputMode) {
        return NextResponse.json({ error: "Input mode is required" }, { status: 400 });
    }
    
    const inputModeList = PlatformList;

    if (!inputModeList.includes(inputMode)) {
        return NextResponse.json({ error: "Invalid input mode" }, { status: 400 });
    }
    
    await updateConfig(globalConfig);

    return NextResponse.json({
        errNo: 0,
        data: {
            globalConfig: { inputMode },
        },
    });
}
