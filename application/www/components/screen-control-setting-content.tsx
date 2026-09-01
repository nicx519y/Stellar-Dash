'use client';

import React, { useEffect, useMemo, useState } from 'react';
import { Grid, VStack, HStack, Table, Image, Box, Flex, Text, Input, Spinner, RadioCard, RadioGroup } from '@chakra-ui/react';
import { Slider } from '@/components/ui/slider';
import { Switch } from '@/components/ui/switch';
import { useGamepadConfig } from '@/contexts/gamepad-config-context';
import { DEFAULT_SCREEN_CONTROL_CONFIG, ScreenControlConfig, ScreenControlFeatureKey, ScreenControlFeatures, ScreenStyle, StandbyDisplay, withRequiredWebConfigEntry } from '@/types/gamepad-config';
import { useLanguage } from '@/contexts/language-context';
import { LuCheck, LuUpload, LuGripVertical } from "react-icons/lu";
import { TitleLabel } from './ui/title-label';
import { SettingDescription } from './ui/setting-description';
import { processGifToRGB565Sequence, processImageToRGB565, rgb565ToPngDataUrl } from '@/lib/screen-control-image';


type ScreenControlSettingContentProps = {
    disabled?: boolean;
};

export function ScreenControlSettingContent(props: ScreenControlSettingContentProps) {
    const { disabled = false } = props;
    const {
        screenControl,
        stageDeferredScreenControl,
        getDeviceImageCatalog,
        readDeviceImage,
        uploadDeviceImage,
        deleteDeviceImage,
        deviceConnected,
    } = useGamepadConfig();
    const [brightness, setBrightness] = useState<number>(screenControl.brightness ?? 100);
    const [standbyDisplay, setStandbyDisplay] = useState<StandbyDisplay>(screenControl.standbyDisplay ?? 'none');
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
    const SYSTEM_BG_ID = 'SYSTEM_DEFAULT';
    const USER_BG_ID = 'USER_IMAGE';
    const [userAsset, setUserAsset] = useState<{ id: string; name: string; width: number; height: number; previewUrl: string; frames?: string[]; fps?: number; frameCount?: number } | null>(null);
    const [isUploadingUserImage, setIsUploadingUserImage] = useState(false);
    const [isDownloadingBgImages, setIsDownloadingBgImages] = useState(false);
    const [userPreviewFrameIndex, setUserPreviewFrameIndex] = useState(0);
    const [gifTargetFps, _setGifTargetFps] = useState(3);
    const [gifMaxFrames, _setGifMaxFrames] = useState(10);
    const fileInputRef = React.useRef<HTMLInputElement>(null);
    const [systemAsset, setSystemAsset] = useState<{ id: string; width: number; height: number; previewUrl: string } | null>(null);
    const placeholderSystemPreviewUrl = useMemo(() => {
        if (typeof document === 'undefined') return '';
        const canvas = document.createElement('canvas');
        canvas.width = 320;
        canvas.height = 172;
        const ctx = canvas.getContext('2d')!;
        ctx.fillStyle = '#222222';
        ctx.fillRect(0, 0, canvas.width, canvas.height);
        return canvas.toDataURL('image/png');
    }, []);
    const systemPreviewUrl = systemAsset?.previewUrl ?? placeholderSystemPreviewUrl;
    const BG_IMAGES_CACHE_KEY = 'screen_control_bg_images_cache_v1';
    const bgImagesLoadedRef = React.useRef(false);

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

    useEffect(() => {
        setBrightness(screenControl.brightness ?? 100);
        setStandbyDisplay(screenControl.standbyDisplay ?? 'none');
        setScreenStyle(screenControl.screenStyle ?? 'dark');
        setBackgroundImageId(screenControl.backgroundImageId ?? '');
        setCurrentPageId(String(screenControl.currentPageId ?? 0));
        setFeatures(withRequiredWebConfigEntry(screenControl.features));
        setFeaturesOrder(normalizeFeaturesOrder(screenControl.featuresOrder));
    }, [screenControl]);

    type BgImagesCache = {
        system?: { id: string; width: number; height: number; previewUrl: string } | null;
        user?: { id: string; width: number; height: number; previewUrl: string; frames?: string[]; fps?: number; frameCount?: number } | null;
    };

    const loadBgImagesCache = (): BgImagesCache | null => {
        try {
            const raw = sessionStorage.getItem(BG_IMAGES_CACHE_KEY);
            if (!raw) return null;
            return JSON.parse(raw) as BgImagesCache;
        } catch {
            return null;
        }
    };

    const saveBgImagesCache = (cache: BgImagesCache) => {
        try {
            sessionStorage.setItem(BG_IMAGES_CACHE_KEY, JSON.stringify(cache));
        } catch {
        }
    };

    const userFrames = userAsset?.frames;
    useEffect(() => {
        setUserPreviewFrameIndex(0);
        if (!userFrames || userFrames.length <= 1) return;
        const fps = Math.max(1, Math.min(5, Math.floor(gifTargetFps)));
        const intervalMs = Math.floor(1000 / fps);
        const id = window.setInterval(() => {
            setUserPreviewFrameIndex((i) => (i + 1) % userFrames.length);
        }, intervalMs);
        return () => {
            window.clearInterval(id);
        };
    }, [gifTargetFps, userFrames]);

    const userSlotPreviewUrl = useMemo(() => {
        if (!userAsset) return undefined;
        if (userAsset.frames && userAsset.frames.length > 0) {
            return userAsset.frames[userPreviewFrameIndex % userAsset.frames.length];
        }
        return userAsset.previewUrl;
    }, [userAsset, userPreviewFrameIndex]);

    useEffect(() => {
        const handleBeforeUnload = () => {
            try {
                sessionStorage.removeItem(BG_IMAGES_CACHE_KEY);
            } catch {
            }
        };
        window.addEventListener('beforeunload', handleBeforeUnload);
        return () => {
            window.removeEventListener('beforeunload', handleBeforeUnload);
        };
    }, []);

    const nextConfig: ScreenControlConfig = useMemo(() => {
        const b = Math.max(0, Math.min(100, brightness | 0));
        const pid = Math.max(0, Math.min(65535, parseInt(currentPageId || '0', 10) || 0));
        return {
            brightness: b,
            standbyDisplay,
            screenStyle,
            backgroundImageId,
            currentPageId: pid,
            features,
            featuresOrder,
        };
    }, [brightness, standbyDisplay, screenStyle, backgroundImageId, currentPageId, features, featuresOrder]);

    const commitUiChange = async (next: ScreenControlConfig) => {
        try {
            stageDeferredScreenControl(next);
        } catch {
            // The context restores the last device-confirmed value and exposes
            // the failure through the shared error toast.
        }
    };

    const push = async () => {
        await commitUiChange(nextConfig);
    };

    const fetchBgImagesFromDeviceOnce = async () => {
        if (bgImagesLoadedRef.current) return;
        if (!deviceConnected) return;

        const cached = loadBgImagesCache();
        if (cached) {
            if (cached.system) setSystemAsset(cached.system);
            if (cached.user) setUserAsset({ ...cached.user, name: cached.user.id });
            bgImagesLoadedRef.current = true;
            return;
        }

        try {
        setIsDownloadingBgImages(true);
        const info = await getDeviceImageCatalog();

        const cache: BgImagesCache = { system: null, user: null };

        const buildPreviews = (bytes: Uint8Array, width: number, height: number, format: number, frameCount: number, fps: number) => {
            const frameSize = width * height * 2;
            if (format === 2 && frameCount > 1 && bytes.length >= frameSize * frameCount) {
                const frames: string[] = [];
                for (let i = 0; i < frameCount; i++) {
                    const slice = bytes.subarray(i * frameSize, (i + 1) * frameSize);
                    frames.push(rgb565ToPngDataUrl(slice, width, height));
                }
                return { previewUrl: frames[0], frames, fps: Math.max(1, Math.min(5, fps || 5)), frameCount };
            }
            return { previewUrl: rgb565ToPngDataUrl(bytes, width, height), frames: undefined, fps: undefined, frameCount: 1 };
        };

        if (info.system.valid && info.system.width > 0 && info.system.height > 0 && info.system.size > 0) {
            const bytes = await readDeviceImage('system', info.system.size);
            const previews = buildPreviews(bytes, info.system.width, info.system.height, info.system.format, info.system.frameCount || 1, info.system.fps || 0);
            const sys = { id: SYSTEM_BG_ID, width: info.system.width, height: info.system.height, previewUrl: previews.previewUrl };
            setSystemAsset(sys);
            cache.system = sys;
        }

        if (info.user.valid && info.user.width > 0 && info.user.height > 0 && info.user.size > 0) {
            const bytes = await readDeviceImage('user', info.user.size);
            const previews = buildPreviews(bytes, info.user.width, info.user.height, info.user.format, info.user.frameCount || 1, info.user.fps || 0);
            const usr = { id: USER_BG_ID, width: info.user.width, height: info.user.height, previewUrl: previews.previewUrl, frames: previews.frames, fps: previews.fps, frameCount: previews.frameCount };
            setUserAsset({ ...usr, name: usr.id });
            cache.user = usr;
        }

        saveBgImagesCache(cache);
        bgImagesLoadedRef.current = true;
        } catch {
            bgImagesLoadedRef.current = false;
        } finally {
            setIsDownloadingBgImages(false);
        }
    };

    const uploadUserBackgroundImage = async (name: string, width: number, height: number, data: Uint8Array, opts: { frameCount: number; fps: number }) => {
        const frameCount = Math.max(1, Math.min(10, opts.frameCount | 0));
        const fps = Math.max(0, Math.min(5, opts.fps | 0));
        await uploadDeviceImage({ width, height, data, frameCount, fps });

        return { id: USER_BG_ID, name, width, height, frameCount, fps };
    };

    const deleteUserBackgroundImage = async () => {
        await deleteDeviceImage();
    };

    const ActionLink = (props: { label: string; onClick: () => void; hidden?: boolean }) => {
        if (props.hidden) return null;
        return (
            <Text
                as="button"
                fontSize="xs"
                color={disabled ? "gray.400" : "gray.400"}
                whiteSpace="nowrap"
                _hover={disabled ? undefined : { color: "green.400", textDecoration: "underline" }}
                cursor={disabled ? "not-allowed" : "pointer"}
                onClick={() => {
                    if (disabled) return;
                    props.onClick();
                }}
            >
                {props.label}
            </Text>
        );
    };

    const handleUploadClick = () => {
        if (disabled) return;
        fileInputRef.current?.click();
    };

    const handleFileChange = async (e: React.ChangeEvent<HTMLInputElement>) => {
        if (disabled) return;
        const file = e.target.files?.[0];
        if (!file) return;
        setIsUploadingUserImage(true);
        try {
            const isGif = file.type === 'image/gif' || file.name.toLowerCase().endsWith('.gif');
            {
                const current = loadBgImagesCache() ?? { system: systemAsset, user: null };
                if (isGif) {
                    const processed = await processGifToRGB565Sequence(file, gifTargetFps, gifMaxFrames);
                    const uploaded = await uploadUserBackgroundImage(file.name, processed.width, processed.height, processed.data, { frameCount: processed.frameCount, fps: processed.fps });
                    setUserAsset({ ...uploaded, previewUrl: processed.previewUrl, frames: processed.frames, fps: processed.fps });
                    current.user = { id: USER_BG_ID, width: processed.width, height: processed.height, previewUrl: processed.previewUrl, frames: processed.frames, fps: processed.fps, frameCount: processed.frameCount };
                } else {
                    const processed = await processImageToRGB565(file);
                    const uploaded = await uploadUserBackgroundImage(file.name, processed.width, processed.height, processed.data, { frameCount: 1, fps: 0 });
                    setUserAsset({ ...uploaded, previewUrl: processed.previewUrl });
                    current.user = { id: USER_BG_ID, width: processed.width, height: processed.height, previewUrl: processed.previewUrl, frameCount: 1 };
                }
                saveBgImagesCache(current);
            }
            setBackgroundImageId(USER_BG_ID);
            setStandbyDisplay('backgroundImage');
            stageDeferredScreenControl({ ...nextConfig, standbyDisplay: 'backgroundImage', backgroundImageId: USER_BG_ID });
        } finally {
            setIsUploadingUserImage(false);
            e.target.value = '';
        }
    };

    const handleSetBackground = async (id: string) => {
        setBackgroundImageId(id);
        setStandbyDisplay('backgroundImage');
        stageDeferredScreenControl({ ...nextConfig, standbyDisplay: 'backgroundImage', backgroundImageId: id });
    };

    const handleDeleteUserAsset = async () => {
        if (!userAsset?.id) return;
        setUserAsset(null);
        {
            const current = loadBgImagesCache() ?? { system: systemAsset, user: null };
            current.user = null;
            saveBgImagesCache(current);
        }
        void deleteUserBackgroundImage().catch(() => {
        });
        if (backgroundImageId === USER_BG_ID) {
            setBackgroundImageId(SYSTEM_BG_ID);
            stageDeferredScreenControl({ ...nextConfig, backgroundImageId: SYSTEM_BG_ID });
        }
    };

    const ActionsRow = ({ items }: { items: Array<React.ReactElement | null> }) => {
        const filtered = items.filter(Boolean) as React.ReactElement[];
        if (filtered.length === 0) return null;
        return (
            <HStack gap={2}
                position="absolute"
                left="50%"
                transform="translateX(-50%)"
                bottom="-24px"
                justifyContent="center"
                w="max-content"
                maxW="none"
                flexWrap="nowrap"
                whiteSpace="nowrap"
                display="inline-flex"
            >
                
                {filtered.map((el, idx) => (
                    <React.Fragment key={idx}  >
                        {idx > 0 && <Text color="gray.400" whiteSpace="nowrap">|</Text>}
                        {el}
                    </React.Fragment>
                ))}
            </HStack>
        );
    };
    const BGSlot = ({
        selected,
        previewUrl,
        onClick,
        emptyBg = "gray.800",
        showUploadTrigger = false,
        uploading = false,
        onUploadClick,
    }: {
        selected: boolean;
        previewUrl?: string;
        onClick?: () => void;
        emptyBg?: string;
        showUploadTrigger?: boolean;
        uploading?: boolean;
        onUploadClick?: () => void;
    }) => {
        const borderColor = selected ? "green.500" : "gray.400";
        const hoverBorderColor = selected ? "green.600" : "gray.300";
        const clickable = !uploading && (!!onClick || (!!onUploadClick && showUploadTrigger && !previewUrl));
        return (
            <Box
                width="160px"
                height="86px"
                borderWidth="1px"
                borderColor={borderColor}
                rounded="md"
                position="relative"
                overflow="hidden"
                bg={previewUrl ? "transparent" : emptyBg}
                _hover={{ borderColor: hoverBorderColor }}
                cursor={clickable ? "pointer" : "default"}
                onClick={() => {
                    if (uploading) return;
                    if (previewUrl) {
                        onClick?.();
                    } else if (showUploadTrigger) {
                        onUploadClick?.();
                    }
                }}
            >
                {previewUrl && (
                    <Image
                        src={previewUrl}
                        alt=""
                        w="100%"
                        h="100%"
                        objectFit="cover"
                    />
                )}
                {!previewUrl && showUploadTrigger && (
                    <Box
                        position="absolute"
                        inset="0"
                        display="flex"
                        alignItems="center"
                        justifyContent="center"
                        color="gray.300"
                    >
                        {uploading ? <Spinner size="sm" /> : <LuUpload />}
                    </Box>
                )}
                {selected && (
                    <Box
                        position="absolute"
                        right="2"
                        bottom="2"
                        bg="green.500"
                        color="white"
                        rounded="full"
                        display="flex"
                        alignItems="center"
                        justifyContent="center"
                        boxSize="20px"
                    >
                        <LuCheck />
                    </Box>
                )}
            </Box>
        );
    };

    const dragFeatureKeyRef = React.useRef<ScreenControlFeatureKey | null>(null);

    const featureLabelMap: Record<ScreenControlFeatureKey, string> = {
        inputModeSwitch: t.SETTINGS_SCREEN_CONTROL_FEATURE_INPUT_MODE_SWITCH,
        profilesSwitch: t.SETTINGS_SCREEN_CONTROL_FEATURE_PROFILES_SWITCH,
        socdModeSwitch: t.SETTINGS_SCREEN_CONTROL_FEATURE_SOCD_MODE_SWITCH,
        connectionModeSwitch: t.SETTINGS_SCREEN_CONTROL_FEATURE_CONNECTION_MODE_SWITCH,
        buttonsPerformanceQuickSet: t.SETTINGS_SCREEN_CONTROL_FEATURE_BUTTONS_PERFORMANCE_QUICK_SET,
        ledBrightnessAdjust: t.SETTINGS_SCREEN_CONTROL_FEATURE_LED_BRIGHTNESS_ADJUST,
        ledEffectSwitch: t.SETTINGS_SCREEN_CONTROL_FEATURE_LED_EFFECT_SWITCH,
        ambientBrightnessAdjust: t.SETTINGS_SCREEN_CONTROL_FEATURE_AMBIENT_BRIGHTNESS_ADJUST,
        ambientEffectSwitch: t.SETTINGS_SCREEN_CONTROL_FEATURE_AMBIENT_EFFECT_SWITCH,
        screenBrightnessAdjust: t.SETTINGS_SCREEN_CONTROL_FEATURE_SCREEN_BRIGHTNESS_ADJUST,
        webConfigEntry: t.SETTINGS_SCREEN_CONTROL_FEATURE_WEB_CONFIG_ENTRY,
        calibrationModeSwitch: t.SETTINGS_SCREEN_CONTROL_FEATURE_CALIBRATION_MODE_SWITCH,
    };

    const orderedFeatureItems = featuresOrder.map((key) => ({ key, label: featureLabelMap[key] }));

    const featureKeyToId: Record<ScreenControlFeatureKey, number> = {
        inputModeSwitch: 0,
        profilesSwitch: 1,
        socdModeSwitch: 2,
        connectionModeSwitch: 3,
        buttonsPerformanceQuickSet: 11,
        ledBrightnessAdjust: 4,
        ledEffectSwitch: 5,
        ambientBrightnessAdjust: 6,
        ambientEffectSwitch: 7,
        screenBrightnessAdjust: 8,
        webConfigEntry: 9,
        calibrationModeSwitch: 10,
    };
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
            
            
            
            <VStack align="stretch" gap={4} >

                <TitleLabel title={t.SETTINGS_SCREEN_CONTROL_BASIC} />


                <Slider
                    size="sm"
                    width="372px"
                    min={0}
                    max={100}
                    step={10}
                    colorPalette="green"
                    disabled={disabled}
                    value={[brightness]}
                    label={t.SETTINGS_SCREEN_CONTROL_BRIGHTNESS_LABEL}
                    showValue
                    onValueChange={(details: { value: number[] }) => setBrightness(details.value[0])}
                    onValueChangeEnd={async () => {
                        await push();
                    }}
                />

                <RadioCard.Root
                    size="sm"
                    value={screenStyle}
                    variant="subtle"
                    onValueChange={async (d) => {
                        const v = (d as { value: ScreenStyle }).value;
                        if (!v || v === screenStyle) return;
                        setScreenStyle(v);
                        await commitUiChange({ ...nextConfig, screenStyle: v });
                    }}
                >
                    <HStack>
                        {[
                            { value: 'dark', label: 'Dark' },
                            { value: 'light', label: 'Light' },
                        ].map(opt => (
                            <RadioCard.Item w="180px" key={opt.value} value={opt.value as ScreenStyle} disabled={disabled}>
                                <RadioCard.ItemHiddenInput />
                                <RadioCard.ItemControl>
                                    <RadioCard.ItemText>{opt.label}</RadioCard.ItemText>
                                </RadioCard.ItemControl>
                            </RadioCard.Item>
                        ))}
                    </HStack>
                </RadioCard.Root>

                <TitleLabel title={t.SETTINGS_SCREEN_CONTROL_STANDBY_DISPLAY_LABEL} />
                <VStack align="start" gap={1}>
                    <RadioCard.Root
                        size={"sm"}
                        value={standbyDisplay}
                        variant={"subtle"}
                        onValueChange={async (d) => {
                            const v = (d as { value: 'none'|'backgroundImage'|'buttonLayout' }).value;
                            setStandbyDisplay(v);
                            const update: Partial<ScreenControlConfig> = { standbyDisplay: v };
                            if (v === 'backgroundImage') {
                                const targetId = backgroundImageId || userAsset?.id || SYSTEM_BG_ID;
                                setBackgroundImageId(targetId);
                                (update as Partial<ScreenControlConfig>).backgroundImageId = targetId;
                            }
                            await commitUiChange({ ...nextConfig, ...update });
                        }}
                    >
                        <HStack>
                            {[
                                { value: 'none', label: t.SETTINGS_SCREEN_CONTROL_STANDBY_NONE },
                                { value: 'backgroundImage', label: t.SETTINGS_SCREEN_CONTROL_STANDBY_BACKGROUND_IMAGE },
                                { value: 'buttonLayout', label: t.SETTINGS_SCREEN_CONTROL_STANDBY_BUTTON_LAYOUT },
                            ].map(opt => (
                                <RadioCard.Item w="242px" key={opt.value} value={opt.value as 'none'|'backgroundImage'|'buttonLayout'}>
                                    <RadioCard.ItemHiddenInput />
                                    <RadioCard.ItemControl>
                                        <RadioCard.ItemText>{opt.label}</RadioCard.ItemText>
                                    </RadioCard.ItemControl>
                                </RadioCard.Item>
                            ))}
                        </HStack>
                    </RadioCard.Root>
                </VStack>
                <SettingDescription
                    text={t.SETTINGS_SCREEN_CONTROL_BACKGROUND_IMAGE_LIMIT_TIP.replace('{seconds}', (Math.floor(gifMaxFrames/gifTargetFps)).toString())}
                    fontSize="xs"
                />
               

                <Input ref={fileInputRef} type="file" accept="image/*" display="none" onChange={handleFileChange} />

                <Grid templateColumns="1fr 1px 1fr" w="full" alignItems="stretch">
                    <Flex justifyContent="center">
                    <VStack align="center" gap={2} position="relative" overflow="visible" >
                        <BGSlot
                            selected={standbyDisplay === 'backgroundImage' && backgroundImageId === SYSTEM_BG_ID}
                            previewUrl={systemPreviewUrl}
                            onClick={() => handleSetBackground(SYSTEM_BG_ID)}
                            emptyBg="gray.800"
                            showUploadTrigger={false}
                            uploading={isDownloadingBgImages}
                        />
                        {!isDownloadingBgImages && (
                            <ActionsRow
                                items={[
                                    <ActionLink
                                        key="load-previews"
                                        label={t.SETTINGS_SCREEN_CONTROL_BACKGROUND_IMAGE_LOAD_PREVIEWS_BUTTON}
                                        onClick={() => void fetchBgImagesFromDeviceOnce()}
                                    />,
                                    <ActionLink
                                        key="set"
                                        label={t.SETTINGS_SCREEN_CONTROL_BACKGROUND_IMAGE_SET_BUTTON}
                                        hidden={standbyDisplay === 'backgroundImage' && backgroundImageId === SYSTEM_BG_ID}
                                        onClick={() => void handleSetBackground(SYSTEM_BG_ID)}
                                    />
                                ]}
                            />
                        )}
                    </VStack>
                    </Flex>

                    <Box w="1px" h="90px" bg="gray.800" alignSelf="stretch" />

                    <Flex justifyContent="center">
                    <VStack align="center" gap={2} position="relative" overflow="visible" >
                        <BGSlot
                            selected={standbyDisplay === 'backgroundImage' && !!userAsset && backgroundImageId === userAsset?.id}
                            previewUrl={userSlotPreviewUrl}
                            onClick={userAsset ? () => handleSetBackground(userAsset.id) : undefined}
                            emptyBg="gray.800"
                            showUploadTrigger={!userAsset}
                            uploading={isUploadingUserImage || isDownloadingBgImages}
                            onUploadClick={() => handleUploadClick()}
                        />
                        {!isUploadingUserImage && !isDownloadingBgImages && (
                            <ActionsRow
                                items={[
                                    userAsset && (standbyDisplay !== 'backgroundImage' || backgroundImageId !== userAsset.id)
                                        ? <ActionLink
                                            key="set"
                                            label={t.SETTINGS_SCREEN_CONTROL_BACKGROUND_IMAGE_SET_BUTTON}
                                            onClick={() => void handleSetBackground(userAsset.id)}
                                        />
                                        : null,
                                    !userAsset
                                        ? <ActionLink
                                            key="upload"
                                            label={t.SETTINGS_SCREEN_CONTROL_BACKGROUND_IMAGE_UPLOAD_BUTTON}
                                            onClick={() => void handleUploadClick()}
                                        />
                                        : null,
                                    userAsset
                                        ? <ActionLink
                                            key="delete"
                                            label={t.SETTINGS_SCREEN_CONTROL_BACKGROUND_IMAGE_DELETE_BUTTON}
                                            onClick={() => void handleDeleteUserAsset()}
                                        />
                                        : null,
                                ]}
                            />
                        )}
                    </VStack>
                    </Flex>
                </Grid>

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
                                        <RadioGroup.Item value={item.key} >
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
