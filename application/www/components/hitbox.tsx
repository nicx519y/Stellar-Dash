'use client';

import { useEffect, useState, useRef } from "react";
import { HITBOX_BTN_POS_LIST, LEDS_ANIMATION_CYCLE, LedsEffectStyle, LedsEffectStyleConfig } from "@/types/gamepad-config";
import { Box } from '@chakra-ui/react';
import styled from "styled-components";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import { useColorMode } from "./ui/color-mode";
import { GamePadColor } from "@/types/gamepad-color";
import { ledAnimations } from "@/components/hitbox-animation";

const StyledSvg = styled.svg`
  width: 828.82px;
  height: 548.1px;
  padding:20px;
  position: relative;
`;

/**
 * StyledCircle
 * @param props 
 *  $opacity: number - 透明度
 *  $interactive: boolean - 是否可交互  
 * @returns 
 */
const StyledCircle = styled.circle<{
    // $color?: string;
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

/**
 * StyledText
 * @returns 
 */
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

/**
 * Hitbox
 * @param props 
 * onClick: (id: number) => void - 点击事件
 * hasText?: boolean - 是否显示文字
 * ledsConfig?: LedsEffectStyleConfig - LED配置
 * interactiveIds?: number[] - 可交互按钮id列表  
 * highlightIds?: number[] - 高亮按钮id列表
 * buttonsColorList?: GamePadColor[] - 按钮颜色列表，用于批量设置按钮颜色（优先级高于LED配置）
 * @returns 
 */
export default function Hitbox(props: {
    onClick?: (id: number) => void,
    hasText?: boolean,
    ledsConfig?: LedsEffectStyleConfig,
    interactiveIds?: number[],
    highlightIds?: number[],
    buttonsColorList?: GamePadColor[],
}) {

    const [hasText, _setHasText] = useState(props.hasText ?? true);

    const { colorMode } = useColorMode()
    const frontColorRef = useRef(props.ledsConfig?.colors[0]?.clone() ?? GamePadColor.fromString("#ffffff"));
    const backColor1Ref = useRef(props.ledsConfig?.colors[1]?.clone() ?? GamePadColor.fromString("#000000"));
    const backColor2Ref = useRef(props.ledsConfig?.colors[2]?.clone() ?? GamePadColor.fromString("#000000"));
    const defaultBackColorRef = useRef(colorMode === 'light' ? GamePadColor.fromString("#ffffff") : GamePadColor.fromString("#000000"));
    const brightnessRef = useRef(props.ledsConfig?.brightness ?? 100);
    const animationSpeedRef = useRef(props.ledsConfig?.animationSpeed ?? 1);
    const colorEnabledRef = useRef(props.ledsConfig?.ledsEnabled ?? false);
    const effectStyleRef = useRef(props.ledsConfig?.effectStyle ?? LedsEffectStyle.STATIC);
    const pressedButtonListRef = useRef(Array(btnLen).fill(-1));
    const prevPressedButtonListRef = useRef(Array(btnLen).fill(-1));

    const { contextJsReady, setContextJsReady } = useGamepadConfig();

    const circleRefs = useRef<(SVGCircleElement | null)[]>([]);
    const colorListRef = useRef<GamePadColor[]>(Array(btnLen));
    const textRefs = useRef<(SVGTextElement | null)[]>([]);
    const animationFrameRef = useRef<number>();
    const timerRef = useRef<number>(0);

    // leds 颜色动画
    // ripple 列表
    const ripplesRef = useRef<{ centerIndex: number, startTime: number }[]>([]);

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

    /**
     * 初始化显示状态
     */
    useEffect(() => {
        setContextJsReady(true);
        for (let i = 0; i < btnLen; i++) {
            colorListRef.current[i] = backColor1Ref.current.clone();
        }
    }, [setContextJsReady]);

    useEffect(() => {
        defaultBackColorRef.current = colorMode === 'light' ? GamePadColor.fromString("#ffffff") : GamePadColor.fromString("#000000");
    }, [colorMode]);

    useEffect(() => {
        if (props.ledsConfig?.colors[0]) {
            frontColorRef.current.setValue(props.ledsConfig.colors[0]);
        }
    }, [props.ledsConfig?.colors[0]]);

    useEffect(() => {
        if (props.ledsConfig?.colors[1]) {
            backColor1Ref.current.setValue(props.ledsConfig.colors[1]);
        }
    }, [props.ledsConfig?.colors[1]]);

    useEffect(() => {
        if (props.ledsConfig?.colors[2]) {
            backColor2Ref.current.setValue(props.ledsConfig.colors[2]);
        }
    }, [props.ledsConfig?.colors[2]]);

    useEffect(() => {
        brightnessRef.current = props.ledsConfig?.brightness ?? 100;
    }, [props.ledsConfig?.brightness]);

    useEffect(() => {
        animationSpeedRef.current = props.ledsConfig?.animationSpeed ?? 1;
    }, [props.ledsConfig?.animationSpeed]);

    useEffect(() => {
        effectStyleRef.current = props.ledsConfig?.effectStyle ?? LedsEffectStyle.STATIC;
    }, [props.ledsConfig?.effectStyle]);

    useEffect(() => {
        colorEnabledRef.current = props.ledsConfig?.ledsEnabled ?? true;
    }, [props.ledsConfig?.ledsEnabled]);

    useEffect(() => {
        if (props.ledsConfig?.ledsEnabled) {
            startAnimation();
        } else if (props.buttonsColorList && props.buttonsColorList.length > 0) {
            // 如果有自定义颜色列表，启动自定义颜色渲染
            startCustomColorAnimation();
        } else {
            stopAnimation();
            clearButtonsColor();
        }
        // 清理函数
        return () => {
            stopAnimation();
            timerRef.current = 0;
        };
    }, [props.ledsConfig?.ledsEnabled]);

    // 当 buttonsColorList 变化时，处理自定义颜色渲染
    useEffect(() => {
        if (props.buttonsColorList && props.buttonsColorList.length > 0) {
            // 停止 LED 动画，启动自定义颜色渲染
            stopAnimation();
            startCustomColorAnimation();
        } else if (props.ledsConfig?.ledsEnabled) {
            // 恢复 LED 动画
            startAnimation();
        } else {
            stopAnimation();
            clearButtonsColor();
        }
    }, [props.buttonsColorList]);

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
     * 使用自定义颜色列表更新按钮颜色
     */
    const updateButtonsWithCustomColors = (colorList: GamePadColor[]) => {
        circleRefs.current.forEach((circle, index) => {
            if (circle && index < colorList.length && colorList[index]) {
                circle.setAttribute('fill', colorList[index].toString('css'));
            } else if (circle) {
                // 如果没有提供颜色，使用默认背景色
                circle.setAttribute('fill', defaultBackColorRef.current.toString('css'));
            }
        });
    };

    // leds 颜色动画
    const animate = () => {
        const now = new Date().getTime();
        const deltaTime = now - timerRef.current;
        const cycle = LEDS_ANIMATION_CYCLE / animationSpeedRef.current;
        const progress = (deltaTime % cycle) / cycle;

        const algorithm = ledAnimations[effectStyleRef.current];

        let global: {
            ripples?: Array<{ centerIndex: number, progress: number }>;
            [key: string]: unknown;
        } = {};

        if (effectStyleRef.current === LedsEffectStyle.RIPPLE) {
            pressedButtonListRef.current.forEach((v, idx) => {
                if (v === 1 && prevPressedButtonListRef.current[idx] !== 1) {
                    ripplesRef.current.push({ centerIndex: idx, startTime: now });
                }
            });
            const rippleDuration = 3000 / animationSpeedRef.current; // ms
            ripplesRef.current = ripplesRef.current.filter(r => (now - r.startTime) < rippleDuration);
            const ripples = ripplesRef.current.map(r => ({
                centerIndex: r.centerIndex,
                progress: Math.min(1, (now - r.startTime) / rippleDuration)
            }));
            global = { ripples };
        }

        for (let i = 0; i < btnLen; i++) {
            const color = algorithm({
                index: i,
                progress,
                pressed: pressedButtonListRef.current[i] === 1,
                colorEnabled: colorEnabledRef.current,
                frontColor: frontColorRef.current,
                backColor1: backColor1Ref.current,
                backColor2: backColor2Ref.current,
                defaultBackColor: defaultBackColorRef.current,
                effectStyle: effectStyleRef.current,
                brightness: brightnessRef.current,
                global,
            });

            colorListRef.current[i].setValue(color);
        }

        // 更新按钮颜色
        circleRefs.current.forEach((circle, index) => {
            if (circle) {
                circle.setAttribute('fill', colorListRef.current[index].toString('css'));
            }
        });

        prevPressedButtonListRef.current = pressedButtonListRef.current.slice();

        animationFrameRef.current = requestAnimationFrame(animate);
    };

    const startAnimation = () => {
        if (!animationFrameRef.current) {
            animationFrameRef.current = requestAnimationFrame(animate);
            timerRef.current = new Date().getTime();
        }
    };

    const stopAnimation = () => {
        if (animationFrameRef.current) {
            cancelAnimationFrame(animationFrameRef.current);
            animationFrameRef.current = undefined;
        }
    };

    const clearButtonsColor = () => {
        circleRefs.current.forEach((circle) => {
            if (circle) {
                circle.setAttribute('fill', defaultBackColorRef.current.toString('css'));
            }
        });
    }

    return (
        <Box display={contextJsReady ? "block" : "none"} >
            <StyledSvg xmlns="http://www.w3.org/2000/svg"
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
                            id={`btn-${index}`}
                            key={index}
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
                        fill={colorMode === 'light' ? 'white' : 'black'}
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