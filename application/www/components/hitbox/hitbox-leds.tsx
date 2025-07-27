'use client';

import { useEffect, useState, useRef } from "react";
import { HITBOX_BTN_POS_LIST, LEDS_ANIMATION_CYCLE, LedsEffectStyle, LedsEffectStyleConfig } from "@/types/gamepad-config";
import { Box } from '@chakra-ui/react';
import styled from "styled-components";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import { useColorMode } from "../ui/color-mode";
import { GamePadColor } from "@/types/gamepad-color";
import { ledAnimations } from "./hitbox-animation";
import { type ButtonStateBinaryData, isButtonTriggered } from '@/lib/button-binary-parser';
import { useButtonMonitor } from '@/hooks/use-button-monitor';

const StyledSvg = styled.svg`
  width: 828.82px;
  height: 548.1px;
  padding: 20px;
  position: relative;
`;

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

  &:active {
    stroke-width: ${props => props.$interactive ? '2px' : '1px'};
    stroke: ${props => props.$interactive ? 'yellowgreen' : 'gray'};
    filter: ${props => props.$interactive ? 'drop-shadow(0 0 15px rgba(154, 205, 50, 0.9))' : 'none'};
  }

  /* 硬件按下状态样式 */
  ${props => props.$pressed && `
    stroke: yellowgreen;
    stroke-width: 2px;
    filter: drop-shadow(0 0 15px rgba(154, 205, 50, 0.9));
  `}
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

interface HitboxLedsProps {
    onClick?: (id: number) => void;
    hasText?: boolean;
    ledsConfig?: LedsEffectStyleConfig;
    interactiveIds?: number[];
    highlightIds?: number[];
    disabledKeys?: number[];
    isButtonMonitoringEnabled?: boolean;
    className?: string;
}

/**
 * HitboxLeds - 专用于LED设置页面的Hitbox组件
 * 支持LED动画预览功能
 */
