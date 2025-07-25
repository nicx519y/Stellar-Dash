"use client";

import {
    Flex,
    Center,
    Stack,
    VStack,
    Fieldset,
    RadioCardLabel,
    SimpleGrid,
    Icon,
    HStack,
    parseColor,
    Text,
    Color,
    Card,
    Portal,
    Switch,
    Grid,
    Separator,
} from "@chakra-ui/react";

import {
    RadioCardItem,
    RadioCardRoot,
} from "@/components/ui/radio-card";

import { ColorPicker, Slider } from "@chakra-ui/react";

import { useEffect, useMemo, useState } from "react";
import {
    // GameProfile,
    LedsEffectStyle,
    LEDS_SETTINGS_INTERACTIVE_IDS,
    AroundLedsEffectStyle,
    GameProfile,
} from "@/types/gamepad-config";
import { LuSunDim, LuActivity, LuCheck, LuSparkles, LuWaves, LuTarget, LuCloudSunRain, LuAudioLines } from "react-icons/lu";
import { TbMeteorFilled } from "react-icons/tb";
import HitboxLeds from "@/components/hitbox/hitbox-leds";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import { useLanguage } from "@/contexts/language-context";
import { useColorMode } from "./ui/color-mode";
import { ContentActionButtons } from "@/components/content-action-buttons";
import { GamePadColor } from "@/types/gamepad-color";
import { ProfileSelect } from "./profile-select";
import { ColorQueueManager } from "@/components/color-queue-manager";

