import { NextResponse } from 'next/server';

export async function POST() {
    try {
        // TODO: 调用系统重启命令
        // 这里需要根据实际系统实现重启功能
        
        return NextResponse.json({
            errNo: 0,
            data: {
                message: 'System is rebooting'
            },
        });
    } catch {
        return NextResponse.json({ 
            errNo: 1, 
            errorMessage: 'Failed to reboot system' 
        }, { 
            status: 500 
        });
    }
} 