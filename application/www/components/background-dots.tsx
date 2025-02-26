'use client';

import { useEffect, useState } from "react";

type LightDot = {
    x: number;
    y: number;
    radius: number;
    color: string;
    opacity: number;
};

export default function BackgroundDots() {
    const [dots, setDots] = useState<LightDot[]>([]);

    const getColor = () => {
        // 随机选择绿色或蓝色
        const isGreen = Math.random() > 0.5;
        if (isGreen) {
            return `hsl(${140 + Math.random() * 20}, 85%, ${50 + Math.random() * 20}%)`; // 绿色范围
        } else {
            return `hsl(${200 + Math.random() * 20}, 85%, ${50 + Math.random() * 20}%)`; // 蓝色范围
        }
    };

    useEffect(() => {
        // 创建15个静态光点
        const newDots = Array.from({ length: 15 }, () => ({
            x: Math.random() * window.innerWidth,
            y: Math.random() * window.innerHeight,
            radius: Math.random() * 300 + 200, // 200-500px
            color: getColor(),
            opacity: Math.random() * 0.08 + 0.03, // 降低透明度范围到 0.03-0.11
        }));
        setDots(newDots);
    }, []);

    return (
        <div style={{
            position: 'fixed',
            top: 0,
            left: 0,
            width: '100vw',
            height: '100vh',
            zIndex: -1,
            overflow: 'hidden',
            background: '#000'
        }}>
            <svg width="100%" height="100%">
                <defs>
                    <filter id="glow">
                        {/* 多层模糊 */}
                        <feGaussianBlur in="SourceGraphic" stdDeviation="150" result="blur1"/>
                        {/* <feGaussianBlur in="blur1" stdDeviation="120" result="blur2"/>
                        <feGaussianBlur in="blur2" stdDeviation="90" result="blur3"/>
                        
                        {/* 柔和混合 */}
                        {/* <feBlend in="blur1" in2="blur2" mode="screen" result="blend1"/>
                        <feBlend in="blend1" in2="blur3" mode="screen" result="softBlend"/> */}
                        
                        {/* 额外的柔化 */}
                        {/* <feGaussianBlur in="softBlend" stdDeviation="60" result="finalBlur"/> */}
                        
                        {/* 轻微的颜色调整 */}
                        {/* <feColorMatrix in="finalBlur" type="saturate" values="0.7"/>  */}
                    </filter>
                </defs>
                {dots.map((dot, index) => (
                    <circle
                        key={index}
                        cx={dot.x}
                        cy={dot.y}
                        r={dot.radius}
                        fill={dot.color}
                        opacity={dot.opacity}
                        filter="url(#glow)"
                    />
                ))}
            </svg>
        </div>
    );
} 