export function LEDsSettingContent() {
    const { t } = useLanguage();

    const { defaultProfile, updateProfileDetails, pushLedsConfig, clearLedsPreview, wsConnected } = useGamepadConfig();
    const { colorMode } = useColorMode();
    const defaultFrontColor = useMemo(() => {
        if (colorMode === "dark") {
            return parseColor("#000000");
        } else {
            return parseColor("#ffffff");
        }
    }, [colorMode]);

    const [isInit, setIsInit] = useState<boolean>(false); // 是否初始化
    const [needPreview, setNeedPreview] = useState<boolean>(false); // 是否需要预览
    const [needUpdate, setNeedUpdate] = useState<boolean>(false); // 是否需要更新


    const [ledsEffectStyle, setLedsEffectStyle] = useState<LedsEffectStyle>(defaultProfile.ledsConfigs?.ledsEffectStyle ?? LedsEffectStyle.STATIC);
    const [color1, setColor1] = useState<Color>(parseColor(defaultProfile.ledsConfigs?.ledColors?.[0] ?? defaultFrontColor.toString('css')));
    const [color2, setColor2] = useState<Color>(parseColor(defaultProfile.ledsConfigs?.ledColors?.[1] ?? defaultFrontColor.toString('css')));
    const [color3, setColor3] = useState<Color>(parseColor(defaultProfile.ledsConfigs?.ledColors?.[2] ?? defaultFrontColor.toString('css')));
    const [ledBrightness, setLedBrightness] = useState<number>(defaultProfile.ledsConfigs?.ledBrightness ?? 75);
    const [ledAnimationSpeed, setLedAnimationSpeed] = useState<number>(defaultProfile.ledsConfigs?.ledAnimationSpeed ?? 1);
    const [ledEnabled, setLedEnabled] = useState<boolean>(defaultProfile.ledsConfigs?.ledEnabled ?? true);

    // 环绕灯配置
    const [aroundLedEnabled, setAroundLedEnabled] = useState<boolean>(defaultProfile.ledsConfigs?.aroundLedEnabled ?? false);
    const [aroundLedSyncToMainLed, setAroundLedSyncToMainLed] = useState<boolean>(defaultProfile.ledsConfigs?.aroundLedSyncToMainLed ?? false);
    const [aroundLedTriggerByButton, setAroundLedTriggerByButton] = useState<boolean>(defaultProfile.ledsConfigs?.aroundLedTriggerByButton ?? false);
    const [aroundLedEffectStyle, setAroundLedEffectStyle] = useState<AroundLedsEffectStyle>(defaultProfile.ledsConfigs?.aroundLedEffectStyle ?? AroundLedsEffectStyle.STATIC);
    const [aroundLedColor1, setAroundLedColor1] = useState<Color>(parseColor(defaultProfile.ledsConfigs?.aroundLedColors?.[0] ?? defaultFrontColor.toString('css')));
    const [aroundLedColor2, setAroundLedColor2] = useState<Color>(parseColor(defaultProfile.ledsConfigs?.aroundLedColors?.[1] ?? defaultFrontColor.toString('css')));
    const [aroundLedColor3, setAroundLedColor3] = useState<Color>(parseColor(defaultProfile.ledsConfigs?.aroundLedColors?.[2] ?? defaultFrontColor.toString('css')));
    const [aroundLedBrightness, setAroundLedBrightness] = useState<number>(defaultProfile.ledsConfigs?.aroundLedBrightness ?? 100);
    const [aroundLedAnimationSpeed, setAroundLedAnimationSpeed] = useState<number>(defaultProfile.ledsConfigs?.aroundLedAnimationSpeed ?? 1);


    const hasAroundLed = useMemo<boolean>(() => {
        return defaultProfile.ledsConfigs?.hasAroundLed ?? false;
    }, [defaultProfile.ledsConfigs?.hasAroundLed]);

    const aroundLedConfigIsEnabled = useMemo<boolean>(() => {
        // 如果同步主灯，则氛围灯配置无效
        return hasAroundLed && aroundLedEnabled && ledEnabled && !aroundLedSyncToMainLed;
    }, [hasAroundLed, aroundLedEnabled, ledEnabled, aroundLedSyncToMainLed]);

    const disabledKeys = useMemo<number[]>(() => {
        return defaultProfile.keysConfig?.keysEnableTag?.map((_, index) => index).filter((_, index) => !defaultProfile.keysConfig?.keysEnableTag?.[index]) ?? [];
    }, [defaultProfile?.keysConfig?.keysEnableTag]);

    // 颜色队列状态
    const [colorSwatches, setColorSwatches] = useState<string[]>(ColorQueueManager.getColorQueue());

    // 临时存储当前选择的颜色，用于在关闭时更新队列
    const [tempSelectedColors, setTempSelectedColors] = useState<{ [key: number]: string }>({});

    // 处理颜色变化的函数（仅更新颜色状态，不更新队列）
    const handleLedColorChange = (colorIndex: number, newColor: Color) => {
        const hexColor = newColor.toString('hex');

        // 更新颜色状态
        if (colorIndex === 0) setColor1(newColor);
        if (colorIndex === 1) setColor2(newColor);
        if (colorIndex === 2) setColor3(newColor);

        // 临时存储选择的颜色
        setTempSelectedColors(prev => ({
            ...prev,
            [colorIndex]: hexColor
        }));
    };

    const handleAroundLedColorChange = (colorIndex: number, newColor: Color) => {
        const hexColor = newColor.toString('hex');

        // 更新颜色状态
        if (colorIndex === 0) setAroundLedColor1(newColor);
        if (colorIndex === 1) setAroundLedColor2(newColor);
        if (colorIndex === 2) setAroundLedColor3(newColor);

        // 临时存储选择的颜色
        setTempSelectedColors(prev => ({
            ...prev,
            [colorIndex]: hexColor
        }));
    };

    // 处理 ColorPicker 关闭事件
    const handleColorPickerClose = (colorIndex: number) => {
        const selectedColor = tempSelectedColors[colorIndex];
        if (selectedColor) {
            // 更新颜色队列
            const updatedQueue = ColorQueueManager.updateColorQueue(selectedColor);
            setColorSwatches(updatedQueue);

            // 清除临时存储的颜色
            setTempSelectedColors(prev => {
                const newTemp = { ...prev };
                delete newTemp[colorIndex];
                return newTemp;
            });
        }
    };

    // 清除灯光预览数据
    useEffect(() => {
        return () => {
            if(wsConnected) {
                clearLedsPreview();
            }
        }
    }, [wsConnected]);

    const colorPickerDisabled = (index: number) => {
        return (index == 2 && !(effectStyleLabelMap.get(ledsEffectStyle)?.hasBackColor2 ?? false)) || !ledEnabled;
    }

    const aroundColorPickerDisabled = (index: number) => {
        return (index == 2 && !(aroundLedEffectStyleLabelMap.get(aroundLedEffectStyle)?.hasBackColor2 ?? false)) || !aroundLedConfigIsEnabled;
    }

    const iconMap: Record<string, JSX.Element> = {
        'static': <LuSunDim />,
        'breathing': <LuActivity />,
        'star': <LuSparkles />,
        'flowing': <LuWaves />,
        'ripple': <LuTarget />,
        'transform': <LuCloudSunRain />,
        'quake': <LuAudioLines />,
        'meteor': <TbMeteorFilled />,
    };

    const effectStyleLabelMap = new Map<LedsEffectStyle, { label: string, icon: string, hasBackColor2: boolean }>([
        [LedsEffectStyle.STATIC, {
            label: t.SETTINGS_LEDS_STATIC_LABEL,
            icon: "static",
            hasBackColor2: false
        }],
        [LedsEffectStyle.BREATHING, {
            label: t.SETTINGS_LEDS_BREATHING_LABEL,
            icon: "breathing",
            hasBackColor2: true
        }],
        [LedsEffectStyle.STAR, {
            label: t.SETTINGS_LEDS_STAR_LABEL,
            icon: "star",
            hasBackColor2: true
        }],
        [LedsEffectStyle.FLOWING, {
            label: t.SETTINGS_LEDS_FLOWING_LABEL,
            icon: "flowing",
            hasBackColor2: true
        }],
        [LedsEffectStyle.RIPPLE, {
            label: t.SETTINGS_LEDS_RIPPLE_LABEL,
            icon: "ripple",
            hasBackColor2: true
        }],
        [LedsEffectStyle.TRANSFORM, {
            label: t.SETTINGS_LEDS_TRANSFORM_LABEL,
            icon: "transform",
            hasBackColor2: true
        }],
    ]);

    const aroundLedEffectStyleLabelMap = new Map<AroundLedsEffectStyle, { label: string, icon: string, hasBackColor2: boolean }>([
        [AroundLedsEffectStyle.STATIC, {
            label: t.SETTINGS_LEDS_STATIC_LABEL,
            icon: "static",
            hasBackColor2: false
        }],
        [AroundLedsEffectStyle.BREATHING, {
            label: t.SETTINGS_LEDS_BREATHING_LABEL,
            icon: "breathing",
            hasBackColor2: true
        }],
        [AroundLedsEffectStyle.QUAKE, {
            label: t.SETTINGS_LEDS_QUAKE_LABEL,
            icon: "quake",
            hasBackColor2: true
        }],
        [AroundLedsEffectStyle.METEOR, {
            label: t.SETTINGS_LEDS_METEOR_LABEL,
            icon: "meteor",
            hasBackColor2: true
        }],
    ]);

    const colorLabels = [
        t.SETTINGS_LEDS_FRONT_COLOR,
        t.SETTINGS_LEDS_BACK_COLOR1,
        t.SETTINGS_LEDS_BACK_COLOR2
    ];

    const aroundColorLabels = [
        t.SETTINGS_LEDS_AMBIENT_LIGHT_COLOR1,
        t.SETTINGS_LEDS_AMBIENT_LIGHT_COLOR2
    ];

    const previewLedsEffectHandler = () => {
        const config = {
            ledEnabled: ledEnabled,
            ledsEffectStyle: ledsEffectStyle,
            ledColors: [color1.toString('hex'), color2.toString('hex'), color3.toString('hex')],
            ledBrightness: ledBrightness,
            ledAnimationSpeed: ledAnimationSpeed,

            aroundLedEnabled: aroundLedEnabled,
            aroundLedSyncToMainLed: aroundLedSyncToMainLed,
            aroundLedTriggerByButton: aroundLedTriggerByButton,
            aroundLedEffectStyle: aroundLedEffectStyle,
            aroundLedColors: [aroundLedColor1.toString('hex'), aroundLedColor2.toString('hex'), aroundLedColor3.toString('hex')],
            aroundLedBrightness: aroundLedBrightness,
            aroundLedAnimationSpeed: aroundLedAnimationSpeed,
        }
        pushLedsConfig(config);
    }

    const updateLedsConfigHandler = () => {
        const newConfig = Object.assign({
            ledEnabled: ledEnabled,
            ledsEffectStyle: ledsEffectStyle,
            ledColors: [color1.toString('hex'), color2.toString('hex'), color3.toString('hex')],
            ledBrightness: ledBrightness,
            ledAnimationSpeed: ledAnimationSpeed,

            aroundLedEnabled: aroundLedEnabled,
            aroundLedSyncToMainLed: aroundLedSyncToMainLed,
            aroundLedTriggerByButton: aroundLedTriggerByButton,
            aroundLedEffectStyle: aroundLedEffectStyle,
            aroundLedColors: [aroundLedColor1.toString('hex'), aroundLedColor2.toString('hex'), aroundLedColor3.toString('hex')],
            aroundLedBrightness: aroundLedBrightness,
            aroundLedAnimationSpeed: aroundLedAnimationSpeed,
        });

        const newProfileDetails: GameProfile = {
            id: defaultProfile.id,
            ledsConfigs: newConfig,
        }
        updateProfileDetails(defaultProfile.id, newProfileDetails);
    }

    // Initialize the state with the default profile details
    useEffect(() => {

        if(isInit) {
            return;
        } else if(defaultProfile.ledsConfigs) {

            const ledsConfigs = defaultProfile.ledsConfigs;

            setLedsEffectStyle(ledsConfigs.ledsEffectStyle ?? LedsEffectStyle.STATIC);
            setColor1(parseColor(ledsConfigs.ledColors?.[0] ?? defaultFrontColor.toString('css')));
            setColor2(parseColor(ledsConfigs.ledColors?.[1] ?? defaultFrontColor.toString('css')));
            setColor3(parseColor(ledsConfigs.ledColors?.[2] ?? defaultFrontColor.toString('css')));
            setLedBrightness(ledsConfigs.ledBrightness ?? 75);
            setLedAnimationSpeed(ledsConfigs.ledAnimationSpeed ?? 3);
            setLedEnabled(ledsConfigs.ledEnabled ?? true);

            setAroundLedEnabled(ledsConfigs.aroundLedEnabled ?? false);
            setAroundLedSyncToMainLed(ledsConfigs.aroundLedSyncToMainLed ?? false);
            setAroundLedTriggerByButton(ledsConfigs.aroundLedTriggerByButton ?? false);
            setAroundLedEffectStyle(ledsConfigs.aroundLedEffectStyle ?? AroundLedsEffectStyle.STATIC);
            setAroundLedColor1(parseColor(ledsConfigs.aroundLedColors?.[0] ?? defaultFrontColor.toString('css')));
            setAroundLedColor2(parseColor(ledsConfigs.aroundLedColors?.[1] ?? defaultFrontColor.toString('css')));
            setAroundLedColor3(parseColor(ledsConfigs.aroundLedColors?.[2] ?? defaultFrontColor.toString('css')));
            setAroundLedBrightness(ledsConfigs.aroundLedBrightness ?? 75);
            setAroundLedAnimationSpeed(ledsConfigs.aroundLedAnimationSpeed ?? 3);

            setIsInit(true);
            setNeedPreview(true);
        }

    }, [defaultProfile]);
    
    useEffect(() => {
        if(needPreview) {
            previewLedsEffectHandler();
            setNeedPreview(false);
        }
    }, [needPreview]);

    useEffect(() => {
        if(needUpdate) {
            updateLedsConfigHandler();
            setNeedUpdate(false);
        }
    }, [needUpdate]);

    return (
        <>
            <Flex direction="row" width={"100%"} height={"100%"} padding={"18px"}  >
                <Flex flex={0} justifyContent={"flex-start"} height="fit-content" >
                    <ProfileSelect />
                </Flex>
                <Center flex={1} justifyContent={"center"} flexDirection={"column"}  >
                    <Center padding="80px 30px" position="relative" flex={1}  >
                        <HitboxLeds
                            hasText={false}
                            ledsConfig={{
                                ledEnabled: ledEnabled,
                                ledsEffectStyle: ledsEffectStyle,
                                ledColors: [
                                    GamePadColor.fromString(color1.toString('hex')),
                                    GamePadColor.fromString(color2.toString('hex')),
                                    GamePadColor.fromString(color3.toString('hex'))
                                ],
                                brightness: ledBrightness,
                                animationSpeed: ledAnimationSpeed,

                                hasAroundLed: hasAroundLed,
                                aroundLedEnabled: aroundLedEnabled,
                                aroundLedSyncToMainLed: aroundLedSyncToMainLed,
                                aroundLedTriggerByButton: aroundLedTriggerByButton,
                                aroundLedEffectStyle: aroundLedEffectStyle,
                                aroundLedColors: [
                                    GamePadColor.fromString(aroundLedColor1.toString('hex')),
                                    GamePadColor.fromString(aroundLedColor2.toString('hex')),
                                    GamePadColor.fromString(aroundLedColor3.toString('hex'))
                                ],
                                aroundLedBrightness: aroundLedBrightness,
                                aroundLedAnimationSpeed: aroundLedAnimationSpeed,
                            }}
                            interactiveIds={LEDS_SETTINGS_INTERACTIVE_IDS}
                            disabledKeys={disabledKeys}
                        />
                    </Center>
                    <Center flex={0}  >
                        <ContentActionButtons />
                    </Center>
                </Center>
                <Center >
                    <Card.Root w="778px" h="100%" >
                        <Card.Header>
                            <Card.Title fontSize={"md"}  >
                                <Text fontSize={"32px"} fontWeight={"normal"} color={"green.600"} >{t.SETTINGS_LEDS_TITLE}</Text>
                            </Card.Title>
                            <Card.Description fontSize={"sm"} pt={4} pb={4} whiteSpace="pre-wrap"  >
                                {t.SETTINGS_LEDS_HELPER_TEXT}
                            </Card.Description>
                        </Card.Header>
                        <Card.Body>
                            <Fieldset.Root>
                                <Stack direction={"column"} >

                                    <Fieldset.Content  >
                                        <VStack gap={8} alignItems={"flex-start"}  >
                                            {/* LED Effect Style */}

                                            <Switch.Root colorPalette={"green"} checked={ledEnabled}
                                                onCheckedChange={() => {
                                                    setLedEnabled(!ledEnabled);
                                                    setNeedUpdate(true);
                                                    setNeedPreview(true);
                                                }}

                                            >
                                                <Switch.HiddenInput />
                                                <Switch.Control>
                                                    <Switch.Thumb />
                                                </Switch.Control>
                                                <Switch.Label>{t.SETTINGS_LEDS_ENABLE_LABEL}</Switch.Label>
                                            </Switch.Root>
                                            {/* LED Effect Style */}
                                            <RadioCardRoot
                                                align="center"
                                                justify="center"
                                                colorPalette={ledEnabled ? "green" : "gray"}
                                                size={"sm"}
                                                variant={ colorMode === "dark" ? "subtle" : "solid"}
                                                value={ledsEffectStyle?.toString() ?? LedsEffectStyle.STATIC.toString()}
                                                onValueChange={(detail) => {
                                                    const newEffectStyle = parseInt(detail.value ?? "0") as LedsEffectStyle;
                                                    setLedsEffectStyle(newEffectStyle);
                                                    setNeedUpdate(true);
                                                    setNeedPreview(true);
                                                }}
                                                disabled={!ledEnabled}
                                            >
                                                <RadioCardLabel>{t.SETTINGS_LEDS_EFFECT_STYLE_CHOICE}</RadioCardLabel>
                                                <SimpleGrid columns={6} gap={1} >
                                                    {Array.from(effectStyleLabelMap.entries()).map(([style], index) => (
                                                        // <Tooltip key={index} content={effectStyleLabelMap.get(style)?.description ?? ""} >
                                                        <RadioCardItem
                                                            w="120px"
                                                            fontSize={"xs"}
                                                            indicator={false}
                                                            key={index}
                                                            icon={
                                                                <Icon fontSize={"2xl"} >
                                                                    {iconMap[effectStyleLabelMap.get(style)?.icon ?? ""]}
                                                                </Icon>
                                                            }
                                                            value={style?.toString() ?? ""}
                                                            label={effectStyleLabelMap.get(style)?.label ?? ""}
                                                            disabled={!ledEnabled}
                                                        />
                                                        // </Tooltip>
                                                    ))}
                                                </SimpleGrid>
                                            </RadioCardRoot>

                                            {/* LED Colors */}
                                            <HStack gap={4} alignItems={"flex-start"} >
                                                {Array.from({ length: 3 }).map((_, index) => (
                                                    <ColorPicker.Root key={index} size="xs"
                                                        value={
                                                            index === 0 ? color1 :
                                                                index === 1 ? color2 :
                                                                    index === 2 ? color3 :
                                                                        parseColor(color1.toString('hex'))}

                                                        onValueChange={(e) => {
                                                            handleLedColorChange(index, e.value);
                                                        }}

                                                        onValueChangeEnd={() => {
                                                            setNeedUpdate(true);
                                                            setNeedPreview(true);
                                                        }}

                                                        onOpenChange={(details) => {
                                                            // 当 ColorPicker 关闭时更新颜色队列
                                                            if (!details.open) {
                                                                handleColorPickerClose(index);
                                                            }
                                                        }}

                                                        maxW="200px"
                                                        disabled={colorPickerDisabled(index)}
                                                    >
                                                        <ColorPicker.Label> {colorLabels[index]} </ColorPicker.Label>
                                                        <ColorPicker.HiddenInput />
                                                        <ColorPicker.Control>
                                                            <ColorPicker.Input />
                                                            <ColorPicker.Trigger />
                                                        </ColorPicker.Control>
                                                        <Portal>
                                                            <ColorPicker.Positioner>
                                                                <ColorPicker.Content>
                                                                    <ColorPicker.Area />
                                                                    <HStack>
                                                                        <ColorPicker.EyeDropper size="sm" variant="outline" />
                                                                        <ColorPicker.ChannelSlider channel="hue" />
                                                                    </HStack>
                                                                    <ColorPicker.SwatchGroup gap={.5} p={0.5} >
                                                                        {colorSwatches.map((item) => (
                                                                            <ColorPicker.SwatchTrigger key={item} value={item}>
                                                                                <ColorPicker.Swatch value={item} boxSize="5">
                                                                                    <ColorPicker.SwatchIndicator>
                                                                                        <LuCheck />
                                                                                    </ColorPicker.SwatchIndicator>
                                                                                </ColorPicker.Swatch>
                                                                            </ColorPicker.SwatchTrigger>
                                                                        ))}
                                                                    </ColorPicker.SwatchGroup>
                                                                </ColorPicker.Content>
                                                            </ColorPicker.Positioner>
                                                        </Portal>
                                                    </ColorPicker.Root>
                                                ))}
                                            </HStack>
                                            {/* LED Brightness */}
                                            <Grid templateColumns="repeat(2, 1fr)" gap={10} w="100%" >
                                                <Slider.Root
                                                    size={"sm"}
                                                    min={0}
                                                    max={100}
                                                    step={1}
                                                    colorPalette={"green"}
                                                    disabled={!ledEnabled}
                                                    value={[ledBrightness]}
                                                    onValueChange={(e) => {
                                                        setLedBrightness(e.value[0]);
                                                    }}

                                                    onValueChangeEnd={() => {
                                                        setNeedUpdate(true);
                                                        setNeedPreview(true);
                                                    }}
                                                >
                                                    <HStack justifyContent={"space-between"} >
                                                        <Slider.Label color={ledEnabled ? "white" : "gray"} >{t.SETTINGS_LEDS_BRIGHTNESS_LABEL}</Slider.Label>
                                                        <Slider.ValueText color={ledEnabled ? "white" : "gray"} />
                                                    </HStack>
                                                    <Slider.Control>
                                                        <Slider.Track>
                                                            <Slider.Range />
                                                        </Slider.Track>
                                                        <Slider.Thumb index={0} >
                                                            <Slider.DraggingIndicator
                                                                layerStyle="fill.solid"
                                                                top="6"
                                                                rounded="sm"
                                                                px="1.5"
                                                            >
                                                                <Slider.ValueText />
                                                            </Slider.DraggingIndicator>
                                                        </Slider.Thumb>
                                                        <Slider.Marks marks={[
                                                            { value: 0, label: "0" },
                                                            { value: 25, label: "25" },
                                                            { value: 50, label: "50" },
                                                            { value: 75, label: "75" },
                                                            { value: 100, label: "100" },
                                                        ]} />
                                                    </Slider.Control>
                                                </Slider.Root>

                                                {/* LED Animation Speed */}
                                                <Slider.Root
                                                    size={"sm"}
                                                    min={1}
                                                    max={5}
                                                    step={1}
                                                    colorPalette={"green"}
                                                    disabled={!ledEnabled || ledsEffectStyle == LedsEffectStyle.STATIC}
                                                    value={[ledAnimationSpeed]}
                                                    onValueChange={(e) => {
                                                        setLedAnimationSpeed(e.value[0]);
                                                    }}

                                                    onValueChangeEnd={() => {
                                                        setNeedUpdate(true);
                                                        setNeedPreview(true);
                                                    }}

                                                >
                                                    <HStack justifyContent={"space-between"} >
                                                        <Slider.Label color={ledEnabled ? "white" : "gray"} >{t.SETTINGS_LEDS_ANIMATION_SPEED_LABEL}</Slider.Label>
                                                        <Slider.ValueText color={ledEnabled ? "white" : "gray"} />
                                                    </HStack>
                                                    <Slider.Control>
                                                        <Slider.Track>
                                                            <Slider.Range />
                                                        </Slider.Track>
                                                        <Slider.Thumb index={0} >
                                                            <Slider.DraggingIndicator
                                                                layerStyle="fill.solid"
                                                                top="6"
                                                                rounded="sm"
                                                                px="1.5"
                                                            >
                                                                <Slider.ValueText />
                                                            </Slider.DraggingIndicator>
                                                        </Slider.Thumb>
                                                        <Slider.Marks marks={[
                                                            { value: 1, label: "1x" },
                                                            { value: 2, label: "2x" },
                                                            { value: 3, label: "3x" },
                                                            { value: 4, label: "4x" },
                                                            { value: 5, label: "5x" },
                                                        ]} />
                                                    </Slider.Control>
                                                </Slider.Root>
                                            </Grid>
                                        </VStack>
                                        <HStack display={hasAroundLed ? "flex" : "none"}  >
                                            <Text flexShrink="0" fontSize={"sm"} fontWeight={"bold"} >{t.SETTINGS_LEDS_AMBIENT_LIGHT_TITLE}</Text>
                                            <Separator my={8} flex="1" />
                                        </HStack>
                                        <VStack gap={8} alignItems={"flex-start"} display={hasAroundLed ? "flex" : "none"}  >
                                            {/* Ambient Light */}
                                            <Grid templateColumns="repeat(3, 1fr)" gap={10} w="100%" >
                                                {/* 是否开启氛围灯 */}
                                                <Switch.Root colorPalette={"green"}
                                                    disabled={!ledEnabled}
                                                    checked={aroundLedEnabled}
                                                    onCheckedChange={() => {
                                                        setAroundLedEnabled(!aroundLedEnabled);
                                                        setNeedUpdate(true);
                                                        setNeedPreview(true);
                                                    }}
                                                >
                                                    <Switch.HiddenInput />
                                                    <Switch.Control>
                                                        <Switch.Thumb />
                                                    </Switch.Control>
                                                    <Switch.Label>{t.SETTINGS_AMBIENT_LIGHT_ENABLE_LABEL}</Switch.Label>
                                                </Switch.Root>
                                                {/* 氛围灯是否同步主灯 */}
                                                <Switch.Root colorPalette={"green"}
                                                    disabled={!ledEnabled || !aroundLedEnabled}
                                                    checked={aroundLedSyncToMainLed}
                                                    onCheckedChange={() => {
                                                        setAroundLedSyncToMainLed(!aroundLedSyncToMainLed);
                                                        setNeedUpdate(true);
                                                        setNeedPreview(true);
                                                    }}
                                                >
                                                    <Switch.HiddenInput />
                                                    <Switch.Control>
                                                        <Switch.Thumb />
                                                    </Switch.Control>
                                                    <Switch.Label>{t.SETTINGS_AMBIENT_LIGHT_SYNC_WITH_LEDS_LABEL}</Switch.Label>
                                                </Switch.Root>
                                                {/* 氛围灯是否触发按钮 */}
                                                <Switch.Root colorPalette={"green"}
                                                    disabled={!aroundLedConfigIsEnabled}
                                                    checked={aroundLedTriggerByButton}
                                                    onCheckedChange={() => {
                                                        setAroundLedTriggerByButton(!aroundLedTriggerByButton);
                                                        setNeedUpdate(true);
                                                        setNeedPreview(true);
                                                    }}
                                                >
                                                    <Switch.HiddenInput />
                                                    <Switch.Control>
                                                        <Switch.Thumb />
                                                    </Switch.Control>
                                                    <Switch.Label>{t.SETTINGS_AMBIENT_LIGHT_TRIGGER_BY_BUTTON_LABEL}</Switch.Label>
                                                </Switch.Root>
                                            </Grid>
                                            <HStack gap={10} alignItems={"flex-start"} >
                                                {/* 氛围灯效果 */}
                                                <RadioCardRoot
                                                    align="center"
                                                    justify="center"
                                                    colorPalette={aroundLedConfigIsEnabled ? "green" : "gray"}
                                                    size={"sm"}
                                                    variant={ colorMode === "dark" ? "subtle" : "solid"}
                                                    value={aroundLedEffectStyle?.toString() ?? AroundLedsEffectStyle.STATIC.toString()}
                                                    onValueChange={(detail) => {
                                                        const newAroundLedEffectStyle = parseInt(detail.value ?? "0") as AroundLedsEffectStyle;
                                                        setAroundLedEffectStyle(newAroundLedEffectStyle);
                                                        setNeedUpdate(true);
                                                        setNeedPreview(true);
                                                    }}
                                                    disabled={!aroundLedConfigIsEnabled}
                                                >
                                                    <RadioCardLabel>{t.SETTINGS_AMBIENT_LIGHT_EFFECT_LABEL}</RadioCardLabel>
                                                    <SimpleGrid columns={6} gap={1} >
                                                        {Array.from(aroundLedEffectStyleLabelMap.entries()).map(([style], index) => (
                                                            <RadioCardItem
                                                                w="120px"
                                                                fontSize={"xs"}
                                                                indicator={false}
                                                                key={index}
                                                                icon={<Icon fontSize={"2xl"} >{iconMap[aroundLedEffectStyleLabelMap.get(style)?.icon ?? ""]}</Icon>}
                                                                value={style.toString()}
                                                                label={aroundLedEffectStyleLabelMap.get(style)?.label ?? ""}
                                                                disabled={!aroundLedConfigIsEnabled}
                                                            />
                                                        ))}
                                                    </SimpleGrid>
                                                </RadioCardRoot>
                                            </HStack>
                                            <HStack gap={10} alignItems={"flex-start"} >
                                                {/* 氛围灯颜色 */}
                                                {Array.from({ length: 2 }).map((_, index) => (
                                                    <ColorPicker.Root key={index} size="xs"
                                                        value={
                                                            index === 0 ? aroundLedColor1 :
                                                                index === 1 ? aroundLedColor2 :
                                                                    parseColor(aroundLedColor1.toString('hex'))}

                                                        onValueChange={(e) => {
                                                            handleAroundLedColorChange(index, e.value);
                                                        }}

                                                        onValueChangeEnd={() => {
                                                            setNeedUpdate(true);
                                                            setNeedPreview(true);
                                                        }}

                                                        onOpenChange={(details) => {
                                                            // 当 ColorPicker 关闭时更新颜色队列
                                                            if (!details.open) {
                                                                handleColorPickerClose(index);
                                                            }
                                                        }}

                                                        maxW="200px"
                                                        disabled={aroundColorPickerDisabled(index)}
                                                    >
                                                        <ColorPicker.Label> {aroundColorLabels[index]} </ColorPicker.Label>
                                                        <ColorPicker.HiddenInput />
                                                        <ColorPicker.Control>
                                                            <ColorPicker.Input />
                                                            <ColorPicker.Trigger />
                                                        </ColorPicker.Control>
                                                        <Portal>
                                                            <ColorPicker.Positioner>
                                                                <ColorPicker.Content>
                                                                    <ColorPicker.Area />
                                                                    <HStack>
                                                                        <ColorPicker.EyeDropper size="sm" variant="outline" />
                                                                        <ColorPicker.ChannelSlider channel="hue" />
                                                                    </HStack>
                                                                    <ColorPicker.SwatchGroup gap={.5} p={0.5} >
                                                                        {colorSwatches.map((item) => (
                                                                            <ColorPicker.SwatchTrigger key={item} value={item}>
                                                                                <ColorPicker.Swatch value={item} boxSize="5">
                                                                                    <ColorPicker.SwatchIndicator>
                                                                                        <LuCheck />
                                                                                    </ColorPicker.SwatchIndicator>
                                                                                </ColorPicker.Swatch>
                                                                            </ColorPicker.SwatchTrigger>
                                                                        ))}
                                                                    </ColorPicker.SwatchGroup>
                                                                </ColorPicker.Content>
                                                            </ColorPicker.Positioner>
                                                        </Portal>
                                                    </ColorPicker.Root>
                                                ))}
                                            </HStack>
                                            <Grid templateColumns="repeat(2, 1fr)" gap={10} w="100%" >
                                                {/* 氛围灯亮度 */}
                                                <Slider.Root
                                                    size={"sm"}
                                                    min={0}
                                                    max={100}
                                                    step={1}
                                                    colorPalette={"green"}
                                                    disabled={!aroundLedConfigIsEnabled}
                                                    value={[aroundLedBrightness]}
                                                    onValueChange={(e) => {
                                                        setAroundLedBrightness(e.value[0]);
                                                    }}

                                                    onValueChangeEnd={() => {
                                                        setNeedUpdate(true);
                                                        setNeedPreview(true);
                                                    }}
                                                >
                                                    <HStack justifyContent={"space-between"} >
                                                        <Slider.Label color={aroundLedConfigIsEnabled ? "white" : "gray"} >{t.SETTINGS_AMBIENT_LIGHT_BRIGHTNESS_LABEL}</Slider.Label>
                                                        <Slider.ValueText color={aroundLedConfigIsEnabled ? "white" : "gray"} />
                                                    </HStack>
                                                    <Slider.Control>
                                                        <Slider.Track>
                                                            <Slider.Range />
                                                        </Slider.Track>
                                                        <Slider.Thumb index={0} >
                                                            <Slider.DraggingIndicator
                                                                layerStyle="fill.solid"
                                                                top="6"
                                                                rounded="sm"
                                                                px="1.5"
                                                            >
                                                                <Slider.ValueText />
                                                            </Slider.DraggingIndicator>
                                                        </Slider.Thumb>
                                                        <Slider.Marks marks={[
                                                            { value: 0, label: "0" },
                                                            { value: 25, label: "25" },
                                                            { value: 50, label: "50" },
                                                            { value: 75, label: "75" },
                                                            { value: 100, label: "100" },
                                                        ]} />
                                                    </Slider.Control>
                                                </Slider.Root>

                                                {/* LED Animation Speed */}
                                                <Slider.Root
                                                    size={"sm"}
                                                    min={1}
                                                    max={5}
                                                    step={1}
                                                    colorPalette={"green"}
                                                    disabled={!aroundLedConfigIsEnabled || aroundLedEffectStyle == AroundLedsEffectStyle.STATIC}
                                                    value={[aroundLedAnimationSpeed]}
                                                    onValueChange={(e) => {
                                                        setAroundLedAnimationSpeed(e.value[0]);
                                                    }}

                                                    onValueChangeEnd={() => {
                                                        setNeedUpdate(true);
                                                        setNeedPreview(true);
                                                    }}

                                                >
                                                    <HStack justifyContent={"space-between"} >
                                                        <Slider.Label color={aroundLedConfigIsEnabled ? "white" : "gray"} >{t.SETTINGS_AMBIENT_LIGHT_ANIMATION_SPEED_LABEL}</Slider.Label>
                                                        <Slider.ValueText color={aroundLedConfigIsEnabled ? "white" : "gray"} />
                                                    </HStack>
                                                    <Slider.Control>
                                                        <Slider.Track>
                                                            <Slider.Range />
                                                        </Slider.Track>
                                                        <Slider.Thumb index={0} >
                                                            <Slider.DraggingIndicator
                                                                layerStyle="fill.solid"
                                                                top="6"
                                                                rounded="sm"
                                                                px="1.5"
                                                            >
                                                                <Slider.ValueText />
                                                            </Slider.DraggingIndicator>
                                                        </Slider.Thumb>
                                                        <Slider.Marks marks={[
                                                            { value: 1, label: "1x" },
                                                            { value: 2, label: "2x" },
                                                            { value: 3, label: "3x" },
                                                            { value: 4, label: "4x" },
                                                            { value: 5, label: "5x" },
                                                        ]} />
                                                    </Slider.Control>
                                                </Slider.Root>
                                            </Grid>
                                        </VStack>

                                    </Fieldset.Content>
                                </Stack>
                            </Fieldset.Root>
                        </Card.Body>
                    </Card.Root>
                </Center>
            </Flex>
        </>
    )
}
