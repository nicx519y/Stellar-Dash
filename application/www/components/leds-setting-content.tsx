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
} from "@chakra-ui/react";

import {
    RadioCardItem,
    RadioCardRoot,
} from "@/components/ui/radio-card";

import { ColorPicker, Slider } from "@chakra-ui/react";

import { useEffect, useMemo, useState } from "react";
import {
    GameProfile,
    LedsEffectStyle,
    LEDS_SETTINGS_INTERACTIVE_IDS,
} from "@/types/gamepad-config";
import { LuSunDim, LuActivity, LuCheck, LuSparkles, LuWaves, LuTarget, LuCloudSunRain } from "react-icons/lu";
import Hitbox from "./hitbox";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import useUnsavedChangesWarning from "@/hooks/use-unsaved-changes-warning";
import { useLanguage } from "@/contexts/language-context";
import { useColorMode } from "./ui/color-mode";
import { ContentActionButtons } from "@/components/content-action-buttons";
import { GamePadColor } from "@/types/gamepad-color";
import { ProfileSelect } from "./profile-select";
import { ColorQueueManager } from "@/components/color-queue-manager";

export function LEDsSettingContent() {
    const { t } = useLanguage();

    const [_isDirty, setIsDirty] = useUnsavedChangesWarning();
    const { defaultProfile, updateProfileDetails, resetProfileDetails } = useGamepadConfig();
    const { colorMode } = useColorMode();
    const defaultFrontColor = useMemo(() => {
        if (colorMode === "dark") {
            return parseColor("#000000");
        } else {
            return parseColor("#ffffff");
        }
    }, [colorMode]);

    const [ledsEffectStyle, setLedsEffectStyle] = useState<LedsEffectStyle>(LedsEffectStyle.STATIC);
    const [color1, setColor1] = useState<Color>(defaultFrontColor);
    const [color2, setColor2] = useState<Color>(defaultFrontColor);
    const [color3, setColor3] = useState<Color>(defaultFrontColor);
    const [ledBrightness, setLedBrightness] = useState<number>(75);
    const [ledAnimationSpeed, setLedAnimationSpeed] = useState<number>(1);
    const [ledEnabled, setLedEnabled] = useState<boolean>(true);
    
    // 颜色队列状态
    const [colorSwatches, setColorSwatches] = useState<string[]>(ColorQueueManager.getColorQueue());
    
    // 临时存储当前选择的颜色，用于在关闭时更新队列
    const [tempSelectedColors, setTempSelectedColors] = useState<{[key: number]: string}>({});

    // 处理颜色变化的函数（仅更新颜色状态，不更新队列）
    const handleColorChange = (colorIndex: number, newColor: Color) => {
        setIsDirty?.(true);
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

    // Initialize the state with the default profile details
    useEffect(() => {
        const ledsConfigs = defaultProfile.ledsConfigs;
        console.log("ledsConfigs: ", ledsConfigs);
        if (ledsConfigs) {
            setLedsEffectStyle(ledsConfigs.ledsEffectStyle ?? LedsEffectStyle.STATIC);
            setColor1(parseColor(ledsConfigs.ledColors?.[0] ?? defaultFrontColor.toString('css')));
            setColor2(parseColor(ledsConfigs.ledColors?.[1] ?? defaultFrontColor.toString('css')));
            setColor3(parseColor(ledsConfigs.ledColors?.[2] ?? defaultFrontColor.toString('css')));
            setLedBrightness(ledsConfigs.ledBrightness ?? 75);
            setLedAnimationSpeed(ledsConfigs.ledAnimationSpeed ?? 1);
            setLedEnabled(ledsConfigs.ledEnabled ?? true);
            setIsDirty?.(false);

        }

        
        
    }, [defaultProfile]);

    // Save the profile details
    const saveProfileDetailsHandler = (): Promise<void> => {
        const newProfileDetails = {
            id: defaultProfile.id,
            ledsConfigs: {
                ledEnabled: ledEnabled,
                ledsEffectStyle: ledsEffectStyle,
                ledColors: [color1.toString('hex'), color2.toString('hex'), color3.toString('hex')],
                ledBrightness: ledBrightness,
                ledAnimationSpeed: ledAnimationSpeed,
            }
        }

        console.log("saveProfileDetailHandler: ", newProfileDetails);
        return updateProfileDetails(defaultProfile.id, newProfileDetails as GameProfile);
    }

    const colorPickerDisabled = (index: number) => {
        return (index == 2 && !(effectStyleLabelMap.get(ledsEffectStyle)?.hasBackColor2 ?? false)) || !ledEnabled;
    }

    const iconMap: Record<string, JSX.Element> = {
        'sun-dim': <LuSunDim />,
        'activity': <LuActivity />,
        'star': <LuSparkles />,
        'flowing': <LuWaves  />,
        'ripple': <LuTarget  />,
        'transform': <LuCloudSunRain  />,
    };

    const effectStyleLabelMap = new Map<LedsEffectStyle, { label: string, description: string, icon: string, hasBackColor2: boolean }>([
        [LedsEffectStyle.STATIC, {
            label: t.SETTINGS_LEDS_STATIC_LABEL,
            description: t.SETTINGS_LEDS_STATIC_DESC,
            icon: "sun-dim",
            hasBackColor2: false
        }],
        [LedsEffectStyle.BREATHING, {
            label: t.SETTINGS_LEDS_BREATHING_LABEL,
            description: t.SETTINGS_LEDS_BREATHING_DESC,
            icon: "activity",
            hasBackColor2: true
        }],
        [LedsEffectStyle.STAR, {
            label: t.SETTINGS_LEDS_STAR_LABEL,
            description: t.SETTINGS_LEDS_STAR_DESC,
            icon: "star",
            hasBackColor2: true
        }],
        [LedsEffectStyle.FLOWING, {
            label: t.SETTINGS_LEDS_FLOWING_LABEL,
            description: t.SETTINGS_LEDS_FLOWING_DESC,
            icon: "flowing",
            hasBackColor2: true
        }],
        [LedsEffectStyle.RIPPLE, {
            label: t.SETTINGS_LEDS_RIPPLE_LABEL,
            description: t.SETTINGS_LEDS_RIPPLE_DESC,
            icon: "ripple",
            hasBackColor2: true
        }],
        [LedsEffectStyle.TRANSFORM, {
            label: t.SETTINGS_LEDS_TRANSFORM_LABEL,
            description: t.SETTINGS_LEDS_TRANSFORM_DESC,
            icon: "transform",
            hasBackColor2: true
        }],
    ]);

    const colorLabels = [
        t.SETTINGS_LEDS_FRONT_COLOR,
        t.SETTINGS_LEDS_BACK_COLOR1,
        t.SETTINGS_LEDS_BACK_COLOR2
    ];

    return (
        <>
            <Flex direction="row" width={"100%"} height={"100%"} padding={"18px"}  >
                <Center >
                    <ProfileSelect />
                </Center>
                <Center flex={1}  >
                    <Hitbox
                        hasText={false}
                        colorEnabled={ledEnabled}
                        frontColor={GamePadColor.fromString(color1.toString('hex'))}
                        backColor1={GamePadColor.fromString(color2.toString('hex'))}
                        backColor2={GamePadColor.fromString(color3.toString('hex'))}
                        effectStyle={ledsEffectStyle}
                        brightness={ledBrightness}
                        animationSpeed={ledAnimationSpeed}
                        interactiveIds={LEDS_SETTINGS_INTERACTIVE_IDS}
                    />
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
                                                        setIsDirty?.(true);
                                                    }} >
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
                                                    variant={colorMode === "dark" ? "surface" : "outline"}
                                                    value={ledsEffectStyle?.toString() ?? LedsEffectStyle.STATIC.toString()}
                                                    onValueChange={(detail) => {
                                                        setLedsEffectStyle(parseInt(detail.value ?? "0") as LedsEffectStyle);
                                                        setIsDirty?.(true);
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
                                                                value={style.toString()}
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
                                                        <ColorPicker.Root key={index} 
                                                            value={ 
                                                                index === 0 ? color1 :
                                                                        index === 1 ? color2 :
                                                                            index === 2 ? color3 :
                                                            parseColor(color1.toString('hex')) } 
                                                            
                                                            onValueChange={(e) => {
                                                                handleColorChange(index, e.value);
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

                                                <Slider.Root 
                                                    width="580px" 
                                                    min={0}
                                                    max={100}
                                                    step={1}
                                                    colorPalette={"green"}
                                                    disabled={!ledEnabled}
                                                    value={[ledBrightness]}
                                                    onValueChange={(e) => {
                                                        setLedBrightness(e.value[0]);
                                                        setIsDirty?.(true);
                                                    }}
                                                    
                                                >
                                                    <HStack justifyContent={"space-between"} >
                                                        <Slider.Label>{t.SETTINGS_LEDS_BRIGHTNESS_LABEL}</Slider.Label>
                                                        <Slider.ValueText />
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
                                                    width="580px" 
                                                    min={1}
                                                    max={5}
                                                    step={1}
                                                    colorPalette={"green"}
                                                    disabled={!ledEnabled || ledsEffectStyle == LedsEffectStyle.STATIC}
                                                    value={[ledAnimationSpeed]}
                                                    onValueChange={(e) => {
                                                        setLedAnimationSpeed(e.value[0]);
                                                        setIsDirty?.(true);
                                                    }}
                                                    
                                                >
                                                    <HStack justifyContent={"space-between"} >
                                                        <Slider.Label>{t.SETTINGS_LEDS_ANIMATION_SPEED_LABEL}</Slider.Label>
                                                        <Slider.ValueText />
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
                                           
                                        </VStack>
                                    </Fieldset.Content>
                                </Stack>
                            </Fieldset.Root>
                        </Card.Body>
                        <Card.Footer justifyContent={"flex-start"} >
                            <ContentActionButtons
                                isDirty={_isDirty}
                                resetLabel={t.BUTTON_RESET}
                                saveLabel={t.BUTTON_SAVE}
                                resetHandler={resetProfileDetails}
                                saveHandler={saveProfileDetailsHandler}
                            />
                        </Card.Footer>
                    </Card.Root>
                </Center>
            </Flex>
        </>
    )
}
