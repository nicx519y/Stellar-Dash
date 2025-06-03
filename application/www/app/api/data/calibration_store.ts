import { promises as fs } from 'fs';
import path from 'path';
import { CalibrationStatus, CalibrationButtonStatus } from '@/contexts/gamepad-config-context';

const calibrationDataFilePath = path.join(process.cwd(), 'app/api/data/calibration_data.json');

interface CalibrationData extends CalibrationStatus {
    autoCalibrationEnabled: boolean;
}

/**
 * 读取校准数据
 */
async function readCalibrationData(): Promise<CalibrationData> {
    try {
        const data = await fs.readFile(calibrationDataFilePath, 'utf8');
        return JSON.parse(data);
    } catch {
        // 如果文件不存在，返回默认数据
        return getDefaultCalibrationData();
    }
}

/**
 * 写入校准数据
 */
async function writeCalibrationData(data: CalibrationData): Promise<void> {
    await fs.writeFile(calibrationDataFilePath, JSON.stringify(data, null, 2), 'utf8');
}

/**
 * 获取默认校准数据
 */
function getDefaultCalibrationData(): CalibrationData {
    const buttons: CalibrationButtonStatus[] = [];
    for (let i = 0; i < 8; i++) {
        buttons.push({
            index: i,
            phase: 'IDLE',
            isCalibrated: false,
            topValue: 0,
            bottomValue: 0,
            ledColor: 'RED'
        });
    }
    
    return {
        isActive: false,
        uncalibratedCount: 8,
        activeCalibrationCount: 0,
        allCalibrated: false,
        autoCalibrationEnabled: true,
        buttons
    };
}

/**
 * 计算校准状态统计
 */
function calculateCalibrationStats(buttons: CalibrationButtonStatus[]): {
    uncalibratedCount: number;
    activeCalibrationCount: number;
    allCalibrated: boolean;
} {
    let uncalibratedCount = 0;
    let activeCalibrationCount = 0;
    
    buttons.forEach(button => {
        if (!button.isCalibrated) {
            uncalibratedCount++;
        }
        if (button.phase === 'TOP_SAMPLING' || button.phase === 'BOTTOM_SAMPLING') {
            activeCalibrationCount++;
        }
    });
    
    return {
        uncalibratedCount,
        activeCalibrationCount,
        allCalibrated: uncalibratedCount === 0
    };
}

/**
 * 获取校准状态
 */
export async function getCalibrationStatus(): Promise<CalibrationStatus> {
    const data = await readCalibrationData();
    const stats = calculateCalibrationStats(data.buttons);
    
    return {
        isActive: data.isActive,
        uncalibratedCount: stats.uncalibratedCount,
        activeCalibrationCount: stats.activeCalibrationCount,
        allCalibrated: stats.allCalibrated,
        buttons: data.buttons
    };
}

/**
 * 获取自动校准开关状态
 */
export async function getAutoCalibrationEnabled(): Promise<boolean> {
    const data = await readCalibrationData();
    return data.autoCalibrationEnabled;
}

/**
 * 设置自动校准开关状态
 */
export async function setAutoCalibrationEnabled(enabled: boolean): Promise<void> {
    const data = await readCalibrationData();
    data.autoCalibrationEnabled = enabled;
    await writeCalibrationData(data);
}

/**
 * 开始手动校准
 */
export async function startManualCalibration(): Promise<CalibrationStatus> {
    const data = await readCalibrationData();
    
    // 设置为激活状态
    data.isActive = true;
    
    // 重置所有未校准按钮为采样状态
    let activeCount = 0;
    data.buttons.forEach(button => {
        if (!button.isCalibrated) {
            button.phase = 'TOP_SAMPLING';
            button.ledColor = 'CYAN';
            activeCount++;
        } else {
            button.ledColor = 'GREEN';
        }
    });
    
    const stats = calculateCalibrationStats(data.buttons);
    data.uncalibratedCount = stats.uncalibratedCount;
    data.activeCalibrationCount = activeCount;
    data.allCalibrated = stats.allCalibrated;
    
    await writeCalibrationData(data);
    
    return {
        isActive: data.isActive,
        uncalibratedCount: data.uncalibratedCount,
        activeCalibrationCount: data.activeCalibrationCount,
        allCalibrated: data.allCalibrated,
        buttons: data.buttons
    };
}