export default function HitboxLeds(props: HitboxLedsProps) {
    const hasText = props.hasText ?? true;
    const { colorMode } = useColorMode();
    const { contextJsReady, setContextJsReady, wsConnected } = useGamepadConfig();

    // 硬件按键状态管理
    const [pressedButtonStates, setPressedButtonStates] = useState(Array(btnLen).fill(-1));
    const [hardwareButtonStates, setHardwareButtonStates] = useState(Array(btnLen).fill(-1));

    const buttonStates: { [key: number]: number } = {};
    if(props.interactiveIds) {
        for(let i = 0; i < btnLen; i++) {
            if(props.interactiveIds.includes(i)) {
                buttonStates[i] = -1;
            }
        }
    }

    const disabledKeysRef = useRef(props.disabledKeys ?? []);
    const interactiveIdsRef = useRef(props.interactiveIds ?? []);

    const frontColorRef = useRef(props.ledsConfig?.ledColors?.[0]?.clone() ?? GamePadColor.fromString("#ffffff"));
    const backColor1Ref = useRef(props.ledsConfig?.ledColors?.[1]?.clone() ?? GamePadColor.fromString("#000000"));
    const backColor2Ref = useRef(props.ledsConfig?.ledColors?.[2]?.clone() ?? GamePadColor.fromString("#000000"));
    const defaultBackColorRef = useRef(colorMode === 'light' ? GamePadColor.fromString("#ffffff") : GamePadColor.fromString("#000000"));
    const brightnessRef = useRef(props.ledsConfig?.brightness ?? 100);
    const animationSpeedRef = useRef(props.ledsConfig?.animationSpeed ?? 1);
    const colorEnabledRef = useRef(props.ledsConfig?.ledEnabled ?? false);
    const effectStyleRef = useRef(props.ledsConfig?.ledsEffectStyle ?? LedsEffectStyle.STATIC);
    const pressedButtonListRef = useRef(Array(btnLen).fill(-1));
    const prevPressedButtonListRef = useRef(Array(btnLen).fill(-1));

    const circleRefs = useRef<(SVGCircleElement | null)[]>([]);
    const colorListRef = useRef<GamePadColor[]>(Array(btnLen));
    const textRefs = useRef<(SVGTextElement | null)[]>([]);
    const animationFrameRef = useRef<number>();
    const timerRef = useRef<number>(0);

    // ripple 列表
    const ripplesRef = useRef<{ centerIndex: number, startTime: number }[]>([]);

    // 使用 useButtonMonitor hook
    const { startMonitoring, stopMonitoring } = useButtonMonitor({
        autoInitialize: false, // 不自动初始化，由组件控制
        onButtonStatesChange: (data: ButtonStateBinaryData) => {
            if (!data || !interactiveIdsRef.current) {
                return;
            }

            // 检查每个在interactiveIds范围内的按键
            interactiveIdsRef.current.forEach((buttonId) => {
                // 禁用的按键不处理
                if(disabledKeysRef.current.includes(buttonId)) {
                    return;
                }

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
                        console.log(`hitbox-leds: 硬件按键 ${buttonId} 按下`);
                        buttonStates[buttonId] = 1;
                    } else {
                        // 按键释放，模拟mouseup
                        props.onClick?.(-1);
                        setHardwareButtonStates(prev => {
                            const newStates = [...prev];
                            newStates[buttonId] = -1;
                            return newStates;
                        });
                        console.log(`hitbox-leds: 硬件按键 ${buttonId} 释放`);
                        buttonStates[buttonId] = -1;
                    }
                }
            });
        },
        onError: (error) => {
            console.error('hitbox-leds: 按键监听错误:', error);
        },
        onMonitoringStateChange: (isActive) => {
            console.log('hitbox-leds: 按键监听状态变化:', isActive);
        }
    });

    const handleClick = (event: React.MouseEvent<SVGElement>) => {
        const target = event.target as SVGElement;
        if (!target.id || !target.id.startsWith("btn-")) return;
        const id = Number(target.id.replace("btn-", ""));
        if (id === Number.NaN || !interactiveIdsRef.current.includes(id)) return;
        // 禁用的按键不能点击
        if (disabledKeysRef.current.includes(id)) return;
        
        if (event.type === "mousedown") {
            props.onClick?.(id);
            pressedButtonListRef.current[id] = 1;
            setPressedButtonStates(prev => {
                const newStates = [...prev];
                newStates[id] = 1;
                return newStates;
            });
        } else if (event.type === "mouseup") {
            props.onClick?.(-1);
            pressedButtonListRef.current[id] = -1;
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
        if (id === Number.NaN || !(interactiveIdsRef.current.includes(id) ?? false)) return;
        if (event.type === "mouseleave") {
            pressedButtonListRef.current[id] = -1;
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
        for (let i = 0; i < btnLen; i++) {
            colorListRef.current[i] = backColor1Ref.current.clone();
        }
    }, [setContextJsReady]);

    /**
     * 管理按键监听的启动和停止
     */
    useEffect(() => {
        const enabled = props.isButtonMonitoringEnabled ?? false;

        if(wsConnected && contextJsReady) {
            if(enabled) {
                console.log("hitbox-leds: 启动按键监听");
                startMonitoring().catch((error) => {
                    console.error('启动按键监听失败:', error);
                });
            }

            if(!enabled) {
                setHardwareButtonStates(Array(btnLen).fill(-1));
                console.log("hitbox-leds: 停止按键监听");
                stopMonitoring();
            }
        }

        // 清理函数
        return () => {
            setHardwareButtonStates(Array(btnLen).fill(-1));
            console.log("hitbox-leds: 清理按键监听");
            if(wsConnected && contextJsReady) {
                stopMonitoring();
            }
        };
    }, [props.isButtonMonitoringEnabled, wsConnected, contextJsReady]);

    useEffect(() => {
        defaultBackColorRef.current = colorMode === 'light' ? GamePadColor.fromString("#ffffff") : GamePadColor.fromString("#000000");
    }, [colorMode]);

    useEffect(() => {
        if (props.ledsConfig?.ledColors?.[0]) {
            frontColorRef.current.setValue(props.ledsConfig.ledColors[0]);
        }
    }, [props.ledsConfig?.ledColors?.[0]]);

    useEffect(() => {
        if (props.ledsConfig?.ledColors?.[1]) {
            backColor1Ref.current.setValue(props.ledsConfig.ledColors[1]);
        }
    }, [props.ledsConfig?.ledColors?.[1]]);

    useEffect(() => {
        if (props.ledsConfig?.ledColors?.[2]) {
            backColor2Ref.current.setValue(props.ledsConfig.ledColors[2]);
        }
    }, [props.ledsConfig?.ledColors?.[2]]);

    useEffect(() => {
        brightnessRef.current = props.ledsConfig?.brightness ?? 100;
    }, [props.ledsConfig?.brightness]);

    useEffect(() => {
        animationSpeedRef.current = props.ledsConfig?.animationSpeed ?? 1;
    }, [props.ledsConfig?.animationSpeed]);

    useEffect(() => {
        effectStyleRef.current = props.ledsConfig?.ledsEffectStyle ?? LedsEffectStyle.STATIC;
    }, [props.ledsConfig?.ledsEffectStyle]);

    useEffect(() => {
        colorEnabledRef.current = props.ledsConfig?.ledEnabled ?? true;
    }, [props.ledsConfig?.ledEnabled]);

    useEffect(() => {
        disabledKeysRef.current = props.disabledKeys ?? [];
    }, [props.disabledKeys]);

    useEffect(() => {
        interactiveIdsRef.current = props.interactiveIds ?? [];
    }, [props.interactiveIds]);

    useEffect(() => {
        if (props.ledsConfig?.ledEnabled) {
            startAnimation();
        } else {
            stopAnimation();
            clearButtonsColor();
        }
        // 清理函数
        return () => {
            stopAnimation();
            timerRef.current = 0;
        };
    }, [props.ledsConfig?.ledEnabled]);

    // 判断按键是否可交互（既要在交互列表中，又不能在禁用列表中）
    const isButtonInteractive = (buttonId: number): boolean => {
        return (interactiveIdsRef.current.includes(buttonId) ?? false) && !isButtonDisabled(buttonId);
    };

    // 判断按键是否被禁用
    const isButtonDisabled = (buttonId: number): boolean => {
        return disabledKeysRef.current.includes(buttonId);
    };

    // 判断按键是否处于按下状态（鼠标或硬件按键）
    const isButtonPressed = (index: number): boolean => {
        return (hardwareButtonStates[index] === 1 || pressedButtonStates[index] === 1) || false;
    };

    // leds 颜色动画
    const animate = () => {
        const now = new Date().getTime();
        const deltaTime = now - timerRef.current;
        
        // 使用与C++端一致的动画进度计算方式
        // C++端: progress = (elapsed % LEDS_ANIMATION_CYCLE) / LEDS_ANIMATION_CYCLE * speedMultiplier; progress = fmod(progress, 1.0f);
        const speedMultiplier = animationSpeedRef.current;
        let progress = (deltaTime % LEDS_ANIMATION_CYCLE) / LEDS_ANIMATION_CYCLE * speedMultiplier;
        progress = progress % 1; // 确保进度值在 0.0-1.0 范围内循环

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
            // 禁用的按键LED不亮，使用默认背景色
            if (isButtonDisabled(i)) {
                colorListRef.current[i].setValue(defaultBackColorRef.current);
                continue;
            }

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
        circleRefs.current.forEach((circle, index) => {
            if (circle) {
                // 禁用的按键始终使用默认背景色
                const color = isButtonDisabled(index) ? defaultBackColorRef.current : defaultBackColorRef.current;
                circle.setAttribute('fill', color.toString('css'));
            }
        });
    }

    return (
        <Box display={contextJsReady ? "block" : "none"} className={props.className}>
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
                        $highlight={props.highlightIds?.includes(index) ?? false}
                        $pressed={isButtonPressed(index)}
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