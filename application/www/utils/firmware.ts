import { 
    FirmwareMetadata, 
    FirmwareComponent, 
    VersionComparisonResult,
    ReleasePackageManifest,
    SlotInfo
} from '@/types/firmware';

/**
 * 固件元数据相关的实用工具函数
 */

/**
 * 解析版本字符串为版本对象
 * @param versionString 版本字符串 (如 "v1.2.3" 或 "1.2.3")
 * @returns 版本对象
 */
export function parseVersion(versionString: string) {
    const cleanVersion = versionString.replace(/^v/, '');
    const parts = cleanVersion.split('.');
    
    return {
        major: parseInt(parts[0] || '0', 10),
        minor: parseInt(parts[1] || '0', 10),
        patch: parseInt(parts[2] || '0', 10),
        original: versionString
    };
}

/**
 * 比较两个版本
 * @param version1 版本1
 * @param version2 版本2
 * @returns 比较结果
 */
export function compareVersions(version1: string, version2: string): VersionComparisonResult {
    const v1 = parseVersion(version1);
    const v2 = parseVersion(version2);
    
    const majorDiff = v2.major - v1.major;
    const minorDiff = v2.minor - v1.minor;
    const patchDiff = v2.patch - v1.patch;
    
    const isNewer = majorDiff > 0 || 
                   (majorDiff === 0 && minorDiff > 0) || 
                   (majorDiff === 0 && minorDiff === 0 && patchDiff > 0);
    
    const isDifferent = majorDiff !== 0 || minorDiff !== 0 || patchDiff !== 0;
    
    return {
        isNewer,
        isDifferent,
        majorDiff,
        minorDiff,
        patchDiff
    };
}

/**
 * 验证组件完整性
 * @param component 组件信息
 * @param actualChecksum 实际校验和
 * @returns 是否验证通过
 */
export function verifyComponent(component: FirmwareComponent, actualChecksum?: string): boolean {
    if (!actualChecksum) {
        return false;
    }
    return component.sha256.toLowerCase() === actualChecksum.toLowerCase();
}

/**
 * 验证发版包manifest完整性
 * @param manifest manifest信息
 * @returns 验证结果
 */
export function validateManifest(manifest: ReleasePackageManifest): {
    isValid: boolean;
    errors: string[];
} {
    const errors: string[] = [];
    
    // 检查必需字段
    if (!manifest.version) {
        errors.push('缺少版本信息');
    }
    
    if (!manifest.slot || !['A', 'B'].includes(manifest.slot)) {
        errors.push('无效的槽位信息');
    }
    
    if (!manifest.build_date) {
        errors.push('缺少构建日期');
    }
    
    if (!manifest.components || !Array.isArray(manifest.components)) {
        errors.push('缺少组件信息');
    } else {
        // 检查每个组件
        manifest.components.forEach((component, index) => {
            if (!component.name) {
                errors.push(`组件${index + 1}缺少名称`);
            }
            if (!component.file) {
                errors.push(`组件${index + 1}缺少文件名`);
            }
            if (!component.address) {
                errors.push(`组件${index + 1}缺少地址信息`);
            }
            if (!component.size || component.size <= 0) {
                errors.push(`组件${index + 1}大小无效`);
            }
            if (!component.sha256) {
                errors.push(`组件${index + 1}缺少校验和`);
            }
        });
        
        // 检查必需组件
        const componentNames = manifest.components.map(c => c.name);
        if (!componentNames.includes('application')) {
            errors.push('缺少必需的application组件');
        }
    }
    
    return {
        isValid: errors.length === 0,
        errors
    };
}

/**
 * 计算槽位地址偏移
 * @param slotName 槽位名称
 * @param componentName 组件名称
 * @returns 地址信息
 */
