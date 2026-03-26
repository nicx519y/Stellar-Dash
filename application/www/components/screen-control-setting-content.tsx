'use client';

import React, { useEffect, useMemo, useState } from 'react';
import { Grid, Input, VStack } from '@chakra-ui/react';
import { Slider } from '@/components/ui/slider';
import { Field } from '@/components/ui/field';
import { Switch } from '@/components/ui/switch';
import { useGamepadConfig } from '@/contexts/gamepad-config-context';
import { ScreenControlConfig } from '@/types/gamepad-config';
import { MainContentBody, MainContentHeader, SettingMainContentLayout, SettingMainContentLayoutSize } from './setting-main-content-layout';
import { useLanguage } from '@/contexts/language-context';

function toHex6(v: number): string {
    const x = (v >>> 0) & 0xFFFFFF;
    return `#${x.toString(16).padStart(6, '0')}`;
}

function parseHex6(s: string, fallback: number): number {
    const t = s.trim();
    const m = t.startsWith('#') ? t.slice(1) : t;
    if (!/^[0-9a-fA-F]{6}$/.test(m)) return fallback;
    return parseInt(m, 16) >>> 0;
}

type ScreenControlSettingContentProps = {
    disabled?: boolean;
    width?: SettingMainContentLayoutSize;
    height?: SettingMainContentLayoutSize;
};

