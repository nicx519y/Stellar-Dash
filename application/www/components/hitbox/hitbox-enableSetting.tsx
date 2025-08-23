'use client';

import { useEffect, useState, useRef } from "react";
import { GameControllerButton, HITBOX_BTN_POS_LIST } from "@/types/gamepad-config";
import { Box } from '@chakra-ui/react';
import styled from "styled-components";
import { useColorMode } from "../ui/color-mode";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import { AiOutlineClose, AiOutlineCheck } from "react-icons/ai";

// 样式化的 SVG 组件 - 与基类保持一致
const StyledSvg = styled.svg<{
    $scale?: number;
}>`
  width: 828.82px;
  height: 548.1px;
  padding: 20px;
  position: relative;
  transform: scale(${props => props.$scale || 1});
  transform-origin: center;
`;

// 样式化的圆形按钮 - 与基类保持一致
const StyledCircle = styled.circle<{
    $opacity?: number;
    $interactive?: boolean;
    $highlight?: boolean;
    $fillNone?: boolean;
}>`
  stroke: 'gray';
  stroke-width: 1px;
  cursor: ${props => props.$interactive ? 'pointer' : 'default'};
  pointer-events: ${props => props.$interactive ? 'auto' : 'none'};
  opacity: ${props => props.$opacity};
  stroke: ${props => props.$highlight ? 'yellowgreen' : 'gray'};
  stroke-width: ${props => props.$highlight ? '2px' : '1px'};
  filter: ${props => props.$highlight ? 'drop-shadow(0 0 2px rgba(154, 205, 50, 0.8))' : 'none'};
  fill: ${props => props.$fillNone ? 'none' : ''};

  &:hover {
    stroke-width: ${props => props.$interactive ? '2px' : '1px'};
    stroke: ${props => props.$interactive ? '#ccc' : 'gray'};
    filter: ${props => props.$interactive ? 'drop-shadow(0 0 10px rgba(204, 204, 204, 0.8))' : 'none'};
  }

  &:active {
    stroke-width: ${props => props.$interactive ? '2px' : '1px'};
    stroke: ${props => props.$interactive ? 'yellowgreen' : 'gray'};
    filter: ${props => props.$interactive ? 'drop-shadow(0 0 15px rgba(154, 205, 50, 0.9))' : 'none'};
  }
`;

// 样式化的框架 - 与基类保持一致
const StyledFrame = styled.rect`
  fill: none;
  stroke: gray;
  stroke-width: 1px;
  filter: drop-shadow(0 0 5px rgba(204, 204, 204, 0.8));
`;

// 样式化的文本 - 与基类保持一致
const StyledText = styled.text`
  text-align: center;
  font-family: "Helvetica", cursive;
  font-size: .9rem;
  cursor: default;
  pointer-events: none;
`;

const btnPosList = HITBOX_BTN_POS_LIST;
const btnFrameRadiusDistance = 3;
const btnLen = btnPosList.length;

interface HitboxEnableSettingProps {
    onClick?: (id: number) => void;
    interactiveIds?: number[];
    buttonsEnableConfig?: boolean[]; // 按键启用配置数组
    buttonLabelMap?: { [key: number]: GameControllerButton };
    containerWidth?: number; // 外部容器宽度
}

/**
 * HitboxEnableSetting - 专用于按键启用设置页面的Hitbox组件
 * 根据按键启用配置显示不同的颜色状态
 */