export function calculateSlotAddress(slotName: 'A' | 'B', componentName: string): {
    baseAddress: string;
    componentAddress: string;
} {
    const slotBaseAddresses = {
        A: 0x90000000,
        B: 0x90300000
    };
    
    const componentOffsets: Record<string, number> = {
        application: 0x00000000,    // +0MB
        webresources: 0x00100000,  // +1MB
        adc_mapping: 0x00280000    // +2.5MB
    };
    
    const baseAddress = slotBaseAddresses[slotName];
    const offset = componentOffsets[componentName] || 0;
    const componentAddress = baseAddress + offset;
    
    return {
        baseAddress: `0x${baseAddress.toString(16).toUpperCase()}`,
        componentAddress: `0x${componentAddress.toString(16).toUpperCase()}`
    };
}

/**
 * 格式化文件大小
 * @param bytes 字节数
 * @returns 格式化后的大小字符串
 */
export function formatFileSize(bytes: number): string {
    const units = ['B', 'KB', 'MB', 'GB'];
    let size = bytes;
    let unitIndex = 0;
    
    while (size >= 1024 && unitIndex < units.length - 1) {
        size /= 1024;
        unitIndex++;
    }
    
    return `${size.toFixed(unitIndex === 0 ? 0 : 1)} ${units[unitIndex]}`;
}

/**
 * 格式化构建日期
 * @param buildDate 构建日期字符串
 * @returns 格式化后的日期
 */
export function formatBuildDate(buildDate: string): string {
    try {
        const date = new Date(buildDate);
        return date.toLocaleString('zh-CN', {
            year: 'numeric',
            month: '2-digit',
            day: '2-digit',
            hour: '2-digit',
            minute: '2-digit',
            second: '2-digit'
        });
    } catch {
        return buildDate;
    }
}

/**
 * 获取槽位状态描述
 * @param slotInfo 槽位信息
 * @returns 状态描述
 */
export function getSlotStatusDescription(slotInfo: SlotInfo): string {
    switch (slotInfo.status) {
        case 'active':
            return `运行中 (${slotInfo.version})`;
        case 'inactive':
            return `待机 (${slotInfo.version})`;
        case 'corrupted':
            return '已损坏';
        case 'empty':
            return '空闲';
        default:
            return '未知状态';
    }
}

/**
 * 检查组件兼容性
 * @param requiredComponents 必需组件列表
 * @param availableComponents 可用组件列表
 * @returns 兼容性检查结果
 */
export function checkComponentCompatibility(
    requiredComponents: string[],
    availableComponents: FirmwareComponent[]
): {
    isCompatible: boolean;
    missingComponents: string[];
    extraComponents: string[];
} {
    const availableNames = availableComponents.map(c => c.name);
    const missingComponents = requiredComponents.filter(name => !availableNames.includes(name));
    const extraComponents = availableNames.filter(name => !requiredComponents.includes(name));
    
    return {
        isCompatible: missingComponents.length === 0,
        missingComponents,
        extraComponents
    };
}

/**
 * 生成固件摘要信息
 * @param metadata 固件元数据
 * @returns 摘要信息
 */
export function generateFirmwareSummary(metadata: FirmwareMetadata): {
    totalSize: number;
    componentCount: number;
    summary: string;
} {
    const totalSize = metadata.components.reduce((sum, component) => sum + component.size, 0);
    const componentCount = metadata.components.length;
    
    const summary = [
        `版本: ${metadata.version}`,
        `槽位: ${metadata.slot}`,
        `组件: ${componentCount}个`,
        `总大小: ${formatFileSize(totalSize)}`,
        `构建时间: ${formatBuildDate(metadata.build_date)}`
    ].join(', ');
    
    return {
        totalSize,
        componentCount,
        summary
    };
}

/**
 * 检查是否可以进行跨槽刷写
 * @param fromSlot 源槽位
 * @param toSlot 目标槽位
 * @param metadata 固件元数据
 * @returns 是否可以跨槽刷写
 */
export function canCrossSlotFlash(
    fromSlot: 'A' | 'B',
    toSlot: 'A' | 'B',
    metadata: FirmwareMetadata
): boolean {
    // 不能向当前运行的槽位刷写
    if (toSlot === metadata.slot) {
        return false;
    }
    
    // 检查是否支持跨槽刷写功能
    if (!metadata.features.dualSlot.crossSlotFlashing) {
        return false;
    }
    
    return true;
} 