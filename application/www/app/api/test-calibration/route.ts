import { NextResponse } from 'next/server';

export async function GET() {
    const baseUrl = process.env.NODE_ENV === 'development' 
        ? 'http://localhost:3000' 
        : 'https://your-domain.com';

    try {
        // 测试所有校准相关API
        const testResults = [];

        // 1. 测试获取全局配置
        try {
            const globalConfigResponse = await fetch(`${baseUrl}/api/global-config`);
            const globalConfigData = await globalConfigResponse.json();
            testResults.push({
                api: 'global-config',
                status: globalConfigResponse.ok ? 'SUCCESS' : 'FAILED',
                data: globalConfigData
            });
        } catch (error) {
            testResults.push({
                api: 'global-config',
                status: 'ERROR',
                error: String(error)
            });
        }

        // 2. 测试获取校准状态
        try {
            const calibrationStatusResponse = await fetch(`${baseUrl}/api/get-calibration-status`);
            const calibrationStatusData = await calibrationStatusResponse.json();
            testResults.push({
                api: 'get-calibration-status',
                status: calibrationStatusResponse.ok ? 'SUCCESS' : 'FAILED',
                data: calibrationStatusData
            });
        } catch (error) {
            testResults.push({
                api: 'get-calibration-status',
                status: 'ERROR',
                error: String(error)
            });
        }

        // 3. 测试开始手动校准
        try {
            const startCalibrationResponse = await fetch(`${baseUrl}/api/start-manual-calibration`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' }
            });
            const startCalibrationData = await startCalibrationResponse.json();
            testResults.push({
                api: 'start-manual-calibration',
                status: startCalibrationResponse.ok ? 'SUCCESS' : 'FAILED',
                data: startCalibrationData
            });
        } catch (error) {
            testResults.push({
                api: 'start-manual-calibration',
                status: 'ERROR',
                error: String(error)
            });
        }

        // 4. 测试停止手动校准
        try {
            const stopCalibrationResponse = await fetch(`${baseUrl}/api/stop-manual-calibration`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' }
            });
            const stopCalibrationData = await stopCalibrationResponse.json();
            testResults.push({
                api: 'stop-manual-calibration',
                status: stopCalibrationResponse.ok ? 'SUCCESS' : 'FAILED',
                data: stopCalibrationData
            });
        } catch (error) {
            testResults.push({
                api: 'stop-manual-calibration',
                status: 'ERROR',
                error: String(error)
            });
        }

        // 5. 测试清除校准数据
        try {
            const clearCalibrationResponse = await fetch(`${baseUrl}/api/clear-manual-calibration-data`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' }
            });
            const clearCalibrationData = await clearCalibrationResponse.json();
            testResults.push({
                api: 'clear-manual-calibration-data',
                status: clearCalibrationResponse.ok ? 'SUCCESS' : 'FAILED',
                data: clearCalibrationData
            });
        } catch (error) {
            testResults.push({
                api: 'clear-manual-calibration-data',
                status: 'ERROR',
                error: String(error)
            });
        }

        return NextResponse.json({
            message: '校准API测试完成',
            results: testResults,
            summary: {
                total: testResults.length,
                success: testResults.filter(r => r.status === 'SUCCESS').length,
                failed: testResults.filter(r => r.status === 'FAILED').length,
                error: testResults.filter(r => r.status === 'ERROR').length
            }
        });

    } catch (error) {
        return NextResponse.json({
            message: '测试过程中出现错误',
            error: String(error)
        }, { status: 500 });
    }
} 