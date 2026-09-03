'use client';

import { useEffect, useRef, useState, useMemo } from "react";
import { Box } from '@chakra-ui/react';
import styled from "styled-components";
import { useColorMode } from "../ui/color-mode";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import { AiOutlineClose } from "react-icons/ai";
import { useHitboxButtonMonitor } from '@/hooks/use-hitbox-button-monitor';
import { shouldStartButtonMonitoring } from '@/lib/button-monitor-lifecycle';
import { GameControllerButton } from "@/types/gamepad-config";
import { HITBOX_WIDTH, HITBOX_HEIGHT, HITBOX_PADDING, HITBOX_LAYOUT_SCALE } from "./hitbox-constants";

// 样式化的 SVG 组件 - 与基类保持一致
const StyledSvg = styled.svg<{
    $scale?: number;
}>`
  width: ${HITBOX_WIDTH + HITBOX_PADDING * 2 + 2}px;
  height: ${HITBOX_HEIGHT + HITBOX_PADDING * 2 + 2}px;
  padding: ${HITBOX_PADDING}px;
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
    $pressed?: boolean;
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

  /* 硬件按下状态样式 */
  ${props => props.$pressed && `
    stroke: yellowgreen;
    stroke-width: 2px;
    filter: drop-shadow(0 0 15px rgba(154, 205, 50, 0.9));
  `}

  /* 鼠标按下状态样式 */
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
  font-family: 'custom_en',system-ui,sans-serif;
  cursor: default;
  pointer-events: none;
`;

const btnFrameRadiusDistance = 3;

export interface HitboxKeysProps {
    onClick?: (id: number) => void;
    interactiveIds?: number[];
    isButtonMonitoringEnabled?: boolean;
    disabledKeys?: number[];
    buttonLabelMap?: { [key: number]: GameControllerButton | string };
    containerWidth?: number; // 外部容器宽度
}

/**
 * HitboxKeys - 专用于按键设置页面的Hitbox组件
 * 提供基本的点击交互功能，支持禁用按键显示
 */
