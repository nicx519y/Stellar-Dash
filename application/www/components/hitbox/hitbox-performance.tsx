'use client';

import HitboxBase, { HitboxBaseProps } from "./hitbox-base";

interface HitboxPerformanceProps extends HitboxBaseProps {
    // performance页面特有的props可以在这里添加
}

/**
 * HitboxPerformance - 专用于按钮性能设置页面的Hitbox组件
 * 提供按钮选择和高亮显示功能
 */
export default function HitboxPerformance(props: HitboxPerformanceProps) {
    return <HitboxBase {...props} />;
} 