import { NextResponse } from 'next/server';
import { getConfig } from '@/app/api/data/store';

/**
 * Get hotkeys config
 * @returns 
 */
export async function GET() {
    try {
        const config = await getConfig();
        if (!config.hotkeys) {
            return NextResponse.json({ errNo: 1, errorMessage: 'No hotkeys config' });
        }
        return NextResponse.json({
            errNo: 0,
            data: {
                hotkeysConfig: config.hotkeys,
            },
        });
    } catch {
        return NextResponse.json({ errNo: 1, errorMessage: 'Failed to fetch hotkeys config' });
    }
} 