/**
 * 停止手动校准
 */
export async function stopManualCalibration(): Promise<CalibrationStatus> {
    const data = await readCalibrationData();
    
    // 设置为非激活状态
    data.isActive = false;
    
    // 重置所有按钮状态
    data.buttons.forEach(button => {
        if (button.isCalibrated) {
            button.phase = 'COMPLETED';
            button.ledColor = 'GREEN';
        } else {
            button.phase = 'IDLE';
            button.ledColor = 'RED';
        }
    });
    
    const stats = calculateCalibrationStats(data.buttons);
    data.uncalibratedCount = stats.uncalibratedCount;
    data.activeCalibrationCount = 0;
    data.allCalibrated = stats.allCalibrated;
    
    await writeCalibrationData(data);
    
    return {
        isActive: data.isActive,
        uncalibratedCount: data.uncalibratedCount,
        activeCalibrationCount: data.activeCalibrationCount,
        allCalibrated: data.allCalibrated,
        buttons: data.buttons
    };
}

/**
 * 清除手动校准数据
 */
export async function clearManualCalibrationData(): Promise<CalibrationStatus> {
    const data = await readCalibrationData();
    
    // 停止校准
    data.isActive = false;
    
    // 重置所有按钮数据
    data.buttons.forEach(button => {
        button.phase = 'IDLE';
        button.isCalibrated = false;
        button.topValue = 0;
        button.bottomValue = 0;
        button.ledColor = 'RED';
    });
    
    const stats = calculateCalibrationStats(data.buttons);
    data.uncalibratedCount = stats.uncalibratedCount;
    data.activeCalibrationCount = 0;
    data.allCalibrated = stats.allCalibrated;
    
    await writeCalibrationData(data);
    
    return {
        isActive: data.isActive,
        uncalibratedCount: data.uncalibratedCount,
        activeCalibrationCount: data.activeCalibrationCount,
        allCalibrated: data.allCalibrated,
        buttons: data.buttons
    };
}

/**
 * 模拟校准进度更新（用于测试）
 */
export async function updateCalibrationProgress(): Promise<CalibrationStatus> {
    const data = await readCalibrationData();
    
    if (!data.isActive) {
        return await getCalibrationStatus();
    }
    
    // 随机更新一些按钮的状态，模拟校准进度
    data.buttons.forEach(button => {
        if (button.phase === 'TOP_SAMPLING' && Math.random() < 0.3) {
            // 30% 概率进入底部采样阶段
            button.phase = 'BOTTOM_SAMPLING';
            button.ledColor = 'DARK_BLUE';
            button.topValue = Math.floor(Math.random() * 1000) + 3000; // 模拟顶部值
        } else if (button.phase === 'BOTTOM_SAMPLING' && Math.random() < 0.4) {
            // 40% 概率完成校准
            button.phase = 'COMPLETED';
            button.isCalibrated = true;
            button.ledColor = 'GREEN';
            button.bottomValue = Math.floor(Math.random() * 500) + 100; // 模拟底部值
        }
    });
    
    const stats = calculateCalibrationStats(data.buttons);
    data.uncalibratedCount = stats.uncalibratedCount;
    data.activeCalibrationCount = stats.activeCalibrationCount;
    data.allCalibrated = stats.allCalibrated;
    
    // 移除自动关闭逻辑 - 即使所有按钮都校准完成，也保持校准状态激活
    // 只有用户手动调用停止接口才会关闭校准模式
    
    await writeCalibrationData(data);
    
    return {
        isActive: data.isActive,
        uncalibratedCount: data.uncalibratedCount,
        activeCalibrationCount: data.activeCalibrationCount,
        allCalibrated: data.allCalibrated,
        buttons: data.buttons
    };
} 