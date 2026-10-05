'use client';

import { normalizeScreenControl, SCREEN_FEATURE_IDS } from '@/lib/screen-control-config';
import React, { useEffect, useMemo, useState } from 'react';
import { VStack, HStack, Table, Box, Text, RadioCard, RadioGroup } from '@chakra-ui/react';
import { Slider } from '@/components/ui/slider';
import { Switch } from '@/components/ui/switch';
import { useGamepadConfig } from '@/contexts/gamepad-config-context';
import { DEFAULT_SCREEN_CONTROL_CONFIG, SCREEN_STANDBY_TIMEOUT_OPTIONS, ScreenControlConfig, ScreenControlFeatureKey, ScreenControlFeatures, ScreenStyle, StandbyDisplay, normalizeScreenStandbyTimeout, withRequiredWebConfigEntry } from '@/types/gamepad-config';
import { useLanguage } from '@/contexts/language-context';
import { LuGripVertical, LuMoon, LuSun } from "react-icons/lu";
import { TitleLabel } from './ui/title-label';
import { SettingDescription } from './ui/setting-description';
import { showToast } from './ui/toaster';
import { BackgroundImageGallery } from './background-image-gallery';
import { ScreenStandbyPreview } from './screen-standby-preview';

const USER_BG_ID = 'USER_IMAGE';
const STANDBY_TIMEOUT_LABELS = ['10s', '30s', '1min', '2min', '5min'] as const;


type ScreenControlSettingContentProps = {
    disabled?: boolean;
};

const normalizeFeaturesOrder = (order: ScreenControlFeatureKey[] | undefined): ScreenControlFeatureKey[] => {
    const fallback = DEFAULT_SCREEN_CONTROL_CONFIG.featuresOrder;
    if (!order || !Array.isArray(order)) return fallback;
    const seen = new Set<ScreenControlFeatureKey>();
    const next: ScreenControlFeatureKey[] = [];
    for (const k of order) {
        if (!fallback.includes(k)) continue;
        if (seen.has(k)) continue;
        seen.add(k);
        next.push(k);
    }
    for (const k of fallback) {
        if (!seen.has(k)) next.push(k);
    }
    return next;
};

