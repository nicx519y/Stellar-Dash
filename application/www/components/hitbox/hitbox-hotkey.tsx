'use client';

import HitboxBase, { HitboxBaseProps } from "./hitbox-base";

interface HitboxHotkeyProps extends HitboxBaseProps {
    // hotkey页面特有的props可以在这里添加
}

/**
 * HitboxHotkey - 专用于热键设置页面的Hitbox组件
 * 提供热键设置的点击交互功能
 */
export default function HitboxHotkey(props: HitboxHotkeyProps) {
    return <HitboxBase {...props} />;
} 