'use client';

import { useEffect, useRef, useState } from "react";
import { HITBOX_BTN_POS_LIST } from "@/types/gamepad-config";
import { Box } from '@chakra-ui/react';
import styled from "styled-components";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import { useColorMode } from "../ui/color-mode";
import { GamePadColor } from "@/types/gamepad-color";

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

const StyledFrame = styled.rect`
  fill: none;
  stroke: gray;
  stroke-width: 1px;
  filter: drop-shadow(0 0 5px rgba(204, 204, 204, 0.8));
`;

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

interface HitboxCalibrationProps {
    onClick?: (id: number) => void;
    hasText?: boolean;
    interactiveIds?: number[];
    highlightIds?: number[];
    buttonsColorList?: GamePadColor[]; // 按钮颜色列表，用于校准状态显示
    className?: string;
    containerWidth?: number; // 外部容器宽度
}

/**
 * HitboxCalibration - 专用于全局设置页面的Hitbox组件
 * 支持校准状态的颜色显示
 */
export default function HitboxCalibration(props: HitboxCalibrationProps) {
    const [hasText, _setHasText] = useState(props.hasText ?? true);
    const { colorMode } = useColorMode();
    const { contextJsReady, setContextJsReady } = useGamepadConfig();

    const circleRefs = useRef<(SVGCircleElement | null)[]>([]);
    const textRefs = useRef<(SVGTextElement | null)[]>([]);
    const pressedButtonListRef = useRef(Array(btnLen).fill(-1));
    const animationFrameRef = useRef<number>();

    // 计算缩放比例
    const calculateScale = (): number => {
        if (!props.containerWidth) return 1;
        
        const hitboxWidth = 829; // StyledSvg的原始宽度
        const margin = 80; // 左右边距
        const availableWidth = props.containerWidth - (margin * 2);
        
        if (availableWidth <= 0) return 0.1; // 最小缩放比例
        
        const scale = availableWidth / hitboxWidth;
        return Math.min(scale, 1.3); // 最大不超过1.3，避免过度放大
    };

    const scale = calculateScale();

    const handleClick = (event: React.MouseEvent<SVGElement>) => {
        const target = event.target as SVGElement;
        if (!target.id || !target.id.startsWith("btn-")) return;
        const id = Number(target.id.replace("btn-", ""));
        if (id === Number.NaN || !(props.interactiveIds?.includes(id) ?? false)) return;
        if (event.type === "mousedown") {
            props.onClick?.(id);
            pressedButtonListRef.current[id] = 1;
        } else if (event.type === "mouseup") {
            props.onClick?.(-1);
            pressedButtonListRef.current[id] = -1;
        }
    };

    const handleLeave = (event: React.MouseEvent<SVGElement>) => {
        const target = event.target as SVGElement;
        if (!target.id || !target.id.startsWith("btn-")) return;
        const id = Number(target.id.replace("btn-", ""));
        if (id === Number.NaN || !(props.interactiveIds?.includes(id) ?? false)) return;
        if (event.type === "mouseleave") {
            pressedButtonListRef.current[id] = -1;
        }
    }

    // 获取按钮填充颜色
    const getButtonFillColor = (index: number): string => {
        if (props.buttonsColorList && props.buttonsColorList.length > 0) {
            if (index < props.buttonsColorList.length && props.buttonsColorList[index]) {
                return props.buttonsColorList[index].toString('css');
            }
        }
        return colorMode === 'light' ? 'white' : 'black';
    };

    /**
     * 自定义颜色动画渲染
     */
    const animateCustomColors = () => {
        if (props.buttonsColorList && props.buttonsColorList.length > 0) {
            updateButtonsWithCustomColors(props.buttonsColorList);
        }
        
        // 继续下一帧渲染
        animationFrameRef.current = requestAnimationFrame(animateCustomColors);
    };

    /**
     * 启动自定义颜色动画
     */
    const startCustomColorAnimation = () => {
        if (!animationFrameRef.current) {
            animationFrameRef.current = requestAnimationFrame(animateCustomColors);
        }
    };

    /**
     * 停止动画
     */
    const stopAnimation = () => {
        if (animationFrameRef.current) {
            cancelAnimationFrame(animationFrameRef.current);
            animationFrameRef.current = undefined;
        }
    };

    /**
     * 使用自定义颜色列表更新按钮颜色
     */
    const updateButtonsWithCustomColors = (colorList: GamePadColor[]) => {
        circleRefs.current.forEach((circle, index) => {
            if (circle && index < colorList.length && colorList[index]) {
                circle.setAttribute('fill', colorList[index].toString('css'));
            } else if (circle) {
                // 如果没有提供颜色，使用默认背景色
                const defaultColor = colorMode === 'light' ? '#ffffff' : '#000000';
                circle.setAttribute('fill', defaultColor);
            }
        });
    };

    /**
     * 清除按钮颜色
     */
    const clearButtonsColor = () => {
        circleRefs.current.forEach((circle) => {
            if (circle) {
                const defaultColor = colorMode === 'light' ? '#ffffff' : '#000000';
                circle.setAttribute('fill', defaultColor);
            }
        });
    };

    /**
     * 初始化显示状态
     */
    useEffect(() => {
        setContextJsReady(true);
    }, [setContextJsReady]);

    // 当 buttonsColorList 变化时，处理自定义颜色渲染
    useEffect(() => {
        if (props.buttonsColorList && props.buttonsColorList.length > 0) {
            // 启动自定义颜色渲染
            startCustomColorAnimation();
        } else {
            stopAnimation();
            clearButtonsColor();
        }

        // 清理函数
        return () => {
            stopAnimation();
        };
    }, [props.buttonsColorList, colorMode]);

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
                        $interactive={props.interactiveIds?.includes(index) ?? false}
                        $highlight={props.highlightIds?.includes(index) ?? false}
                        fill={getButtonFillColor(index)}
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
                        fill={colorMode === 'light' ? 'black' : 'white'}
                    >
                        {index !== btnLen - 1 ? index + 1 : "Fn"}
                    </StyledText>
                ))}
            </StyledSvg>
        </Box>
    );
} 