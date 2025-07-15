'use client';

import HitboxBase, { HitboxBaseProps } from "./hitbox-base";

/**
 * HitboxHotkey - 专用于热键设置页面的Hitbox组件
 * 提供热键设置的点击交互功能
 */
export default function HitboxHotkey(props: HitboxBaseProps) {
    return <HitboxBase {...props} />;
} 