export default function HitboxEnableSetting({
    onClick,
    interactiveIds = [],
    buttonsEnableConfig = [],
    buttonLabelMap = {},
    containerWidth,
}: HitboxEnableSettingProps) {
    const { colorMode } = useColorMode();
    const { contextJsReady, setContextJsReady } = useGamepadConfig();
    const svgRef = useRef<SVGSVGElement>(null);
    const circleRefs = useRef<(SVGCircleElement | null)[]>([]);
    const textRefs = useRef<(SVGTextElement | null)[]>([]);
    const pressedButtonListRef = useRef(Array(btnLen).fill(-1));
    const [hoveredButton, setHoveredButton] = useState<number | null>(null);

    // 计算缩放比例
    const calculateScale = (): number => {
        if (!containerWidth) return 1;
        
        const hitboxWidth = 829; // StyledSvg的原始宽度
        const margin = 80; // 左右边距
        const availableWidth = containerWidth - (margin * 2);
        
        if (availableWidth <= 0) return 0.1; // 最小缩放比例
        
        const scale = availableWidth / hitboxWidth;
        return Math.min(scale, 1.3); // 最大不超过1.3，避免过度放大
    };

    const scale = calculateScale();

    // 初始化显示状态
    useEffect(() => {
        setContextJsReady(true);
    }, [setContextJsReady]);

    // 处理按钮点击 - 采用与基类一致的事件处理方式
    const handleClick = (event: React.MouseEvent<SVGElement>) => {
        const target = event.target as SVGElement;
        if (!target.id || !target.id.startsWith("btn-")) return;
        const id = Number(target.id.replace("btn-", ""));
        if (id === Number.NaN || !(interactiveIds?.includes(id) ?? false)) return;
        if (event.type === "mousedown") {
            onClick?.(id);
            pressedButtonListRef.current[id] = 1;
        } else if (event.type === "mouseup") {
            onClick?.(-1);
            pressedButtonListRef.current[id] = -1;
        }
    };

    const handleLeave = (event: React.MouseEvent<SVGElement>) => {
        const target = event.target as SVGElement;
        if (!target.id || !target.id.startsWith("btn-")) return;
        const id = Number(target.id.replace("btn-", ""));
        if (id === Number.NaN || !(interactiveIds?.includes(id) ?? false)) return;
        if (event.type === "mouseleave") {
            pressedButtonListRef.current[id] = -1;
            setHoveredButton(null);
        }
    }

    const handleMouseEnter = (event: React.MouseEvent<SVGElement>) => {
        const target = event.target as SVGElement;
        if (!target.id || !target.id.startsWith("btn-")) return;
        const id = Number(target.id.replace("btn-", ""));
        if (id === Number.NaN || !(interactiveIds?.includes(id) ?? false)) return;
        setHoveredButton(id);
    }

    // 获取按钮颜色
    const getButtonColor = (buttonId: number): string => {
        const isEnabled = buttonsEnableConfig[buttonId];
        const isInteractive = interactiveIds.includes(buttonId);
        
        if (!isInteractive) {
            return colorMode === "dark" ? "#333333" : "#eeeeee"; // 非交互按钮为灰色
        }
        
        return isEnabled ? "#00aa00" : "#a5a5a5"; // 启用绿色，禁用深灰色（移除悬停颜色变化）
    };

    // 获取按钮文字颜色
    const getButtonTextColor = (buttonId: number): string => {
        const isEnabled = buttonsEnableConfig[buttonId];
        const isInteractive = interactiveIds.includes(buttonId);
        
        if (!isInteractive) {
            return colorMode === "dark" ? "#666666" : "#999999"; // 非交互按钮文字颜色
        }
        
        return isEnabled ? "#ffffff" : "#d5d5d5"; // 启用时白色文字，禁用时浅灰文字
    };

    const getInnerText = (index: number, x: number, y: number): string => {
        if(index === btnLen - 1) {
            return `<tspan x="${x}" y="${y}" style="font-size: 0.6rem; font-weight: bold; fill: #fff; ">Fn</tspan>`;
        }
        const buttonLabel = buttonLabelMap[index];

        return `
            <tspan x="${x+1}" y="${y-7}" style="font-size: 0.5rem; fill: #fff; ">
                ${index + 1}
            </tspan>
            <tspan x="${x}" y="${y+6}" style="font-size: 0.5rem; font-weight: bold; fill: #fff;">
                ${buttonLabel ?? "----"}
            </tspan>
        `;
    }

    return (
        <Box display={contextJsReady ? "block" : "none"} >
            <StyledSvg 
                xmlns="http://www.w3.org/2000/svg"
                ref={svgRef}
                onMouseDown={handleClick}
                onMouseUp={handleClick}
                $scale={scale}
            >
                <title>hitbox</title>
                <StyledFrame x="0.36" y="0.36" width="787.82" height="507.1" rx="10" />

                {/* 渲染按钮外框 */}
                {btnPosList.map((item, index) => {
                    const radius = item.r + btnFrameRadiusDistance;
                    return (
                        <StyledCircle
                            id={`btn-frame-${index}`}
                            key={`frame-${index}`}
                            cx={item.x}
                            cy={item.y}
                            r={radius}
                            $interactive={false}
                            $highlight={false}
                            $fillNone={true}
                        />
                    )
                })}

                {/* 渲染按钮 */}
                {btnPosList.map((item, index) => (
                    <StyledCircle
                        ref={(el: SVGCircleElement | null) => {
                            circleRefs.current[index] = el;
                        }}
                        id={`btn-${index}`}
                        key={index}
                        cx={item.x}
                        cy={item.y}
                        r={item.r}
                        $opacity={1}
                        $interactive={interactiveIds?.includes(index) ?? false}
                        $highlight={false}
                        fill={getButtonColor(index)}
                        onMouseLeave={handleLeave}
                        onMouseEnter={handleMouseEnter}
                    />
                ))}

                {/* 渲染按钮文字 */}
                {btnPosList.map((item, index) => (
                    <StyledText
                        ref={(el: SVGTextElement | null) => {
                            textRefs.current[index] = el;
                        }}
                        textAnchor="middle"
                        dominantBaseline="middle"
                        key={index}
                        x={item.x}
                        y={index < btnLen - 4 ? item.y : item.y + 30}
                        fill={getButtonTextColor(index)}
                        dangerouslySetInnerHTML={{ __html: getInnerText(index, item.x, index < btnLen - 4 ? item.y : item.y + 30) }}
                    />
                ))}

                {/* 渲染悬停图标 */}
                {btnPosList.map((item, index) => {
                    const isEnabled = buttonsEnableConfig[index];
                    const isInteractive = interactiveIds.includes(index);
                    const isHovered = hoveredButton === index;
                    
                    if (!isInteractive || !isHovered) return null;
                    
                const IconComponent = isEnabled ? AiOutlineClose : AiOutlineCheck;
                    const iconColor = isEnabled ? "#ffffff" : "#00aa00";
                    const buttonRadius = item.r;
                    const iconSize = buttonRadius * 2.0; // 图标大小为按钮半径的1.5倍，确保覆盖整个按键
                    
                    return (
                        <foreignObject
                            key={`hover-icon-${index}`}
                            x={item.x - iconSize / 2} // 图标中心对齐按钮中心
                            y={item.y - iconSize / 2} // 图标中心对齐按钮中心
                            width={iconSize}
                            height={iconSize}
                            style={{ pointerEvents: "none" }}
                        >
                            <div style={{
                                display: "flex",
                                justifyContent: "center",
                                alignItems: "center",
                                width: "100%",
                                height: "100%",
                                color: iconColor,
                                backgroundColor: "rgba(0, 0, 0, 0.3)", // 半透明背景增强可见性
                                borderRadius: "50%", // 圆形背景
                            }}>
                                <IconComponent size={iconSize * .5} />
                            </div>
                        </foreignObject>
                    );
                })}
            </StyledSvg>
        </Box>
    );
} 