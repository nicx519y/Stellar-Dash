'use client';

import { useEffect, useState } from "react";
import { HITBOX_BTN_POS_LIST } from "@/types/gamepad-config";
import { Box } from '@chakra-ui/react';
import styled from "styled-components";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import { useColorMode } from "../ui/color-mode";
import { type ButtonStateBinaryData, isButtonTriggered } from '@/lib/button-binary-parser';
import { useButtonMonitor } from '@/hooks/use-button-monitor';
import { GameControllerButton } from "@/types/gamepad-config";

const StyledSvg = styled.svg<{
    $scale?: number;
}>`
  width: 829px;
  height: 548.1px;
  padding: 20px;
  position: relative;
  transform: scale(${props => props.$scale || 1});
  transform-origin: center;
`;

/**
 * StyledCircle
 * @param props 
 *  $opacity: number - 透明度
 *  $interactive: boolean - 是否可交互  
 * @returns 
 */
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

const StyledFrame = styled.rect`
  fill: none;
  stroke: gray;
  stroke-width: 1px;
  filter: drop-shadow(0 0 5px rgba(204, 204, 204, 0.8));
`;

/**
 * StyledText
 * @returns 
 */
const StyledText = styled.text`
  text-align: center;
  font-family: 'custom_en',system-ui,sans-serif;
  cursor: default;
  pointer-events: none;
`;

export const btnPosList = HITBOX_BTN_POS_LIST;
const btnFrameRadiusDistance = 3;
export const btnLen = btnPosList.length;

// 基础Props接口
export interface HitboxBaseProps {
    onClick?: (id: number) => void;
    hasText?: boolean;
    interactiveIds?: number[];
    isButtonMonitoringEnabled?: boolean;
    highlightIds?: number[];
    className?: string;
    containerWidth?: number; // 外部容器宽度
    buttonLabelMap?: { [key: number]: GameControllerButton | string }; // 按钮标签映射
}

/**
 * HitboxBase 基类组件
 * 提供基础的SVG渲染和交互逻辑
 */