export default function HitboxKeys({
    onClick,
    interactiveIds = [],
    isButtonMonitoringEnabled = false,
    disabledKeys = [],
    buttonLabelMap = {},
    containerWidth,
}: HitboxKeysProps) {
    const { colorMode } = useColorMode();
    const {
        contextJsReady,
        setContextJsReady,
        deviceConnected,
        dataIsReady,
        hitboxLayout,
    } = useGamepadConfig();
    
    const layout = useMemo(() => {
        const rawLayout = hitboxLayout ?? [];
        return rawLayout.map(item => ({
            ...item,
            x: item.x * HITBOX_LAYOUT_SCALE,
            y: item.y * HITBOX_LAYOUT_SCALE
        }));
    }, [hitboxLayout]);
    const len = layout.length;
    const shouldMonitorButtons = shouldStartButtonMonitoring({
        enabled: isButtonMonitoringEnabled ?? false,
        deviceConnected,
        dataIsReady,
        contextJsReady,
        layoutLength: len,
    });
    
    const svgRef = useRef<SVGSVGElement>(null);
    const circleRefs = useRef<(SVGCircleElement | null)[]>([]);
    const textRefs = useRef<(SVGTextElement | null)[]>([]);

    const [pressedButtonStates, setPressedButtonStates] = useState(Array(len).fill(-1));
    const hardwareButtonStates = useHitboxButtonMonitor({
        buttonCount: len,
        interactiveIds,
        disabledIds: disabledKeys,
        enabled: shouldMonitorButtons,
        onButtonChange: onClick,
        logPrefix: 'hitbox-keys',
    });

    // 当 layout 长度变化时，重置状态数组
    useEffect(() => {
        setPressedButtonStates(Array(len).fill(-1));
    }, [len]);

    // 计算缩放比例
    const calculateScale = (): number => {
        if (!containerWidth) return 1;
        
        const margin = 80; // 左右边距
        const availableWidth = containerWidth - (margin * 2);
        
        if (availableWidth <= 0) return 0.1; // 最小缩放比例
        
        const scale = availableWidth / HITBOX_WIDTH;
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
        // 禁用的按键不能点击
        if (disabledKeys.includes(id)) return;
        
        if (event.type === "mousedown") {
            onClick?.(id);
            setPressedButtonStates(prev => {
                const newStates = [...prev];
                newStates[id] = 1;
                return newStates;
            });
        } else if (event.type === "mouseup") {
            onClick?.(-1);
            setPressedButtonStates(prev => {
                const newStates = [...prev];
                newStates[id] = -1;
                return newStates;
            });
        }
    };

    const handleLeave = (event: React.MouseEvent<SVGElement>) => {
        const target = event.target as SVGElement;
        if (!target.id || !target.id.startsWith("btn-")) return;
        const id = Number(target.id.replace("btn-", ""));
        if (id === Number.NaN || !(interactiveIds?.includes(id) ?? false)) return;
        if (event.type === "mouseleave") {
            setPressedButtonStates(prev => {
                const newStates = [...prev];
                newStates[id] = -1;
                return newStates;
            });
        }
    }

    // 获取按钮颜色
    const getButtonColor = (buttonId: number): string => {
        const isDisabled = disabledKeys.includes(buttonId);
        const isInteractive = interactiveIds.includes(buttonId);
        
        if (isDisabled) {
            return "#ff444400"; // 禁用按键为红色
        }
        
        if (!isInteractive) {
            return colorMode === "dark" ? "#333333" : "#eeeeee"; // 非交互按钮为灰色
        }
        
        return colorMode === "dark" ? "#000" : "#fff"; // 默认颜色
    };

    // 判断按键是否可交互（既要在交互列表中，又不能在禁用列表中）
    const isButtonInteractive = (buttonId: number): boolean => {
        return interactiveIds.includes(buttonId) && !disabledKeys.includes(buttonId);
    };

    // 判断按键是否处于按下状态（鼠标或硬件按键）
    const isButtonPressed = (index: number): boolean => {
        return (hardwareButtonStates[index] === 1 || pressedButtonStates[index] === 1) || false;
    };

    const getInnerText = (index: number, x: number, y: number): string => {
        if(index === len - 1) {
            return `<tspan x="${x}" y="${y}" style="font-size: 0.6rem; font-weight: bold; fill: #fff; ">Fn</tspan>`;
        }
        const buttonLabel = buttonLabelMap[index];

        return `
            <tspan x="${x+1}" y="${y-7}" style="font-size: 0.5rem; fill: #999; ">
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
                <StyledFrame x="0.36" y="0.36" width={HITBOX_WIDTH} height={HITBOX_HEIGHT} rx="10" />

                {/* 渲染按钮外框 */}
                {layout.map((item: { x: number, y: number, r: number }, index: number) => {
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
                            $pressed={false}
                        />
                    )
                })}

                {/* 渲染按钮 */}
                {layout.map((item: { x: number, y: number, r: number }, index: number) => (
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
                        $pressed={isButtonPressed(index)}
                        fill={getButtonColor(index)}
                        onMouseLeave={handleLeave}
                    />
                ))}

                {/* 渲染按钮文字 */}
                {layout.map((item: { x: number, y: number, r: number }, index: number) => (
                    <StyledText
                        ref={(el: SVGTextElement | null) => {
                            textRefs.current[index] = el;
                        }}
                        textAnchor="middle"
                        dominantBaseline="middle"
                        key={index}
                        x={item.x}
                        y={index < len - 4 ? item.y : item.y + 30}
                        dangerouslySetInnerHTML={{ __html: getInnerText(index, item.x, index < len - 4 ? item.y : item.y + 30) }}
                    />
                ))}

                {/* 渲染禁用按键的 X 图标 */}
                {layout.map((item: { x: number, y: number, r: number }, index: number) => {
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
