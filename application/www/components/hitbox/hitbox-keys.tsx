'use client';

import { useEffect, useRef } from "react";
import { HITBOX_BTN_POS_LIST } from "@/types/gamepad-config";
import { Box } from '@chakra-ui/react';
import styled from "styled-components";
import { useColorMode } from "../ui/color-mode";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import { AiOutlineClose } from "react-icons/ai";

// 样式化的 SVG 组件 - 与基类保持一致
const StyledSvg = styled.svg`
  width: 828.82px;
  height: 548.1px;
  padding: 20px;
  position: relative;
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

export interface HitboxKeysProps {
    onClick?: (id: number) => void;
    hasText?: boolean;
    interactiveIds?: number[];
    highlightIds?: number[];
    disabledKeys?: number[];
    className?: string;
}

/**
 * HitboxKeys - 专用于按键设置页面的Hitbox组件
 * 提供基本的点击交互功能，支持禁用按键显示
 */
export default function HitboxKeys({
    onClick,
    hasText = true,
    interactiveIds = [],
    highlightIds = [],
    disabledKeys = [],
    className,
}: HitboxKeysProps) {
    const { colorMode } = useColorMode();
    const { contextJsReady, setContextJsReady } = useGamepadConfig();
    const svgRef = useRef<SVGSVGElement>(null);
    const circleRefs = useRef<(SVGCircleElement | null)[]>([]);
    const textRefs = useRef<(SVGTextElement | null)[]>([]);
    const pressedButtonListRef = useRef(Array(btnLen).fill(-1));

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
        // 禁用的按键不能点击
        if (disabledKeys.includes(id)) return;
        
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
        }
    }

    // 获取按钮颜色
    const getButtonColor = (buttonId: number): string => {
        const isDisabled = disabledKeys.includes(buttonId);
        const isInteractive = interactiveIds.includes(buttonId);
        const isHighlighted = highlightIds.includes(buttonId);
        
        if (isDisabled) {
            return "#ff444400"; // 禁用按键为红色
        }
        
        if (isHighlighted) {
            return "#9acd32"; // 高亮按键为黄绿色
        }
        
        if (!isInteractive) {
            return colorMode === "dark" ? "#333333" : "#eeeeee"; // 非交互按钮为灰色
        }
        
        return colorMode === "dark" ? "#000" : "#fff"; // 默认颜色
    };

    // 获取按钮文字颜色
    const getButtonTextColor = (buttonId: number): string => {
        const isDisabled = disabledKeys.includes(buttonId);
        const isInteractive = interactiveIds.includes(buttonId);
        
        if (isDisabled) {
            return colorMode === "dark" ? "#ffffff" : "#333333"; // 禁用按键为白色文字
        }
        
        if (!isInteractive) {
            return colorMode === "dark" ? "#666666" : "#999999"; // 非交互按钮文字颜色
        }
        
        return colorMode === "dark" ? "#ffffff" : "#333333"; // 默认文字颜色
    };

    // 判断按键是否可交互（既要在交互列表中，又不能在禁用列表中）
    const isButtonInteractive = (buttonId: number): boolean => {
        return interactiveIds.includes(buttonId) && !disabledKeys.includes(buttonId);
    };

    return (
        <Box display={contextJsReady ? "block" : "none"} className={className}>
            <StyledSvg xmlns="http://www.w3.org/2000/svg"
                ref={svgRef}
                onMouseDown={handleClick}
                onMouseUp={handleClick}
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
                        $interactive={isButtonInteractive(index)}
                        $highlight={highlightIds?.includes(index) ?? false}
                        fill={getButtonColor(index)}
                        onMouseLeave={handleLeave}
                    />
                ))}

                {/* 渲染按钮文字 */}
                {hasText && btnPosList.map((item, index) => (
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
                    >
                        {index !== btnLen - 1 ? index + 1 : "Fn"}
                    </StyledText>
                ))}

                {/* 渲染禁用按键的 X 图标 */}
                {btnPosList.map((item, index) => {
                    const isDisabled = disabledKeys.includes(index);
                    
                    if (!isDisabled) return null;
                    
                    const buttonRadius = item.r;
                    const iconSize = buttonRadius * 2.0; // 图标大小
                    
                    return (
                        <foreignObject
                            key={`disabled-icon-${index}`}
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
                                color: colorMode === "dark" ? "#ffffff" : "#333333",
                            }}>
                                <AiOutlineClose size={iconSize * .5} />
                            </div>
                        </foreignObject>
                    );
                })}
            </StyledSvg>
        </Box>
    );
} 