export default function HitboxBase(props: HitboxBaseProps) {
    const hasText = props.hasText ?? true;
    const { colorMode } = useColorMode();
    const { contextJsReady, setContextJsReady, wsConnected } = useGamepadConfig();

    const [pressedButtonStates, setPressedButtonStates] = useState(Array(btnLen).fill(-1));
    const [hardwareButtonStates, setHardwareButtonStates] = useState(Array(btnLen).fill(-1));

    // 计算缩放比例
    const calculateScale = (): number => {
        if (!props.containerWidth) return 1;
        
        const hitboxWidth = 829; // StyledSvg的原始宽度
        const margin = 80; // 左右边距
        const availableWidth = props.containerWidth - (margin * 2);
        
        if (availableWidth <= 0) return 0.1; // 最小缩放比例
        
        const scale = availableWidth / hitboxWidth;
        return Math.min(scale, 1.3); // 最大不超过1，避免放大
    };

    const scale = calculateScale();

    const buttonStates: { [key: number]: number } = {};
    if(props.interactiveIds) {
        for(let i = 0; i < btnLen; i++) {
            if(props.interactiveIds.includes(i)) {
                buttonStates[i] = -1;
            }
        }
    }

    // 使用 useButtonMonitor hook
    const { startMonitoring, stopMonitoring } = useButtonMonitor({
        autoInitialize: false, // 不自动初始化，由组件控制
        onButtonStatesChange: (data: ButtonStateBinaryData) => {
            if (!data || !props.interactiveIds) {
                return;
            }

            // 检查每个在interactiveIds范围内的按键
            props.interactiveIds.forEach((buttonId) => {
                const isPressed = isButtonTriggered(data.triggerMask, buttonId);
                
                // 使用ref获取当前真实状态，避免闭包陷阱
                const wasPressed = (buttonStates[buttonId] === 1) || false;
                if (isPressed !== wasPressed) {
                    if (isPressed) {
                        // 按键按下，模拟mousedown
                        props.onClick?.(buttonId);
                        setHardwareButtonStates(prev => {
                            const newStates = [...prev];
                            newStates[buttonId] = 1;
                            return newStates;
                        });
                        console.log(`硬件按键 ${buttonId} 按下`);
                        buttonStates[buttonId] = 1;
                    } else {
                        // 按键释放，模拟mouseup
                        props.onClick?.(-1);
                        setHardwareButtonStates(prev => {
                            const newStates = [...prev];
                            newStates[buttonId] = -1;
                            return newStates;
                        });
                        console.log(`硬件按键 ${buttonId} 释放`);
                        buttonStates[buttonId] = -1;
                    }
                }
            });

        },
        onError: (error) => {
            console.error('按键监听错误:', error);
        },
        onMonitoringStateChange: (isActive) => {
            console.log('按键监听状态变化:', isActive);
        }
    });

    const handleClick = (event: React.MouseEvent<SVGElement>) => {
        const target = event.target as SVGElement;
        if (!target.id || !target.id.startsWith("btn-")) return;
        const id = Number(target.id.replace("btn-", ""));
        if (id === Number.NaN || !(props.interactiveIds?.includes(id) ?? false)) return;
        if (event.type === "mousedown") {
            props.onClick?.(id);
            setPressedButtonStates(prev => {
                const newStates = [...prev];
                newStates[id] = 1;
                return newStates;
            });
        } else if (event.type === "mouseup") {
            props.onClick?.(-1);
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
        if (id === Number.NaN || !(props.interactiveIds?.includes(id) ?? false)) return;
        if (event.type === "mouseleave") {
            setPressedButtonStates(prev => {
                const newStates = [...prev];
                newStates[id] = -1;
                return newStates;
            });
        }
    }

    /**
     * 初始化显示状态
     */
    useEffect(() => {
        setContextJsReady(true);
    }, [setContextJsReady]);

    /**
     * 管理按键监听的启动和停止
     */
    useEffect(() => {

        const enabled = props.isButtonMonitoringEnabled ?? false;

        if(wsConnected && contextJsReady) {
            if(enabled) {
                console.log("hitbox-base: 启动按键监听");
                startMonitoring().catch((error) => {
                    console.error('启动按键监听失败:', error);
                });
            }

            if(!enabled) {
                setHardwareButtonStates(Array(btnLen).fill(-1));
                console.log("hitbox-base: 停止按键监听");
                stopMonitoring();
            }
        }

        // 清理函数
        return () => {
            setHardwareButtonStates(Array(btnLen).fill(-1));
            console.log("hitbox-base: 清理按键监听");
            if(wsConnected && contextJsReady) {
                stopMonitoring();
            }
        };
    }, [props.isButtonMonitoringEnabled, wsConnected, contextJsReady]);

    // 子类可以重写的方法
    const getButtonFillColor = (_index: number): string => {
        return colorMode === 'light' ? 'white' : 'black';
    };

    const isButtonPressed = (index: number): boolean => {
        return (hardwareButtonStates[index] === 1 || pressedButtonStates[index] === 1) || false;
    };

    const getInnerText = (index: number, x: number, y: number): string => {

        if(index === btnLen - 1) {
            return `<tspan x="${x}" y="${y}" style="font-size: 0.6rem; font-weight: bold; fill: #fff; ">Fn</tspan>`;
        }

        if(!props.buttonLabelMap) {
            return `
                <tspan x="${x}" y="${y}" style="font-size: 0.6rem; font-weight: bold; fill: #fff; ">${index + 1}</tspan>
            `;
        }
        
        const buttonLabel = props.buttonLabelMap?.[index];

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
        <Box display={contextJsReady ? "block" : "none"} className={props.className}>
            <StyledSvg 
                xmlns="http://www.w3.org/2000/svg"
                onMouseDown={handleClick}
                onMouseUp={handleClick}
                $scale={scale}
            >
                <title>hitbox</title>
                <StyledFrame x="0.36" y="0.36" width="787.82" height="507.1" rx="10" />

                {/* 渲染按钮外框 */}
                {btnPosList.map((item: { x: number, y: number, r: number }, index: number) => {
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
                {btnPosList.map((item: { x: number, y: number, r: number }, index: number) => (
                    <StyledCircle
                        // ref={(el: SVGCircleElement | null) => {
                        //     circleRefs.current[index] = el;
                        // }}
                        id={`btn-${index}`}
                        key={index}
                        cx={item.x}
                        cy={item.y}
                        r={item.r}
                        $opacity={1}
                        $interactive={props.interactiveIds?.includes(index) ?? false}
                        $highlight={(props.interactiveIds?.includes(index) && props.highlightIds?.includes(index)) ?? false}
                        $pressed={isButtonPressed(index)}
                        fill={getButtonFillColor(index)}
                        onMouseLeave={handleLeave}
                    />
                ))}

                {/* 渲染按钮文字 */}
                {hasText && btnPosList.map((item: { x: number, y: number, r: number }, index: number) => (
                    <StyledText
                        // ref={(el: SVGTextElement | null) => {
                        //     textRefs.current[index] = el;
                        // }}
                        textAnchor="middle"
                        dominantBaseline="middle"
                        key={index}
                        x={item.x}
                        y={index < btnLen - 4 ? item.y : item.y + 30}
                        dangerouslySetInnerHTML={{ __html: getInnerText(index, item.x, index < btnLen - 4 ? item.y : item.y + 30) }}
                    />
                ))}
            </StyledSvg>
        </Box>
    );
} 