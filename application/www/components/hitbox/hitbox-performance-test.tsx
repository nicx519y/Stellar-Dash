'use client';

import { useEffect, useMemo } from "react";
import { Box } from '@chakra-ui/react';
import styled from "styled-components";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import { useColorMode } from "../ui/color-mode";
import { GameControllerButton } from "@/types/gamepad-config";
import { HITBOX_WIDTH, HITBOX_HEIGHT, HITBOX_PADDING, HITBOX_LAYOUT_SCALE } from "./hitbox-constants";

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

const btnFrameRadiusDistance = 3;

// 按钮状态接口
export interface ButtonState {
    buttonIndex: number;
    isPressed: boolean;
}

// 基础Props接口
export interface HitboxPerformanceTestProps {
    buttonStates?: ButtonState[];
    hasText?: boolean;
    interactiveIds?: number[];
    highlightIds?: number[];
    className?: string;
    containerWidth?: number; // 外部容器宽度
    buttonLabelMap?: { [key: number]: GameControllerButton | string }; // 按钮标签映射
}

/**
 * HitboxPerformanceTest 组件
 * 通过props传入按钮状态数据来控制按键显示
 */
export default function HitboxPerformanceTest(props: HitboxPerformanceTestProps) {
    const hasText = props.hasText ?? true;
    const { colorMode } = useColorMode();
    const { contextJsReady, setContextJsReady, hitboxLayout } = useGamepadConfig();
    
    const layout = useMemo(() => {
        const rawLayout = hitboxLayout ?? [];
        return rawLayout.map(item => ({
            ...item,
            x: item.x * HITBOX_LAYOUT_SCALE,
            y: item.y * HITBOX_LAYOUT_SCALE
        }));
    }, [hitboxLayout]);
    const len = layout.length;

    // 计算缩放比例
    const calculateScale = (): number => {
        if (!props.containerWidth) return 1;
        
        const margin = 80; // 左右边距
        const availableWidth = props.containerWidth - (margin * 2);
        
        if (availableWidth <= 0) return 0.1; // 最小缩放比例
        
        const scale = availableWidth / HITBOX_WIDTH;
        return Math.min(scale, 1.3); // 最大不超过1.3，避免放大
    };

    const scale = calculateScale();

    /**
     * 初始化显示状态
     */
    useEffect(() => {
        setContextJsReady(true);
    }, [setContextJsReady]);

    // 子类可以重写的方法
    const getButtonFillColor = (_index: number): string => {
        return colorMode === 'light' ? 'white' : 'black';
    };

    const isButtonPressed = (index: number): boolean => {
        if (!props.buttonStates) return false;
        
        const buttonState = props.buttonStates.find(state => state.buttonIndex === index);
        return buttonState?.isPressed ?? false;
    };

    const getInnerText = (index: number, x: number, y: number): string => {
        if(index === len - 1) {
            return `<tspan x="${x}" y="${y}" style="font-size: 0.6rem; font-weight: bold; fill: #fff; ">Fn</tspan>`;
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
                $scale={scale}
            >
                <title>hitbox-performance-test</title>
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
                    />
                ))}

                {/* 渲染按钮文字 */}
                {hasText && layout.map((item: { x: number, y: number, r: number }, index: number) => (
                    <StyledText
                        textAnchor="middle"
                        dominantBaseline="middle"
                        key={index}
                        x={item.x}
                        y={index < len - 4 ? item.y : item.y + 30}
                        dangerouslySetInnerHTML={{ __html: getInnerText(index, item.x, index < len - 4 ? item.y : item.y + 30) }}
                    />
                ))}
            </StyledSvg>
        </Box>
    );
} 