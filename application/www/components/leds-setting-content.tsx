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
} from "@chakra-ui/react";

import { Field } from "@/components/ui/field"
import { Slider } from "@/components/ui/slider"
import { Switch } from "@/components/ui/switch"
import {
    RadioCardItem,
    RadioCardRoot,
} from "@/components/ui/radio-card"

import {
    ColorPickerArea,
    ColorPickerContent,
    ColorPickerControl,
    ColorPickerInput,
    ColorPickerLabel,
    ColorPickerRoot,
    ColorPickerSliders,
    ColorPickerTrigger,
} from "@/components/ui/color-picker"

import { useEffect, useMemo, useState } from "react";
import {
    GameProfile,
    LedsEffectStyle,
    LedsEffectStyleList,
    LedsEffectStyleLabelMap,
    LEDS_SETTINGS_INTERACTIVE_IDS,
} from "@/types/gamepad-config";
import { LuSunDim, LuActivity } from "react-icons/lu";
import Hitbox from "./hitbox";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import useUnsavedChangesWarning from "@/hooks/use-unsaved-changes-warning";
import { useLanguage } from "@/contexts/language-context";
import { useColorMode } from "./ui/color-mode";
import { ContentActionButtons } from "@/components/content-action-buttons";
import { GamePadColor } from "@/types/gamepad-color";
import { ProfileSelect } from "./profile-select";

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
    const [ledEnabled, setLedEnabled] = useState<boolean>(true);

    const iconMap: Record<string, JSX.Element> = {
        'sun-dim': <LuSunDim />,
        'activity': <LuActivity />
    };

    // Initialize the state with the default profile details
    useEffect(() => {
        const ledsConfigs = defaultProfile.ledsConfigs;
        if (ledsConfigs) {
            setLedsEffectStyle(ledsConfigs.ledsEffectStyle ?? LedsEffectStyle.STATIC);
            setColor1(parseColor(ledsConfigs.ledColors?.[0] ?? defaultFrontColor.toString('css')));
            setColor2(parseColor(ledsConfigs.ledColors?.[1] ?? defaultFrontColor.toString('css')));
            setColor3(parseColor(ledsConfigs.ledColors?.[2] ?? defaultFrontColor.toString('css')));
            setLedBrightness(ledsConfigs.ledBrightness ?? 75);
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
            }
        }

        console.log("saveProfileDetailHandler: ", newProfileDetails);
        return updateProfileDetails(defaultProfile.id, newProfileDetails as GameProfile);
    }

    const colorPickerDisabled = (index: number) => {
        return (index == 2 && !(LedsEffectStyleLabelMap.get(ledsEffectStyle)?.hasBackColor2 ?? false)) || !ledEnabled;
    }

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
                        hasLeds={true}
                        hasText={false}
                        colorEnabled={ledEnabled}
                        frontColor={GamePadColor.fromString(color1.toString('css'))}
                        backColor1={GamePadColor.fromString(color2.toString('css'))}
                        backColor2={GamePadColor.fromString(color3.toString('css'))}
                        effectStyle={ledsEffectStyle}
                        brightness={ledBrightness}
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
                                        <VStack gap={8} alignItems={"flex-start"} >
                                            {/* LED Effect Style */}
                                            
                                                <Switch colorPalette={"green"} checked={ledEnabled}
                                                    onChange={() => {
                                                        setLedEnabled(!ledEnabled);
                                                        setIsDirty?.(true);
                                                    }} >{t.SETTINGS_LEDS_ENABLE_LABEL}</Switch>
                                                {/* LED Effect Style */}
                                                <RadioCardRoot
                                                    colorPalette={ledEnabled ? "green" : "gray"}
                                                    size={"sm"}
                                                    variant={colorMode === "dark" ? "surface" : "outline"}
                                                    value={ledsEffectStyle?.toString() ?? LedsEffectStyle.STATIC.toString()}
                                                    onValueChange={(detail) => {
                                                        setLedsEffectStyle(detail.value as LedsEffectStyle);
                                                        setIsDirty?.(true);
                                                    }}
                                                    disabled={!ledEnabled}
                                                >
                                                    <RadioCardLabel>{t.SETTINGS_LEDS_EFFECT_STYLE_CHOICE}</RadioCardLabel>
                                                    <SimpleGrid gap={1} columns={5} >
                                                        {LedsEffectStyleList.map((style, index) => (
                                                            // <Tooltip key={index} content={effectStyleLabelMap.get(style)?.description ?? ""} >
                                                            <RadioCardItem
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
                                                <Field>
                                                    <Text fontSize={"sm"} opacity={!ledEnabled ? "0.25" : "0.85"} >{t.SETTINGS_LEDS_COLORS_LABEL}</Text>
                                                    {Array.from({ length: 3 }).map((_, index) => (
                                                        <ColorPickerRoot
                                                            key={index}
                                                            value={
                                                                index === 0 ? color1 :
                                                                    index === 1 ? color2 :
                                                                        index === 2 ? color3 :
                                                                            defaultFrontColor
                                                            }
                                                            maxW="200px"
                                                            disabled={colorPickerDisabled(index)}
                                                            onValueChange={(e) => {
                                                                setIsDirty?.(true);
                                                                const hex = e.value;
                                                                if (index === 0) setColor1(hex);
                                                                if (index === 1) setColor2(hex);
                                                                if (index === 2) setColor3(hex);
                                                            }}
                                                        >
                                                            <ColorPickerLabel opacity={colorPickerDisabled(index) ? "0.35" : "0.85"} >{colorLabels[index]}</ColorPickerLabel>
                                                            <ColorPickerControl >
                                                                <ColorPickerInput colorPalette={"green"} fontSize={"sm"} />
                                                                <ColorPickerTrigger />
                                                            </ColorPickerControl>
                                                            <ColorPickerContent>
                                                                <ColorPickerArea />
                                                                <HStack>
                                                                    <ColorPickerSliders />
                                                                </HStack>
                                                            </ColorPickerContent>
                                                        </ColorPickerRoot>
                                                    ))}
                                                </Field>

                                                {/* LED Brightness */}
                                                <Text fontSize={"sm"} opacity={!ledEnabled ? "0.35" : "0.85"} >
                                                    {t.SETTINGS_LEDS_BRIGHTNESS_LABEL}: {ledBrightness}
                                                </Text>
                                                <Slider
                                                    size={"md"}
                                                    colorPalette={"green"}
                                                    width={"300px"}
                                                    value={[ledBrightness]}
                                                    onValueChange={(e) => {
                                                        setLedBrightness(e.value[0]);
                                                        setIsDirty?.(true);
                                                    }}
                                                    disabled={!ledEnabled}
                                                    marks={[
                                                        { value: 0, label: "0" },
                                                        { value: 25, label: "25" },
                                                        { value: 50, label: "50" },
                                                        { value: 75, label: "75" },
                                                        { value: 100, label: "100" },
                                                    ]}
                                                />


                                           
                                        </VStack>
                                    </Fieldset.Content>
                                </Stack>
                            </Fieldset.Root>
                        </Card.Body>
                        <Card.Footer justifyContent={"flex-start"} >
                            <ContentActionButtons
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
