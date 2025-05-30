'use client';

import { useEffect, useState, useRef } from "react";
import { HITBOX_BTN_POS_LIST, LEDS_ANIMATION_CYCLE, LedsEffectStyle } from "@/types/gamepad-config";
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
 * colorEnabled: boolean - 是否用颜色
 * color1: string - 默认颜色
 * color2: string - 呼吸颜色
 * brightness: number - 亮度
 * interactiveIds: number[] - 可交互按钮id列表  
 * @returns 
 */
export default function Hitbox(props: {
    onClick?: (id: number) => void,
    hasLeds?: boolean,
    hasText?: boolean,
    colorEnabled?: boolean,
    frontColor?: GamePadColor,
    backColor1?: GamePadColor,
    backColor2?: GamePadColor,
    brightness?: number,
    effectStyle?: LedsEffectStyle,
    interactiveIds?: number[],
    highlightIds?: number[],
    animationSpeed?: number,
}) {

    const [hasLeds, _setHasLeds] = useState(props.hasLeds ?? false);
    const [hasText, _setHasText] = useState(props.hasText ?? true);

    const { colorMode } = useColorMode()
    const frontColorRef = useRef(props.frontColor?.clone() ?? GamePadColor.fromString("#ffffff"));
    const backColor1Ref = useRef(props.backColor1?.clone() ?? GamePadColor.fromString("#000000"));
    const backColor2Ref = useRef(props.backColor2?.clone() ?? GamePadColor.fromString("#000000"));
    const defaultBackColorRef = useRef(colorMode === 'light' ? GamePadColor.fromString("#ffffff") : GamePadColor.fromString("#000000"));
    const brightnessRef = useRef(props.brightness ?? 100);
    const animationSpeedRef = useRef(props.animationSpeed ?? 1);
    const colorEnabledRef = useRef(props.colorEnabled ?? false);
    const effectStyleRef = useRef(props.effectStyle ?? LedsEffectStyle.STATIC);
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
        if (props.frontColor) {
            frontColorRef.current.setValue(props.frontColor);
        }
    }, [props.frontColor]);

    useEffect(() => {
        if (props.backColor1) {
            backColor1Ref.current.setValue(props.backColor1);
        }
    }, [props.backColor1]);

    useEffect(() => {
        if (props.backColor2) {
            backColor2Ref.current.setValue(props.backColor2);
        }
    }, [props.backColor2]);

    useEffect(() => {
        brightnessRef.current = props.brightness ?? 100;
    }, [props.brightness]);

    useEffect(() => {
        animationSpeedRef.current = props.animationSpeed ?? 1;
    }, [props.animationSpeed]);

    useEffect(() => {
        effectStyleRef.current = props.effectStyle ?? LedsEffectStyle.STATIC;
    }, [props.effectStyle]);

    useEffect(() => {
        colorEnabledRef.current = props.colorEnabled ?? true;
    }, [props.colorEnabled]);

    useEffect(() => {
        if (hasLeds) {
            startAnimation();
        }
        // 清理函数
        return () => {
            stopAnimation();
            timerRef.current = 0;
        };
    }, [hasLeds]);


    // leds 颜色动画
    const animate = () => {
        const now = new Date().getTime();
        const deltaTime = now - timerRef.current;
        const cycle = LEDS_ANIMATION_CYCLE / animationSpeedRef.current;
        const progress = (deltaTime % cycle) / cycle;

        console.log(effectStyleRef.current);
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