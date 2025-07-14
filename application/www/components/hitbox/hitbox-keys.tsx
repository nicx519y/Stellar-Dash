'use client';

import HitboxBase, { HitboxBaseProps } from "./hitbox-base";

interface HitboxKeysProps extends HitboxBaseProps {
    // keys页面特有的props可以在这里添加
}

/**
 * HitboxKeys - 专用于按键设置页面的Hitbox组件
 * 提供基本的点击交互功能
 */
export default function HitboxKeys(props: HitboxKeysProps) {
    return <HitboxBase {...props} />;
} 