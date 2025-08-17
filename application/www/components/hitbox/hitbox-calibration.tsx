'use client';

import { useEffect, useMemo, useRef, useState } from "react";
import { HITBOX_BTN_POS_LIST } from "@/types/gamepad-config";
import { Box } from '@chakra-ui/react';
import styled from "styled-components";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import { useColorMode } from "../ui/color-mode";
import { GamePadColor } from "@/types/gamepad-color";
import { CalibrationStatus } from "@/types/types";
import { eventBus, EVENTS } from "@/lib/event-manager";

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

// const StyledText = styled.text`
//   text-align: center;
//   font-family: "Helvetica", cursive;
//   font-size: .9rem;
//   cursor: default;
//   pointer-events: none;
// `;

const btnPosList = HITBOX_BTN_POS_LIST;
const btnFrameRadiusDistance = 3;
// const btnLen = btnPosList.length;

interface HitboxCalibrationProps {
    containerWidth?: number; // 外部容器宽度
    calibrationAllCompletedCallback?: () => void;
}

/**
 * HitboxCalibration - 专用于全局设置页面的Hitbox组件
 * 支持校准状态的颜色显示
 */
export default function HitboxCalibration(props: HitboxCalibrationProps) {
    const { colorMode } = useColorMode();
    const { contextJsReady, setContextJsReady, startManualCalibration, stopManualCalibration } = useGamepadConfig();
    const circleRefs = useRef<(SVGCircleElement | null)[]>([]);

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

    const [calibrationStatus, setCalibrationStatus] = useState<CalibrationStatus>({
        isActive: false,
        uncalibratedCount: 0,
        activeCalibrationCount: 0,
        allCalibrated: false,
        buttons: []
    });

    // 手动校准状态监听 - 只订阅一次，通过 ref 获取最新状态
    useEffect(() => {
        // 添加校准状态更新事件监听
        const handleCalibrationUpdate = (data: unknown) => {
            // 处理校准状态更新
            if (data && typeof data === 'object' && 'calibrationStatus' in data) {
                const eventData = data as { calibrationStatus?: CalibrationStatus };
                if (eventData.calibrationStatus) {
                    // 确保数据格式正确，添加默认值
                    const statusData = eventData.calibrationStatus;
                    const newCalibrationStatus: CalibrationStatus = {
                        isActive: statusData?.isActive || false,
                        uncalibratedCount: statusData?.uncalibratedCount || 0,
                        activeCalibrationCount: statusData?.activeCalibrationCount || 0,
                        allCalibrated: statusData?.allCalibrated || false,
                        buttons: statusData?.buttons || []
                    };
                    // 直接更新校准状态
                    setCalibrationStatus(newCalibrationStatus);

                    if(newCalibrationStatus.allCalibrated) {
                        props.calibrationAllCompletedCallback?.();
                    }
                }
            }
        };

        // 订阅校准更新事件（只订阅一次）
        const unsubscribe = eventBus.on(EVENTS.CALIBRATION_UPDATE, handleCalibrationUpdate);
        // 开启校准
        startManualCalibration();

        // 清理函数
        return () => {
            unsubscribe();
            // 停止校准
            stopManualCalibration();
        };
    }, []); // 依赖数组为空，只执行一次

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
                        $interactive={false}
                        fill={getButtonFillColor(index)}
                    />
                ))}

            </StyledSvg>
        </Box>
    );
} 