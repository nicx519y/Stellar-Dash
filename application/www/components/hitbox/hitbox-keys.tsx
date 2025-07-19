'use client';

import { useEffect, useRef, useState, useMemo } from "react";
import { HITBOX_BTN_POS_LIST } from "@/types/gamepad-config";
import { Box } from '@chakra-ui/react';
import styled from "styled-components";
import { useColorMode } from "../ui/color-mode";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import { AiOutlineClose } from "react-icons/ai";
import { type ButtonStateBinaryData, isButtonTriggered } from '@/lib/button-binary-parser';
import { useButtonMonitor } from '@/hooks/use-button-monitor';

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
    isButtonMonitoringEnabled?: boolean;
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
    isButtonMonitoringEnabled = false,
    highlightIds = [],
    disabledKeys = [],
    className,
}: HitboxKeysProps) {
    const { colorMode } = useColorMode();
    const { contextJsReady, setContextJsReady, wsConnected } = useGamepadConfig();
    
    const svgRef = useRef<SVGSVGElement>(null);
    const circleRefs = useRef<(SVGCircleElement | null)[]>([]);
    const textRefs = useRef<(SVGTextElement | null)[]>([]);
    
    // 用于跟踪软件按键状态的state和ref
    const pressedButtonListRef = useRef(Array(btnLen).fill(-1));
    // 用于跟踪硬件按键状态的state和ref
    const hardwareButtonStatesRef = useRef(Array(btnLen).fill(-1));

    const disabledKeysRef = useRef(disabledKeys);

    const [pressedButtonStates, setPressedButtonStates] = useState(Array(btnLen).fill(-1));
    const [hardwareButtonStates, setHardwareButtonStates] = useState(Array(btnLen).fill(-1));

    pressedButtonListRef.current = useMemo(() => pressedButtonStates, [pressedButtonStates]);
    hardwareButtonStatesRef.current = useMemo(() => hardwareButtonStates, [hardwareButtonStates]);
    disabledKeysRef.current = useMemo(() => disabledKeys, [disabledKeys]);

    // 使用 useButtonMonitor hook
    const { startMonitoring, stopMonitoring } = useButtonMonitor({
        autoInitialize: false, // 不自动初始化，由组件控制
        onButtonStatesChange: (data: ButtonStateBinaryData) => {
            if (!data || !interactiveIds) {
                return;
            }

            // 检查每个在interactiveIds范围内的按键
            interactiveIds.forEach((buttonId) => {

                // 禁用的按键不处理
                if(disabledKeysRef.current.includes(buttonId)) {
                    return;
                }

                const isPressed = isButtonTriggered(data.triggerMask, buttonId);
                
                // 使用ref获取当前真实状态，避免闭包陷阱
                const wasPressed = (hardwareButtonStatesRef.current[buttonId] === 1) || false;
                if (isPressed !== wasPressed) {
                    if (isPressed) {
                        // 按键按下，模拟mousedown
                        onClick?.(buttonId);
                        setHardwareButtonStates(prev => {
                            const newStates = [...prev];
                            newStates[buttonId] = 1;
                            return newStates;
                        });
                        console.log(`hitbox-keys: 硬件按键 ${buttonId} 按下`);
                    } else {
                        // 按键释放，模拟mouseup
                        onClick?.(-1);
                        setHardwareButtonStates(prev => {
                            const newStates = [...prev];
                            newStates[buttonId] = -1;
                            return newStates;
                        });
                        console.log(`hitbox-keys: 硬件按键 ${buttonId} 释放`);
                    }
                }
            });
        },
        onError: (error) => {
            console.error('hitbox-keys: 按键监听错误:', error);
        },
        onMonitoringStateChange: (isActive) => {
            console.log('hitbox-keys: 按键监听状态变化:', isActive);
        }
    });

    // 初始化显示状态
    useEffect(() => {
        setContextJsReady(true);
    }, [setContextJsReady]);

    /**
     * 管理按键监听的启动和停止
     */
    useEffect(() => {
        const enabled = isButtonMonitoringEnabled ?? false;

        if(wsConnected && contextJsReady) {
            if(enabled) {
                startMonitoring().catch((error) => {
                    console.error('启动按键监听失败:', error);
                });
            }

            if(!enabled) {
                setHardwareButtonStates(Array(btnLen).fill(-1));
                stopMonitoring();
            }
        }

        // 清理函数
        return () => {
            setHardwareButtonStates(Array(btnLen).fill(-1));
            if(wsConnected && contextJsReady) {
                stopMonitoring();
            }
        };
    }, [isButtonMonitoringEnabled, wsConnected, contextJsReady]);

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

    // 判断按键是否处于按下状态（鼠标或硬件按键）
    const isButtonPressed = (index: number): boolean => {
        return (hardwareButtonStates[index] === 1 || pressedButtonStates[index] === 1) || false;
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
                            $pressed={false}
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
                        $pressed={isButtonPressed(index)}
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