'use client';

import React, { useEffect, useMemo, useState } from 'react';
import { Grid, VStack, HStack, Portal, Color, parseColor, Table, Image, Box, Flex, Text, Input, Spinner } from '@chakra-ui/react';
import { Slider } from '@/components/ui/slider';
import { Field } from '@/components/ui/field';
import { Switch } from '@/components/ui/switch';
import { useGamepadConfig } from '@/contexts/gamepad-config-context';
import { ScreenControlConfig } from '@/types/gamepad-config';
import { useLanguage } from '@/contexts/language-context';
import { ColorPicker } from '@chakra-ui/react';
import { LuCheck, LuUpload } from "react-icons/lu";
import { TitleLabel } from './ui/title-label';



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
};

export function ScreenControlSettingContent(props: ScreenControlSettingContentProps) {
    const { disabled = false } = props;
    const { screenControl, updateScreenControl, sendBinaryMessage, onBinaryMessage, wsConnected } = useGamepadConfig();
    const [brightness, setBrightness] = useState<number>(screenControl.brightness ?? 100);
    const [backgroundImageEnabled, setBackgroundImageEnabled] = useState<boolean>(screenControl.backgroundImageEnabled ?? false);
    const [bgColor, setBgColor] = useState<Color>(parseColor(toHex6(screenControl.backgroundColor ?? 0)));
    const [textColor, setTextColor] = useState<Color>(parseColor(toHex6(screenControl.textColor ?? 0xFFFFFF)));
    const [backgroundImageId, setBackgroundImageId] = useState<string>(screenControl.backgroundImageId ?? '');
    const [currentPageId, setCurrentPageId] = useState<string>(String(screenControl.currentPageId ?? 0));
    const [features, setFeatures] = useState(screenControl.features);
    const { t } = useLanguage();
    
    // 颜色队列状态
    const defaultColorSwatches = [
        '#FF0000', '#00FF00', '#0000FF', '#FFFF00', '#FF00FF', '#00FFFF', '#FFFFFF', '#000000'
    ];
    const [colorSwatches, setColorSwatches] = useState<string[]>(defaultColorSwatches);
    const [tempSelectedColors, setTempSelectedColors] = useState<{ bg?: string; text?: string }>({});
    const SYSTEM_BG_ID = 'SYSTEM_DEFAULT';
    const USER_BG_ID = 'USER_IMAGE';
    const [userAsset, setUserAsset] = useState<{ id: string; name: string; width: number; height: number; previewUrl: string } | null>(null);
    const [isUploadingUserImage, setIsUploadingUserImage] = useState(false);
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

    useEffect(() => {
        setBrightness(screenControl.brightness ?? 100);
        setBackgroundImageEnabled(screenControl.backgroundImageEnabled ?? false);
        setBgColor(parseColor(toHex6(screenControl.backgroundColor ?? 0)));
        setTextColor(parseColor(toHex6(screenControl.textColor ?? 0xFFFFFF)));

        setBackgroundImageId(screenControl.backgroundImageId ?? '');
        setCurrentPageId(String(screenControl.currentPageId ?? 0));
        setFeatures(screenControl.features);
    }, [screenControl]);

    type BgImagesCache = {
        system?: { id: string; width: number; height: number; previewUrl: string } | null;
        user?: { id: string; width: number; height: number; previewUrl: string } | null;
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

    const handleColorChange = (type: 'bg' | 'text', newColor: Color) => {
        const hexColor = newColor.toString('hex');
        if (type === 'bg') {
            setBgColor(newColor);
        } else {
            setTextColor(newColor);
        }
        setTempSelectedColors(prev => ({
            ...prev,
            [type]: hexColor
        }));
    };

    const handleColorPickerClose = (type: 'bg' | 'text') => {
        const selectedColor = tempSelectedColors[type];
        if (selectedColor && !colorSwatches.includes(selectedColor)) {
            setColorSwatches(prev => [selectedColor, ...prev.slice(0, 7)]);
        }
    };

    const nextConfig: ScreenControlConfig = useMemo(() => {
        const b = Math.max(0, Math.min(100, brightness | 0));
        const bg = parseHex6(bgColor.toString('hex'), screenControl.backgroundColor ?? 0);
        const fg = parseHex6(textColor.toString('hex'), screenControl.textColor ?? 0xFFFFFF);
        const pid = Math.max(0, Math.min(65535, parseInt(currentPageId || '0', 10) || 0));
        return {
            brightness: b,
            backgroundImageEnabled,
            backgroundColor: bg,
            textColor: fg,
            backgroundImageId,
            currentPageId: pid,
            features,
        };
    }, [brightness, backgroundImageEnabled, bgColor, textColor, backgroundImageId, currentPageId, features, screenControl.backgroundColor, screenControl.textColor]);

    const push = async () => {
        await updateScreenControl(nextConfig, true);
    };

    const rgb565ToPngDataUrl = (rgb565le: Uint8Array, width: number, height: number) => {
        const canvas = document.createElement('canvas');
        canvas.width = width;
        canvas.height = height;
        const ctx = canvas.getContext('2d', { willReadFrequently: true })!;

        const rgba = new Uint8ClampedArray(width * height * 4);
        for (let i = 0, j = 0; i < rgb565le.length; i += 2, j += 4) {
            const v = rgb565le[i] | (rgb565le[i + 1] << 8);
            const r = (v >> 11) & 0x1F;
            const g = (v >> 5) & 0x3F;
            const b = v & 0x1F;
            rgba[j] = (r * 255) / 31;
            rgba[j + 1] = (g * 255) / 63;
            rgba[j + 2] = (b * 255) / 31;
            rgba[j + 3] = 255;
        }
        const img = new ImageData(rgba, width, height);
        ctx.putImageData(img, 0, 0);
        return canvas.toDataURL('image/png');
    };

    const processImageToRGB565 = async (file: File) => {
        const bitmap = await createImageBitmap(file);
        const maxW = 320;
        const maxH = 172;
        const scale = Math.min(1, Math.min(maxW / bitmap.width, maxH / bitmap.height));
        const w = Math.max(1, Math.floor(bitmap.width * scale));
        const h = Math.max(1, Math.floor(bitmap.height * scale));
        const canvas = document.createElement('canvas');
        canvas.width = maxW;
        canvas.height = maxH;
        const ctx = canvas.getContext('2d', { willReadFrequently: true })!;
        ctx.fillStyle = '#000000';
        ctx.fillRect(0, 0, maxW, maxH);
        const dx = Math.floor((maxW - w) / 2);
        const dy = Math.floor((maxH - h) / 2);
        ctx.drawImage(bitmap, dx, dy, w, h);
        const imgData = ctx.getImageData(0, 0, maxW, maxH);
        const src = imgData.data;
        const out = new Uint8Array(maxW * maxH * 2);
        for (let i = 0, j = 0; i < src.length; i += 4) {
            const r = src[i];
            const g = src[i + 1];
            const b = src[i + 2];
            const r5 = r >> 3;
            const g6 = g >> 2;
            const b5 = b >> 3;
            const v = (r5 << 11) | (g6 << 5) | b5;
            out[j++] = v & 0xFF;
            out[j++] = (v >> 8) & 0xFF;
        }
        const previewUrl = canvas.toDataURL('image/png');
        return { width: maxW, height: maxH, data: out, previewUrl };
    };

    const sendBinaryUserImageRequest = async (payload: Uint8Array, expectedRespCmd: number, cid: number) => {
        return new Promise<{ success: boolean; received: number; total: number; error?: string }>((resolve, reject) => {
            const timeout = window.setTimeout(() => {
                unsubscribe();
                reject(new Error('Binary request timeout'));
            }, 15000);

            const unsubscribe = onBinaryMessage((data) => {
                try {
                    const view = new DataView(data);
                    if (view.byteLength < 1 + 1 + 4 + 4 + 4 + 1) return;
                    const cmd = view.getUint8(0);
                    if (cmd !== expectedRespCmd) return;
                    const respCid = view.getUint32(2, true);
                    if (respCid !== cid) return;

                    window.clearTimeout(timeout);
                    unsubscribe();

                    const success = view.getUint8(1) === 1;
                    const received = view.getUint32(6, true);
                    const total = view.getUint32(10, true);
                    const errLen = view.getUint8(14);
                    let error: string | undefined;
                    if (!success && errLen > 0 && view.byteLength >= 15 + errLen) {
                        const bytes = new Uint8Array(data, 15, errLen);
                        error = new TextDecoder().decode(bytes);
                    }
                    resolve({ success, received, total, error });
                } catch (e) {
                    window.clearTimeout(timeout);
                    unsubscribe();
                    reject(e);
                }
            });

            sendBinaryMessage(payload);
        });
    };

    const fetchBgImagesFromDeviceOnce = async () => {
        if (bgImagesLoadedRef.current) return;
        if (!wsConnected) return;

        const cached = loadBgImagesCache();
        if (cached) {
            if (cached.system) setSystemAsset(cached.system);
            if (cached.user) setUserAsset({ ...cached.user, name: cached.user.id });
            bgImagesLoadedRef.current = true;
            return;
        }

        try {
        const CMD_GET_INFO = 0x34;
        const RESP_GET_INFO = 0xB4;
        const CMD_READ_CHUNK = 0x35;
        const RESP_READ_CHUNK = 0xB5;

        const cid = (Math.random() * 0xFFFFFFFF) >>> 0;
        const req = new ArrayBuffer(6);
        const reqView = new DataView(req);
        reqView.setUint8(0, CMD_GET_INFO);
        reqView.setUint8(1, 0);
        reqView.setUint32(2, cid, true);

        const info = await new Promise<{
            userValid: boolean;
            userWidth: number;
            userHeight: number;
            userSize: number;
            sysValid: boolean;
            sysWidth: number;
            sysHeight: number;
            sysSize: number;
        }>((resolve, reject) => {
            const timeout = window.setTimeout(() => {
                unsubscribe();
                reject(new Error('Get info timeout'));
            }, 5000);

            const unsubscribe = onBinaryMessage((data) => {
                const view = new DataView(data);
                if (view.byteLength < 56) return;
                const cmd = view.getUint8(0);
                if (cmd !== RESP_GET_INFO) return;
                const respCid = view.getUint32(2, true);
                if (respCid !== cid) return;

                window.clearTimeout(timeout);
                unsubscribe();

                const userValid = view.getUint8(6) === 1;
                const sysValid = view.getUint8(7) === 1;
                const userWidth = view.getUint16(8, true);
                const userHeight = view.getUint16(10, true);
                const userSize = view.getUint32(12, true);
                const sysWidth = view.getUint16(16, true);
                const sysHeight = view.getUint16(18, true);
                const sysSize = view.getUint32(20, true);
                resolve({ userValid, userWidth, userHeight, userSize, sysValid, sysWidth, sysHeight, sysSize });
            });

            sendBinaryMessage(new Uint8Array(req));
        });

        const readImage = async (target: 0 | 1, width: number, height: number, total: number) => {
            const all = new Uint8Array(total);
            const chunkSize = 4096;
            const readCid = (Math.random() * 0xFFFFFFFF) >>> 0;
            for (let offset = 0; offset < total; offset += chunkSize) {
                const want = Math.min(chunkSize, total - offset);
                const hdr = new ArrayBuffer(14);
                const dv = new DataView(hdr);
                dv.setUint8(0, CMD_READ_CHUNK);
                dv.setUint8(1, target);
                dv.setUint32(2, readCid, true);
                dv.setUint32(6, offset, true);
                dv.setUint16(10, want, true);
                dv.setUint16(12, 0, true);

                const resp = await new Promise<{ payload: Uint8Array }>((resolve, reject) => {
                    const timeout = window.setTimeout(() => {
                        unsubscribe();
                        reject(new Error('Read chunk timeout'));
                    }, 5000);

                    const unsubscribe = onBinaryMessage((data) => {
                        const view = new DataView(data);
                        if (view.byteLength < 55) return;
                        const cmd = view.getUint8(0);
                        if (cmd !== RESP_READ_CHUNK) return;
                        const respCid = view.getUint32(4, true);
                        if (respCid !== readCid) return;
                        const respTarget = view.getUint8(2);
                        if (respTarget !== target) return;
                        const respOffset = view.getUint32(16, true);
                        if (respOffset !== offset) return;

                        window.clearTimeout(timeout);
                        unsubscribe();

                        const success = view.getUint8(1) === 1;
                        if (!success) {
                            const errLen = view.getUint8(22);
                            let msg = 'Read failed';
                            if (errLen > 0 && view.byteLength >= 23 + errLen) {
                                msg = new TextDecoder().decode(new Uint8Array(data, 23, errLen));
                            }
                            reject(new Error(msg));
                            return;
                        }
                        const got = view.getUint16(20, true);
                        const payload = new Uint8Array(data, 55, got);
                        resolve({ payload });
                    });

                    sendBinaryMessage(new Uint8Array(hdr));
                });

                all.set(resp.payload, offset);
            }
            const previewUrl = rgb565ToPngDataUrl(all, width, height);
            return previewUrl;
        };

        const cache: BgImagesCache = { system: null, user: null };

        if (info.sysValid && info.sysWidth > 0 && info.sysHeight > 0 && info.sysSize > 0) {
            const previewUrl = await readImage(1, info.sysWidth, info.sysHeight, info.sysSize);
            const sys = { id: SYSTEM_BG_ID, width: info.sysWidth, height: info.sysHeight, previewUrl };
            setSystemAsset(sys);
            cache.system = sys;
        }

        if (info.userValid && info.userWidth > 0 && info.userHeight > 0 && info.userSize > 0) {
            const previewUrl = await readImage(0, info.userWidth, info.userHeight, info.userSize);
            const usr = { id: USER_BG_ID, width: info.userWidth, height: info.userHeight, previewUrl };
            setUserAsset({ ...usr, name: usr.id });
            cache.user = usr;
        }

        saveBgImagesCache(cache);
        bgImagesLoadedRef.current = true;
        } catch {
            bgImagesLoadedRef.current = false;
        }
    };

    useEffect(() => {
        if (!wsConnected) return;
        void fetchBgImagesFromDeviceOnce();
    }, [wsConnected]);

    const uploadUserBackgroundImage = async (name: string, width: number, height: number, data: Uint8Array) => {
        const CMD_BEGIN = 0x30;
        const CMD_CHUNK = 0x31;
        const CMD_COMMIT = 0x32;
        const RESP_BEGIN = 0xB0;
        const RESP_CHUNK = 0xB1;
        const RESP_COMMIT = 0xB2;

        const cid = (Math.random() * 0xFFFFFFFF) >>> 0;

        const begin = new ArrayBuffer(14);
        const beginView = new DataView(begin);
        beginView.setUint8(0, CMD_BEGIN);
        beginView.setUint8(1, 0);
        beginView.setUint32(2, cid, true);
        beginView.setUint16(6, width, true);
        beginView.setUint16(8, height, true);
        beginView.setUint32(10, data.length, true);
        const beginResp = await sendBinaryUserImageRequest(new Uint8Array(begin), RESP_BEGIN, cid);
        if (!beginResp.success) throw new Error(beginResp.error || 'Begin failed');

        const chunkSize = 4096;
        for (let offset = 0; offset < data.length; offset += chunkSize) {
            const part = data.subarray(offset, Math.min(data.length, offset + chunkSize));
            const header = new ArrayBuffer(14);
            const headerView = new DataView(header);
            headerView.setUint8(0, CMD_CHUNK);
            headerView.setUint8(1, 0);
            headerView.setUint32(2, cid, true);
            headerView.setUint32(6, offset, true);
            headerView.setUint16(10, part.length, true);
            headerView.setUint16(12, 0, true);
            const msg = new Uint8Array(14 + part.length);
            msg.set(new Uint8Array(header), 0);
            msg.set(part, 14);
            const chunkResp = await sendBinaryUserImageRequest(msg, RESP_CHUNK, cid);
            if (!chunkResp.success) throw new Error(chunkResp.error || 'Chunk failed');
        }

        const commit = new ArrayBuffer(6);
        const commitView = new DataView(commit);
        commitView.setUint8(0, CMD_COMMIT);
        commitView.setUint8(1, 0);
        commitView.setUint32(2, cid, true);
        const commitResp = await sendBinaryUserImageRequest(new Uint8Array(commit), RESP_COMMIT, cid);
        if (!commitResp.success) throw new Error(commitResp.error || 'Commit failed');

        return { id: USER_BG_ID, name, width, height };
    };

    const deleteUserBackgroundImage = async () => {
        const CMD_DELETE = 0x33;
        const RESP_DELETE = 0xB3;
        const cid = (Math.random() * 0xFFFFFFFF) >>> 0;
        const del = new ArrayBuffer(6);
        const delView = new DataView(del);
        delView.setUint8(0, CMD_DELETE);
        delView.setUint8(1, 0);
        delView.setUint32(2, cid, true);
        const resp = await sendBinaryUserImageRequest(new Uint8Array(del), RESP_DELETE, cid);
        if (!resp.success) throw new Error(resp.error || 'Delete failed');
    };

    const ActionLink = (props: { label: string; onClick: () => void; hidden?: boolean }) => {
        if (props.hidden) return null;
        return (
            <Text
                as="button"
                fontSize="sm"
                color={disabled ? "gray.400" : "gray.400"}
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
            const processed = await processImageToRGB565(file);
            const uploaded = await uploadUserBackgroundImage(file.name, processed.width, processed.height, processed.data);
            const next = { ...uploaded, previewUrl: processed.previewUrl };
            setUserAsset(next);
            {
                const current = loadBgImagesCache() ?? { system: systemAsset, user: null };
                current.user = { id: USER_BG_ID, width: processed.width, height: processed.height, previewUrl: processed.previewUrl };
                saveBgImagesCache(current);
            }
            setBackgroundImageId(USER_BG_ID);
            setBackgroundImageEnabled(true);
            await updateScreenControl({ ...nextConfig, backgroundImageEnabled: true, backgroundImageId: USER_BG_ID }, true);
        } finally {
            setIsUploadingUserImage(false);
            e.target.value = '';
        }
    };

    const handleSetBackground = async (id: string) => {
        setBackgroundImageId(id);
        setBackgroundImageEnabled(true);
        await updateScreenControl({ ...nextConfig, backgroundImageEnabled: true, backgroundImageId: id }, true);
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
            await updateScreenControl({ ...nextConfig, backgroundImageId: SYSTEM_BG_ID }, true);
        }
    };

    const ActionsRow = ({ items }: { items: Array<React.ReactElement | null> }) => {
        const filtered = items.filter(Boolean) as React.ReactElement[];
        if (filtered.length === 0) return null;
        return (
            <HStack gap={2}>
                {filtered.map((el, idx) => (
                    <React.Fragment key={idx}>
                        {idx > 0 && <Text color="gray.400">|</Text>}
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

    return (

        <>
            {/* <MainContentHeader
                description={t.SETTINGS_SCREEN_CONTROL_HELPER_TEXT}
            /> */}

            <Text fontSize="14px" color="gray.400" mb="30px" whiteSpace="pre-wrap" >
                {t.SETTINGS_SCREEN_CONTROL_HELPER_TEXT}
            </Text>
            
            
            
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
                    onValueChangeEnd={() => void push()}
                />

                <Grid templateColumns="repeat(2, 1fr)" gap={4}>
                    <Field label={t.SETTINGS_SCREEN_CONTROL_BACKGROUND_COLOR_LABEL}>
                        <ColorPicker.Root size="xs"
                            value={bgColor}
                            onValueChange={(e: { value: Color }) => handleColorChange('bg', e.value)}
                            onValueChangeEnd={() => void push()}
                            onOpenChange={(details: { open: boolean }) => {
                                if (!details.open) handleColorPickerClose('bg');
                            }}
                            maxW="200px"
                            disabled={disabled}
                        >
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
                    </Field>
                    <Field label={t.SETTINGS_SCREEN_CONTROL_TEXT_COLOR_LABEL}>
                        <ColorPicker.Root size="xs"
                            value={textColor}
                            onValueChange={(e: { value: Color }) => handleColorChange('text', e.value)}
                            onValueChangeEnd={() => void push()}
                            onOpenChange={(details: { open: boolean }) => {
                                if (!details.open) handleColorPickerClose('text');
                            }}
                            maxW="200px"
                            disabled={disabled}
                        >
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
                    </Field>
                   
                </Grid>

                <TitleLabel title={t.SETTINGS_SCREEN_CONTROL_BACKGROUND_IMAGES_TITLE} />
                <HStack align="center" gap={4}>
                    <Switch
                        checked={backgroundImageEnabled}
                        onCheckedChange={async (e: { checked: boolean }) => {
                            if (!e.checked) {
                                setBackgroundImageEnabled(false);
                                await updateScreenControl({ ...nextConfig, backgroundImageEnabled: false }, true);
                            } else {
                                const targetId = backgroundImageId || userAsset?.id || SYSTEM_BG_ID;
                                setBackgroundImageEnabled(true);
                                setBackgroundImageId(targetId);
                                await updateScreenControl({ ...nextConfig, backgroundImageEnabled: true, backgroundImageId: targetId }, true);
                            }
                        }}
                    >
                        {t.SETTINGS_SCREEN_CONTROL_BACKGROUND_IMAGE_ENABLE_LABEL}
                    </Switch>
                </HStack>
                <Text fontSize="xs" color="gray.400">{t.SETTINGS_SCREEN_CONTROL_BACKGROUND_IMAGE_LIMIT_TIP}</Text>

                <Input ref={fileInputRef} type="file" accept="image/*" display="none" onChange={handleFileChange} />

                <Grid templateColumns="1fr 1px 1fr" w="full" alignItems="stretch">
                    <Flex justifyContent="center">
                    <VStack align="center" gap={2}>
                        <BGSlot
                            selected={backgroundImageEnabled && backgroundImageId === SYSTEM_BG_ID}
                            previewUrl={systemPreviewUrl}
                            onClick={() => handleSetBackground(SYSTEM_BG_ID)}
                            emptyBg="gray.800"
                            showUploadTrigger={false}
                        />
                        <ActionsRow
                            items={[
                                <ActionLink
                                    key="set"
                                    label={t.SETTINGS_SCREEN_CONTROL_BACKGROUND_IMAGE_SET_BUTTON}
                                    hidden={backgroundImageEnabled && backgroundImageId === SYSTEM_BG_ID}
                                    onClick={() => void handleSetBackground(SYSTEM_BG_ID)}
                                />
                            ]}
                        />
                    </VStack>
                    </Flex>

                    <Box w="1px" h="90px" bg="gray.800" alignSelf="stretch" />

                    <Flex justifyContent="center">
                    <VStack align="center" gap={2}>
                        <BGSlot
                            selected={backgroundImageEnabled && !!userAsset && backgroundImageId === userAsset?.id}
                            previewUrl={userAsset?.previewUrl}
                            onClick={userAsset ? () => handleSetBackground(userAsset.id) : undefined}
                            emptyBg="gray.800"
                            showUploadTrigger={!userAsset}
                            uploading={isUploadingUserImage}
                            onUploadClick={() => handleUploadClick()}
                        />
                        <ActionsRow
                            items={[
                                userAsset && (!backgroundImageEnabled || backgroundImageId !== userAsset.id)
                                    ? <ActionLink
                                        key="set"
                                        label={t.SETTINGS_SCREEN_CONTROL_BACKGROUND_IMAGE_SET_BUTTON}
                                        onClick={() => void handleSetBackground(userAsset.id)}
                                    />
                                    : null,
                                !userAsset && !isUploadingUserImage
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
                    </VStack>
                    </Flex>
                </Grid>

                <TitleLabel title={t.SETTINGS_SCREEN_CONTROL_FEATURES} mt="20px" />

                <Table.Root size="sm" colorPalette="green" interactive>
                    <Table.Body>
                        <Table.Row>
                            <Table.Cell py={3}>{t.SETTINGS_SCREEN_CONTROL_FEATURE_INPUT_MODE_SWITCH}</Table.Cell>
                            <Table.Cell py={3} textAlign="end">
                                <Switch
                                    checked={features.inputModeSwitch}
                                    disabled={disabled}
                                    onCheckedChange={(e: { checked: boolean }) => { setFeatures({ ...features, inputModeSwitch: e.checked }); void updateScreenControl({ ...nextConfig, features: { ...features, inputModeSwitch: e.checked } }, true); }}
                                />
                            </Table.Cell>
                        </Table.Row>
                        <Table.Row>
                            <Table.Cell py={3}>{t.SETTINGS_SCREEN_CONTROL_FEATURE_PROFILES_SWITCH}</Table.Cell>
                            <Table.Cell py={3} textAlign="end">
                                <Switch
                                    checked={features.profilesSwitch}
                                    disabled={disabled}
                                    onCheckedChange={(e: { checked: boolean }) => { setFeatures({ ...features, profilesSwitch: e.checked }); void updateScreenControl({ ...nextConfig, features: { ...features, profilesSwitch: e.checked } }, true); }}
                                />
                            </Table.Cell>
                        </Table.Row>
                        <Table.Row>
                            <Table.Cell py={3}>{t.SETTINGS_SCREEN_CONTROL_FEATURE_SOCD_MODE_SWITCH}</Table.Cell>
                            <Table.Cell py={3} textAlign="end">
                                <Switch
                                    checked={features.socdModeSwitch}
                                    disabled={disabled}
                                    onCheckedChange={(e: { checked: boolean }) => { setFeatures({ ...features, socdModeSwitch: e.checked }); void updateScreenControl({ ...nextConfig, features: { ...features, socdModeSwitch: e.checked } }, true); }}
                                />
                            </Table.Cell>
                        </Table.Row>
                        <Table.Row>
                            <Table.Cell py={3}>{t.SETTINGS_SCREEN_CONTROL_FEATURE_TOURNAMENT_MODE_SWITCH}</Table.Cell>
                            <Table.Cell py={3} textAlign="end">
                                <Switch
                                    checked={features.tournamentModeSwitch}
                                    disabled={disabled}
                                    onCheckedChange={(e: { checked: boolean }) => { setFeatures({ ...features, tournamentModeSwitch: e.checked }); void updateScreenControl({ ...nextConfig, features: { ...features, tournamentModeSwitch: e.checked } }, true); }}
                                />
                            </Table.Cell>
                        </Table.Row>
                        <Table.Row>
                            <Table.Cell py={3}>{t.SETTINGS_SCREEN_CONTROL_FEATURE_LED_BRIGHTNESS_ADJUST}</Table.Cell>
                            <Table.Cell py={3} textAlign="end">
                                <Switch
                                    checked={features.ledBrightnessAdjust}
                                    disabled={disabled}
                                    onCheckedChange={(e: { checked: boolean }) => { setFeatures({ ...features, ledBrightnessAdjust: e.checked }); void updateScreenControl({ ...nextConfig, features: { ...features, ledBrightnessAdjust: e.checked } }, true); }}
                                />
                            </Table.Cell>
                        </Table.Row>
                        <Table.Row>
                            <Table.Cell py={3}>{t.SETTINGS_SCREEN_CONTROL_FEATURE_LED_EFFECT_SWITCH}</Table.Cell>
                            <Table.Cell py={3} textAlign="end">
                                <Switch
                                    checked={features.ledEffectSwitch}
                                    disabled={disabled}
                                    onCheckedChange={(e: { checked: boolean }) => { setFeatures({ ...features, ledEffectSwitch: e.checked }); void updateScreenControl({ ...nextConfig, features: { ...features, ledEffectSwitch: e.checked } }, true); }}
                                />
                            </Table.Cell>
                        </Table.Row>
                        <Table.Row>
                            <Table.Cell py={3}>{t.SETTINGS_SCREEN_CONTROL_FEATURE_AMBIENT_BRIGHTNESS_ADJUST}</Table.Cell>
                            <Table.Cell py={3} textAlign="end">
                                <Switch
                                    checked={features.ambientBrightnessAdjust}
                                    disabled={disabled}
                                    onCheckedChange={(e: { checked: boolean }) => { setFeatures({ ...features, ambientBrightnessAdjust: e.checked }); void updateScreenControl({ ...nextConfig, features: { ...features, ambientBrightnessAdjust: e.checked } }, true); }}
                                />
                            </Table.Cell>
                        </Table.Row>
                        <Table.Row>
                            <Table.Cell py={3}>{t.SETTINGS_SCREEN_CONTROL_FEATURE_AMBIENT_EFFECT_SWITCH}</Table.Cell>
                            <Table.Cell py={3} textAlign="end">
                                <Switch
                                    checked={features.ambientEffectSwitch}
                                    disabled={disabled}
                                    onCheckedChange={(e: { checked: boolean }) => { setFeatures({ ...features, ambientEffectSwitch: e.checked }); void updateScreenControl({ ...nextConfig, features: { ...features, ambientEffectSwitch: e.checked } }, true); }}
                                />
                            </Table.Cell>
                        </Table.Row>
                        <Table.Row>
                            <Table.Cell py={3}>{t.SETTINGS_SCREEN_CONTROL_FEATURE_SCREEN_BRIGHTNESS_ADJUST}</Table.Cell>
                            <Table.Cell py={3} textAlign="end">
                                <Switch
                                    checked={features.screenBrightnessAdjust}
                                    disabled={disabled}
                                    onCheckedChange={(e: { checked: boolean }) => { setFeatures({ ...features, screenBrightnessAdjust: e.checked }); void updateScreenControl({ ...nextConfig, features: { ...features, screenBrightnessAdjust: e.checked } }, true); }}
                                />
                            </Table.Cell>
                        </Table.Row>
                        <Table.Row>
                            <Table.Cell py={3}>{t.SETTINGS_SCREEN_CONTROL_FEATURE_WEB_CONFIG_ENTRY}</Table.Cell>
                            <Table.Cell py={3} textAlign="end">
                                <Switch
                                    checked={features.webConfigEntry}
                                    disabled={disabled}
                                    onCheckedChange={(e: { checked: boolean }) => { setFeatures({ ...features, webConfigEntry: e.checked }); void updateScreenControl({ ...nextConfig, features: { ...features, webConfigEntry: e.checked } }, true); }}
                                />
                            </Table.Cell>
                        </Table.Row>
                        <Table.Row>
                            <Table.Cell py={3}>{t.SETTINGS_SCREEN_CONTROL_FEATURE_CALIBRATION_MODE_SWITCH}</Table.Cell>
                            <Table.Cell py={3} textAlign="end">
                                <Switch
                                    checked={features.calibrationModeSwitch}
                                    disabled={disabled}
                                    onCheckedChange={(e: { checked: boolean }) => { setFeatures({ ...features, calibrationModeSwitch: e.checked }); void updateScreenControl({ ...nextConfig, features: { ...features, calibrationModeSwitch: e.checked } }, true); }}
                                />
                            </Table.Cell>
                        </Table.Row>
                    </Table.Body>
                </Table.Root>
            </VStack>
        </>
    );
}
