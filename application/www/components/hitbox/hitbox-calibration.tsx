'use client';

import { useEffect, useMemo, useRef } from "react";
import { Box } from '@chakra-ui/react';
import styled from "styled-components";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import { useColorMode } from "../ui/color-mode";
import { GamePadColor } from "@/types/gamepad-color";
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

const btnFrameRadiusDistance = 3;

interface HitboxCalibrationProps {
    containerWidth?: number; // 外部容器宽度
    calibrationAllCompletedCallback?: () => void;
}

/**
 * HitboxCalibration - 专用于全局设置页面的Hitbox组件
 * 支持校准状态的颜色显示
 */
export default function HitboxCalibration(props: HitboxCalibrationProps) {
    const { calibrationAllCompletedCallback } = props;
    const { colorMode } = useColorMode();
    const {
        contextJsReady,
        setContextJsReady,
        hitboxLayout,
        calibrationStatus,
    } = useGamepadConfig();
    
    const layout = useMemo(() => {
        const rawLayout = hitboxLayout ?? [];
        return rawLayout.map(item => ({
            ...item,
            x: item.x * HITBOX_LAYOUT_SCALE,
            y: item.y * HITBOX_LAYOUT_SCALE
        }));
    }, [hitboxLayout]);
    
    const circleRefs = useRef<(SVGCircleElement | null)[]>([]);

    // 计算缩放比例
    const calculateScale = (): number => {
        if (!props.containerWidth) return 1;
        const margin = 80; // 左右边距
        const availableWidth = props.containerWidth - (margin * 2);
        if (availableWidth <= 0) return 0.1; // 最小缩放比例
        const scale = availableWidth / HITBOX_WIDTH;
        return Math.min(scale, 1.3); // 最大不超过1.3，避免过度放大
    };

    const scale = calculateScale();

    const completionNotifiedRef = useRef(false);
    useEffect(() => {
        if (calibrationStatus.allCalibrated) {
            if (!completionNotifiedRef.current) {
                completionNotifiedRef.current = true;
                calibrationAllCompletedCallback?.();
            }
        } else {
            completionNotifiedRef.current = false;
        }
    }, [calibrationStatus.allCalibrated, calibrationAllCompletedCallback]);

    // 获取按钮填充颜色
    const getButtonFillColor = (index: number): string => {
        if (buttonsColorList && buttonsColorList.length > 0) {
            if (index < buttonsColorList.length && buttonsColorList[index]) {
                return buttonsColorList[index].toString('css');
            }
        }
        return colorMode === 'light' ? 'white' : 'black';
    };

    // 根据校准状态生成按钮颜色列表
    const buttonsColorList = useMemo(() => {

        const colorMap = {
            'OFF': GamePadColor.fromString('#000000'),      // 黑色
            'RED': GamePadColor.fromString('#FF0000'),      // 红色 - 未校准
            'CYAN': GamePadColor.fromString('#00FFFF'),     // 天蓝色 - 顶部值采样中
            'DARK_BLUE': GamePadColor.fromString('#0000AA'), // 深蓝色 - 底部值采样中
            'GREEN': GamePadColor.fromString('#00FF00'),    // 绿色 - 校准完成
            'YELLOW': GamePadColor.fromString('#FFFF00'),   // 黄色 - 校准出错
        };

        const colors = calibrationStatus.buttons.map(button =>
            colorMap[button.ledColor] || GamePadColor.fromString('#808080') // 默认灰色
        );

        return colors;
    }, [calibrationStatus]);

    /**
     * 初始化显示状态
     */
    useEffect(() => {
        setContextJsReady(true);
    }, [setContextJsReady]);

    return (
        <Box display={contextJsReady ? "block" : "none"} >
            <StyledSvg 
                xmlns="http://www.w3.org/2000/svg"
                $scale={scale}
            >
                <title>hitbox</title>
                <StyledFrame x="0.36" y="0.36" width={HITBOX_WIDTH} height={HITBOX_HEIGHT} rx="10" />

                {/* 渲染按钮外框 */}
                {layout.map((item, index) => {
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
                {layout.map((item, index) => (
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
                        $interactive={false}
                        fill={getButtonFillColor(index)}
                    />
                ))}

            </StyledSvg>
        </Box>
    );
}