export function ScreenControlSettingContent(props: ScreenControlSettingContentProps) {
    const { disabled = false, width = 778, height = '100%' } = props;
    const { screenControl, updateScreenControl } = useGamepadConfig();
    const { t } = useLanguage();
    const [brightness, setBrightness] = useState<number>(screenControl.brightness ?? 100);
    const [bgHex, setBgHex] = useState<string>(toHex6(screenControl.backgroundColor ?? 0));
    const [textHex, setTextHex] = useState<string>(toHex6(screenControl.textColor ?? 0xFFFFFF));
    const [backgroundImageId, setBackgroundImageId] = useState<string>(screenControl.backgroundImageId ?? '');
    const [currentPageId, setCurrentPageId] = useState<string>(String(screenControl.currentPageId ?? 0));
    const [features, setFeatures] = useState(screenControl.features);

    useEffect(() => {
        setBrightness(screenControl.brightness ?? 100);
        setBgHex(toHex6(screenControl.backgroundColor ?? 0));
        setTextHex(toHex6(screenControl.textColor ?? 0xFFFFFF));
        setBackgroundImageId(screenControl.backgroundImageId ?? '');
        setCurrentPageId(String(screenControl.currentPageId ?? 0));
        setFeatures(screenControl.features);
    }, [screenControl]);

    const nextConfig: ScreenControlConfig = useMemo(() => {
        const b = Math.max(0, Math.min(100, brightness | 0));
        const bg = parseHex6(bgHex, screenControl.backgroundColor ?? 0);
        const fg = parseHex6(textHex, screenControl.textColor ?? 0xFFFFFF);
        const pid = Math.max(0, Math.min(65535, parseInt(currentPageId || '0', 10) || 0));
        return {
            brightness: b,
            backgroundColor: bg,
            textColor: fg,
            backgroundImageId,
            currentPageId: pid,
            features,
        };
    }, [brightness, bgHex, textHex, backgroundImageId, currentPageId, features, screenControl.backgroundColor, screenControl.textColor]);

    const push = async () => {
        await updateScreenControl(nextConfig, true);
    };

    return (
        <SettingMainContentLayout 
            width={width}
            height={height}
        >
            <MainContentHeader
                // title={t.SETTINGS_SCREEN_CONTROL_TITLE}
                description={t.SETTINGS_SCREEN_CONTROL_HELPER_TEXT}
            />
            <MainContentBody>
                <VStack align="stretch" gap={4}>
                    <Slider
                        size="sm"
                        min={0}
                        max={100}
                        step={1}
                        colorPalette="green"
                        disabled={disabled}
                        value={[brightness]}
                        label="Brightness"
                        showValue
                        onValueChange={(details: { value: number[] }) => setBrightness(details.value[0])}
                        onValueChangeEnd={() => void push()}
                    />

                    <Grid templateColumns="repeat(2, 1fr)" gap={4}>
                        <Field label="Background Color">
                            <Input
                                value={bgHex}
                                disabled={props.disabled}
                                onChange={(e: React.ChangeEvent<HTMLInputElement>) => setBgHex(e.target.value)}
                                onBlur={() => void push()}
                            />
                        </Field>
                        <Field label="Text Color">
                            <Input
                                value={textHex}
                                disabled={props.disabled}
                                onChange={(e: React.ChangeEvent<HTMLInputElement>) => setTextHex(e.target.value)}
                                onBlur={() => void push()}
                            />
                        </Field>
                        <Field label="Background Image Id">
                            <Input
                                value={backgroundImageId}
                                disabled={props.disabled}
                                onChange={(e: React.ChangeEvent<HTMLInputElement>) => setBackgroundImageId(e.target.value)}
                                onBlur={() => void push()}
                            />
                        </Field>
                        <Field label="Current Page Id">
                            <Input
                                value={currentPageId}
                                disabled={props.disabled}
                                onChange={(e: React.ChangeEvent<HTMLInputElement>) => setCurrentPageId(e.target.value)}
                                onBlur={() => void push()}
                            />
                        </Field>
                    </Grid>

                    <Grid templateColumns="repeat(2, 1fr)" gap={3}>
                        <Switch checked={features.inputModeSwitch} disabled={props.disabled} onCheckedChange={(e: { checked: boolean }) => { setFeatures({ ...features, inputModeSwitch: e.checked }); void updateScreenControl({ ...nextConfig, features: { ...features, inputModeSwitch: e.checked } }, true); }}>
                            InputMode Switch
                        </Switch>
                        <Switch checked={features.profilesSwitch} disabled={props.disabled} onCheckedChange={(e: { checked: boolean }) => { setFeatures({ ...features, profilesSwitch: e.checked }); void updateScreenControl({ ...nextConfig, features: { ...features, profilesSwitch: e.checked } }, true); }}>
                            Profiles Switch
                        </Switch>
                        <Switch checked={features.socdModeSwitch} disabled={props.disabled} onCheckedChange={(e: { checked: boolean }) => { setFeatures({ ...features, socdModeSwitch: e.checked }); void updateScreenControl({ ...nextConfig, features: { ...features, socdModeSwitch: e.checked } }, true); }}>
                            SOCD Switch
                        </Switch>
                        <Switch checked={features.tournamentModeSwitch} disabled={props.disabled} onCheckedChange={(e: { checked: boolean }) => { setFeatures({ ...features, tournamentModeSwitch: e.checked }); void updateScreenControl({ ...nextConfig, features: { ...features, tournamentModeSwitch: e.checked } }, true); }}>
                            Mode Switch
                        </Switch>
                        <Switch checked={features.ledBrightnessAdjust} disabled={props.disabled} onCheckedChange={(e: { checked: boolean }) => { setFeatures({ ...features, ledBrightnessAdjust: e.checked }); void updateScreenControl({ ...nextConfig, features: { ...features, ledBrightnessAdjust: e.checked } }, true); }}>
                            LED Brightness
                        </Switch>
                        <Switch checked={features.ledEffectSwitch} disabled={props.disabled} onCheckedChange={(e: { checked: boolean }) => { setFeatures({ ...features, ledEffectSwitch: e.checked }); void updateScreenControl({ ...nextConfig, features: { ...features, ledEffectSwitch: e.checked } }, true); }}>
                            LED Effect
                        </Switch>
                        <Switch checked={features.ambientBrightnessAdjust} disabled={props.disabled} onCheckedChange={(e: { checked: boolean }) => { setFeatures({ ...features, ambientBrightnessAdjust: e.checked }); void updateScreenControl({ ...nextConfig, features: { ...features, ambientBrightnessAdjust: e.checked } }, true); }}>
                            Ambient Brightness
                        </Switch>
                        <Switch checked={features.ambientEffectSwitch} disabled={props.disabled} onCheckedChange={(e: { checked: boolean }) => { setFeatures({ ...features, ambientEffectSwitch: e.checked }); void updateScreenControl({ ...nextConfig, features: { ...features, ambientEffectSwitch: e.checked } }, true); }}>
                            Ambient Effect
                        </Switch>
                        <Switch checked={features.screenBrightnessAdjust} disabled={props.disabled} onCheckedChange={(e: { checked: boolean }) => { setFeatures({ ...features, screenBrightnessAdjust: e.checked }); void updateScreenControl({ ...nextConfig, features: { ...features, screenBrightnessAdjust: e.checked } }, true); }}>
                            Screen Brightness
                        </Switch>
                        <Switch checked={features.webConfigEntry} disabled={props.disabled} onCheckedChange={(e: { checked: boolean }) => { setFeatures({ ...features, webConfigEntry: e.checked }); void updateScreenControl({ ...nextConfig, features: { ...features, webConfigEntry: e.checked } }, true); }}>
                            WebConfig Entry
                        </Switch>
                        <Switch checked={features.calibrationModeSwitch} disabled={props.disabled} onCheckedChange={(e: { checked: boolean }) => { setFeatures({ ...features, calibrationModeSwitch: e.checked }); void updateScreenControl({ ...nextConfig, features: { ...features, calibrationModeSwitch: e.checked } }, true); }}>
                            Calibration Mode
                        </Switch>
                    </Grid>
                </VStack>
            </MainContentBody>
        </SettingMainContentLayout>
    );
}