export function ScreenControlSettingContent(props: ScreenControlSettingContentProps) {
    const externallyDisabled = props.disabled ?? false;
    const {
        screenControl,
        stageDeferredScreenControl,
        previewScreenBrightness,
    } = useGamepadConfig();
    const disabled = externallyDisabled || screenControl.standbySupported === false;
    const [standbyEnabled, setStandbyEnabled] = useState(screenControl.standbyEnabled);
    const [brightness, setBrightness] = useState<number>(screenControl.brightness ?? 100);
    const [standbyDisplay, setStandbyDisplay] = useState<StandbyDisplay>(screenControl.standbyDisplay ?? 'screenOff');
    const [standbyTimeoutSeconds, setStandbyTimeoutSeconds] = useState<number>(normalizeScreenStandbyTimeout(screenControl.standbyTimeoutSeconds));
    const standbyTimeoutDraftRef = React.useRef(standbyTimeoutSeconds);
    standbyTimeoutDraftRef.current = standbyTimeoutSeconds;
    const [screenStyle, setScreenStyle] = useState<ScreenStyle>(screenControl.screenStyle ?? 'dark');
    const [backgroundImageId, setBackgroundImageId] = useState<string>(screenControl.backgroundImageId ?? '');
    const [currentPageId, setCurrentPageId] = useState<string>(String(screenControl.currentPageId ?? 0));
    const [features, setFeatures] = useState(
        withRequiredWebConfigEntry(screenControl.features),
    );
    const [featuresOrder, setFeaturesOrder] = useState<ScreenControlFeatureKey[]>(
        screenControl.featuresOrder ?? DEFAULT_SCREEN_CONTROL_CONFIG.featuresOrder
    );
    const [dropIndicator, setDropIndicator] = useState<{ key: ScreenControlFeatureKey; position: 'before' | 'after' } | null>(null);
    const { t } = useLanguage();
    const [deviceImageAvailable, setDeviceImageAvailable] = useState(false);
    const [galleryDeviceBusy, setGalleryDeviceBusy] = useState(false);
    const imageOperationBusy = galleryDeviceBusy;

    // Value snapshots keep equivalent device reads from resetting an in-flight draft.
    const featuresSnapshot = JSON.stringify(screenControl.features ?? DEFAULT_SCREEN_CONTROL_CONFIG.features);
    const featuresOrderSnapshot = JSON.stringify(screenControl.featuresOrder ?? DEFAULT_SCREEN_CONTROL_CONFIG.featuresOrder);

    useEffect(() => {
        setStandbyEnabled(screenControl.standbyEnabled);
        setBrightness(screenControl.brightness ?? 100);
        setStandbyDisplay(screenControl.standbyDisplay ?? 'screenOff');
        setStandbyTimeoutSeconds(normalizeScreenStandbyTimeout(screenControl.standbyTimeoutSeconds));
        setScreenStyle(screenControl.screenStyle ?? 'dark');
        setBackgroundImageId(screenControl.backgroundImageId ?? '');
        setCurrentPageId(String(screenControl.currentPageId ?? 0));
        setFeatures(withRequiredWebConfigEntry(JSON.parse(featuresSnapshot) as ScreenControlFeatures));
        setFeaturesOrder(normalizeFeaturesOrder(JSON.parse(featuresOrderSnapshot) as ScreenControlFeatureKey[]));
    // Live reads may return a fresh object while the slider is being dragged.
    // Only replace the local draft when actual configuration values change.
    }, [screenControl.brightness, screenControl.standbyEnabled, screenControl.standbyDisplay,
        screenControl.standbyTimeoutSeconds, screenControl.screenStyle, screenControl.backgroundImageId,
        screenControl.currentPageId, featuresSnapshot, featuresOrderSnapshot]);

    const nextConfig: ScreenControlConfig = useMemo(() => {
        const b = Math.max(0, Math.min(100, brightness | 0));
        const pid = Math.max(0, Math.min(65535, parseInt(currentPageId || '0', 10) || 0));
        return {
            brightness: b,
            standbyDisplay,
            standbyEnabled,
            standbySupported: screenControl.standbySupported,
            standbyTimeoutSeconds,
            screenStyle,
            backgroundImageId,
            currentPageId: pid,
            features,
            featuresOrder,
        };
    }, [brightness, standbyEnabled, screenControl.standbySupported, standbyDisplay, standbyTimeoutSeconds, screenStyle, backgroundImageId, currentPageId, features, featuresOrder]);

    const commitUiChange = async (next: ScreenControlConfig) => {
        try {
            if (disabled) return;
            stageDeferredScreenControl(normalizeScreenControl(next));
        } catch {
            // The global sync status retains failed drafts and offers retry.
        }
    };

    const handleGalleryInstalled = React.useCallback(() => {
        setBackgroundImageId(USER_BG_ID);
        setStandbyDisplay('backgroundImage');
        setDeviceImageAvailable(true);
    }, []);

    const handleGalleryAvailabilityChange = React.useCallback((available: boolean) => {
        setDeviceImageAvailable(available);
        if (!available) {
            if (standbyDisplay === 'backgroundImage') setStandbyEnabled(false);
            setStandbyDisplay(current => current === 'backgroundImage' ? 'screenOff' : current);
            setBackgroundImageId('');
        }
    }, [standbyDisplay]);

    const dragFeatureKeyRef = React.useRef<ScreenControlFeatureKey | null>(null);

    const featureLabelMap: Record<ScreenControlFeatureKey, string> = {
        inputModeSwitch: t.SETTINGS_SCREEN_CONTROL_FEATURE_INPUT_MODE_SWITCH,
        profilesSwitch: t.SETTINGS_SCREEN_CONTROL_FEATURE_PROFILES_SWITCH,
        socdModeSwitch: t.SETTINGS_SCREEN_CONTROL_FEATURE_SOCD_MODE_SWITCH,
        connectionModeSwitch: t.SETTINGS_SCREEN_CONTROL_FEATURE_CONNECTION_MODE_SWITCH,
        buttonsPerformanceQuickSet: t.SETTINGS_SCREEN_CONTROL_FEATURE_BUTTONS_PERFORMANCE_QUICK_SET,
        ledSetting: t.SETTINGS_SCREEN_CONTROL_FEATURE_LED_SETTING,
        power: t.SETTINGS_SCREEN_CONTROL_FEATURE_POWER,
        screenBrightnessAdjust: t.SETTINGS_SCREEN_CONTROL_FEATURE_SCREEN_BRIGHTNESS_ADJUST,
        webConfigEntry: t.SETTINGS_SCREEN_CONTROL_FEATURE_WEB_CONFIG_ENTRY,
        calibrationModeSwitch: t.SETTINGS_SCREEN_CONTROL_FEATURE_CALIBRATION_MODE_SWITCH,
    };

    const orderedFeatureItems = featuresOrder.map((key) => ({ key, label: featureLabelMap[key] }));

    const featureKeyToId = SCREEN_FEATURE_IDS;
    const idToFeatureKey = (id: number): ScreenControlFeatureKey | null => {
        const entries = Object.entries(featureKeyToId) as [ScreenControlFeatureKey, number][];
        for (const [k, v] of entries) if (v === id) return k;
        return null;
    };
    const [firstFeatureKey, setFirstFeatureKey] = useState<ScreenControlFeatureKey>(() => {
        const k = idToFeatureKey(screenControl.currentPageId);
        return k && featuresOrder.includes(k) ? k : featuresOrder[0];
    });
    useEffect(() => {
        const k = idToFeatureKey(screenControl.currentPageId);
        setFirstFeatureKey(k && featuresOrder.includes(k) ? k : featuresOrder[0]);
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [screenControl.currentPageId, featuresOrder.join('|')]);

    return (

        <>
            {/* <MainContentHeader
                description={t.SETTINGS_SCREEN_CONTROL_HELPER_TEXT}
            /> */}

            <SettingDescription
                text={t.SETTINGS_SCREEN_CONTROL_HELPER_TEXT}
                fontSize="14px"
                mb="30px"
            />
            
            
            
            {screenControl.standbySupported === false && <Text role="alert" color="fg.warning">{t.SETTINGS_SCREEN_CONTROL_UPDATE_REQUIRED}</Text>}
            <VStack align="stretch" gap={4} >

                <TitleLabel title={t.SETTINGS_SCREEN_CONTROL_BASIC} />

                <Switch
                    alignSelf="start"
                    size="lg"
                    colorPalette="green"
                    checked={screenStyle === 'light'}
                    disabled={disabled}
                    title={screenStyle === 'light' ? t.SETTINGS_SCREEN_CONTROL_LIGHT_MODE : t.SETTINGS_SCREEN_CONTROL_DARK_MODE}
                    thumbLabel={{
                        on: <LuSun size={14} color="#b45309" aria-hidden="true" />,
                        off: <LuMoon size={14} color="#334155" aria-hidden="true" />,
                    }}
                    trackLabel={{
                        on: <LuMoon size={14} color="#cbd5e1" aria-hidden="true" />,
                        off: <LuSun size={14} color="#fbbf24" aria-hidden="true" />,
                    }}
                    onCheckedChange={(event: { checked: boolean }) => {
                        const style: ScreenStyle = event.checked ? 'light' : 'dark';
                        setScreenStyle(style);
                        void commitUiChange({ ...nextConfig, screenStyle: style });
                    }}
                >
                    {t.SETTINGS_SCREEN_CONTROL_STYLE_LABEL}
                </Switch>

                <Slider
                    size="sm"
                    width="100%"
                    maxW="100%"
                    min={0}
                    max={100}
                    step={10}
                    colorPalette="green"
                    disabled={disabled}
                    value={[brightness]}
                    label={t.SETTINGS_SCREEN_CONTROL_BRIGHTNESS_LABEL}
                    showValue
                    onValueChange={(details: { value: number[] }) => {
                        const value = details.value[0];
                        setBrightness(value);
                        void commitUiChange({ ...nextConfig, brightness: value });
                        void previewScreenBrightness(value).catch(() => undefined);
                    }}
                    onValueChangeEnd={async (details: { value: number[] }) => {
                        const value = details.value[0];
                        setBrightness(value);
                        await commitUiChange({ ...nextConfig, brightness: value });
                    }}
                />

                <TitleLabel title={t.SETTINGS_SCREEN_CONTROL_STANDBY_DISPLAY_LABEL} />
                <VStack align="start" gap={3}>
                    <VStack width="100%" align="start" gap={4} mb={4}>
                        <Switch checked={standbyEnabled} disabled={disabled}
                            onCheckedChange={(event: { checked: boolean }) => {
                                setStandbyEnabled(event.checked);
                                void commitUiChange({ ...nextConfig, standbyEnabled: event.checked });
                            }}>
                            {t.SETTINGS_SCREEN_CONTROL_STANDBY_ENABLED}
                        </Switch>
                    <Slider
                        size="sm"
                        width="100%"
                        maxW="100%"
                        minW={0}
                        px={{ base: 3, md: 0 }}
                        mt={0}
                        min={0}
                        max={SCREEN_STANDBY_TIMEOUT_OPTIONS.length - 1}
                        step={1}
                        colorPalette="green"
                        disabled={disabled || !standbyEnabled}
                        value={[Math.max(0, SCREEN_STANDBY_TIMEOUT_OPTIONS.indexOf(standbyTimeoutSeconds as typeof SCREEN_STANDBY_TIMEOUT_OPTIONS[number]))]}
                        label={t.SETTINGS_SCREEN_CONTROL_STANDBY_TIMEOUT_LABEL}
                        marks={STANDBY_TIMEOUT_LABELS.map((label, value) => ({ value, label }))}
                        onValueChange={(details: { value: number[] }) => {
                            const seconds = SCREEN_STANDBY_TIMEOUT_OPTIONS[details.value[0]];
                            if (seconds !== undefined) {
                                standbyTimeoutDraftRef.current = seconds;
                                setStandbyTimeoutSeconds(seconds);
                            }
                        }}
                        onValueChangeEnd={() => {
                            // Controlled Chakra sliders can end a keyboard step before
                            // React publishes its new value. Commit the latest draft.
                            const seconds = standbyTimeoutDraftRef.current;
                            void commitUiChange({ ...nextConfig, standbyTimeoutSeconds: seconds });
                        }}
                    />
                    </VStack>
                    <RadioCard.Root
                        size={"sm"}
                        width="100%"
                        value={standbyDisplay}
                        variant={"subtle"}
                        onValueChange={async (d) => {
                            const v = (d as { value: 'screenOff'|'backgroundImage'|'buttonLayout' }).value;
                            const update: Partial<ScreenControlConfig> = { standbyDisplay: v };
                            if (v === 'backgroundImage') {
                                if (!deviceImageAvailable) {
                                    showToast({ title: t.SETTINGS_SCREEN_CONTROL_BACKGROUND_IMAGE_INVALID_SELECTION, type: 'error' });
                                    return;
                                }
                                setBackgroundImageId(USER_BG_ID);
                                (update as Partial<ScreenControlConfig>).backgroundImageId = USER_BG_ID;
                            }
                            setStandbyDisplay(v);
                            await commitUiChange({ ...nextConfig, ...update });
                        }}
                    >
                        <HStack width="100%">
                            {[
                                { value: 'screenOff', label: t.SETTINGS_SCREEN_CONTROL_STANDBY_NONE },
                                { value: 'backgroundImage', label: t.SETTINGS_SCREEN_CONTROL_STANDBY_BACKGROUND_IMAGE },
                                { value: 'buttonLayout', label: t.SETTINGS_SCREEN_CONTROL_STANDBY_BUTTON_LAYOUT },
                            ].map(opt => (
                                <RadioCard.Item flex={1} minW={0} key={opt.value} value={opt.value as 'screenOff'|'backgroundImage'|'buttonLayout'} disabled={disabled || imageOperationBusy}>
                                    <RadioCard.ItemHiddenInput />
                                    <RadioCard.ItemControl>
                                        <RadioCard.ItemText>{opt.label}</RadioCard.ItemText>
                                    </RadioCard.ItemControl>
                                </RadioCard.Item>
                            ))}
                        </HStack>
                    </RadioCard.Root>
                    <HStack align="start" width="100%">
                        <VStack flex={1} minW={0}>
                            <ScreenStandbyPreview mode="screenOff" selected={standbyDisplay === 'screenOff'} screenStyle={screenStyle} />
                        </VStack>
                        <VStack flex={1} minW={0}>
                            <BackgroundImageGallery
                                disabled={disabled}
                                config={nextConfig}
                                onInstalled={handleGalleryInstalled}
                                onAvailabilityChange={handleGalleryAvailabilityChange}
                                onBusyChange={setGalleryDeviceBusy}
                            />
                        </VStack>
                        <VStack flex={1} minW={0}>
                            <ScreenStandbyPreview mode="buttonLayout" selected={standbyDisplay === 'buttonLayout'} screenStyle={screenStyle} />
                        </VStack>
                    </HStack>

                </VStack>

                <TitleLabel title={t.SETTINGS_SCREEN_CONTROL_FEATURES} mt="20px" />

                <RadioGroup.Root
                    variant = "subtle"
                    value={firstFeatureKey}
                    onValueChange={async (d) => {
                        const v = (d as { value: string }).value as ScreenControlFeatureKey;
                        if (!v || v === firstFeatureKey) return;
                        setFirstFeatureKey(v);
                        const pageId = featureKeyToId[v];
                        await commitUiChange({ ...nextConfig, currentPageId: pageId });
                    }}
                >
                <Table.Root size="sm" colorPalette="green" interactive>
                    <Table.Body>
                        {orderedFeatureItems.map((item) => (
                            <Table.Row
                                key={item.key}
                                height="40px"
                                draggable={!disabled}
                                bg={item.key === firstFeatureKey ? "bg.success" : undefined}
                                borderTopWidth={dropIndicator?.key === item.key && dropIndicator.position === 'before' ? "2px" : undefined}
                                borderTopColor={dropIndicator?.key === item.key && dropIndicator.position === 'before' ? "green.400" : undefined}
                                borderBottomWidth={dropIndicator?.key === item.key && dropIndicator.position === 'after' ? "2px" : undefined}
                                borderBottomColor={dropIndicator?.key === item.key && dropIndicator.position === 'after' ? "green.400" : undefined}
                                onDragStart={(e) => {
                                    if (disabled) return;
                                    dragFeatureKeyRef.current = item.key;
                                    e.dataTransfer.effectAllowed = 'move';
                                    e.dataTransfer.setData('text/plain', item.key);
                                }}
                                onDragOver={(e) => {
                                    if (disabled) return;
                                    e.preventDefault();
                                    e.dataTransfer.dropEffect = 'move';

                                    const rect = (e.currentTarget as HTMLElement).getBoundingClientRect();
                                    const mid = rect.top + rect.height / 2;
                                    const deadZonePx = 6;

                                    setDropIndicator((cur) => {
                                        if (cur?.key === item.key && Math.abs(e.clientY - mid) <= deadZonePx) {
                                            return cur;
                                        }
                                        const position: 'before' | 'after' = e.clientY < mid ? 'before' : 'after';
                                        if (cur?.key === item.key && cur.position === position) return cur;
                                        return { key: item.key, position };
                                    });
                                }}
                                onDrop={async (e) => {
                                    if (disabled) return;
                                    e.preventDefault();

                                    const rect = (e.currentTarget as HTMLElement).getBoundingClientRect();
                                    const mid = rect.top + rect.height / 2;
                                    const position: 'before' | 'after' = e.clientY < mid ? 'before' : 'after';

                                    setDropIndicator(null);
                                    const from = dragFeatureKeyRef.current;
                                    const to = item.key;
                                    dragFeatureKeyRef.current = null;
                                    if (!from || from === to) return;

                                    const nextOrder = [...featuresOrder];
                                    const fromIdx = nextOrder.indexOf(from);
                                    const toIdx = nextOrder.indexOf(to);
                                    if (fromIdx < 0 || toIdx < 0) return;

                                    const [moved] = nextOrder.splice(fromIdx, 1);
                                    let insertIndex = position === 'before' ? toIdx : toIdx + 1;
                                    if (fromIdx < insertIndex) insertIndex -= 1;
                                    nextOrder.splice(insertIndex, 0, moved);

                                    setFeaturesOrder(nextOrder);
                                    await commitUiChange({ ...nextConfig, featuresOrder: nextOrder });
                                }}
                                onDragEnd={() => {
                                    dragFeatureKeyRef.current = null;
                                    setDropIndicator(null);
                                }}
                            >
                                <Table.Cell py={1} fontSize="11px">
                                    <HStack gap={2}>
                                        <Box color="gray.500" cursor={disabled ? "default" : "grab"}>
                                            <LuGripVertical />
                                        </Box>
                                        <Text>{item.label}</Text>
                                    </HStack>
                                </Table.Cell>
                                <Table.Cell py={1} fontSize="11px">
                                    <HStack gap={2}>
                                        <RadioGroup.Item value={item.key} disabled={disabled || !features[item.key]} >
                                            <RadioGroup.ItemHiddenInput />
                                            <RadioGroup.ItemIndicator />
                                            {firstFeatureKey === item.key && <RadioGroup.ItemText fontSize="11px" color="gray.400" >{t.SETTINGS_SCREEN_CONTROL_FIRST_SCREEN_LABEL}</RadioGroup.ItemText>}
                                        </RadioGroup.Item>
                                    </HStack>
                                </Table.Cell>
                                <Table.Cell py={1} fontSize="11px" textAlign="end">
                                    <Switch
                                        checked={item.key === 'webConfigEntry' || features[item.key]}
                                        disabled={disabled || item.key === 'webConfigEntry'}
                                        onCheckedChange={async (e: { checked: boolean }) => {
                                            if (item.key === 'webConfigEntry') return;
                                            const nf = { ...features, [item.key]: e.checked } as ScreenControlFeatures;
                                            setFeatures(nf);
                                            await commitUiChange({ ...nextConfig, features: nf });
                                        }}
                                    />
                                </Table.Cell>
                            </Table.Row>
                        ))}
                    </Table.Body>
                </Table.Root>
                </RadioGroup.Root>
            </VStack>
        </>
    );
}
