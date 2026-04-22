'use client';

import React, { useEffect, useMemo, useState } from 'react';
import { Grid, VStack, HStack, Portal, Color, parseColor, Table, Image, Box, Flex, Text, Input, Spinner, RadioCard, RadioGroup } from '@chakra-ui/react';
import { Slider } from '@/components/ui/slider';
import { Field } from '@/components/ui/field';
import { Switch } from '@/components/ui/switch';
import { useGamepadConfig } from '@/contexts/gamepad-config-context';
import { DEFAULT_SCREEN_CONTROL_CONFIG, ScreenControlConfig, ScreenControlFeatureKey, ScreenControlFeatures, StandbyDisplay } from '@/types/gamepad-config';
import { useLanguage } from '@/contexts/language-context';
import { ColorPicker } from '@chakra-ui/react';
import { LuCheck, LuUpload, LuGripVertical } from "react-icons/lu";
import { TitleLabel } from './ui/title-label';
import { processGifToRGB565Sequence, processImageToRGB565, rgb565ToPngDataUrl } from '@/lib/screen-control-image';



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
    const [standbyDisplay, setStandbyDisplay] = useState<StandbyDisplay>(screenControl.standbyDisplay ?? 'none');
    const [bgColor, setBgColor] = useState<Color>(parseColor(toHex6(screenControl.backgroundColor ?? 0)));
    const [textColor, setTextColor] = useState<Color>(parseColor(toHex6(screenControl.textColor ?? 0xFFFFFF)));
    const [backgroundImageId, setBackgroundImageId] = useState<string>(screenControl.backgroundImageId ?? '');
    const [currentPageId, setCurrentPageId] = useState<string>(String(screenControl.currentPageId ?? 0));
    const [features, setFeatures] = useState(screenControl.features);
    const [featuresOrder, setFeaturesOrder] = useState<ScreenControlFeatureKey[]>(
        screenControl.featuresOrder ?? DEFAULT_SCREEN_CONTROL_CONFIG.featuresOrder
    );
    const [dropIndicator, setDropIndicator] = useState<{ key: ScreenControlFeatureKey; position: 'before' | 'after' } | null>(null);
    const { t } = useLanguage();
    
    // 颜色队列状态
    const defaultColorSwatches = [
        '#FF0000', '#00FF00', '#0000FF', '#FFFF00', '#FF00FF', '#00FFFF', '#FFFFFF', '#000000'
    ];
    const [colorSwatches, setColorSwatches] = useState<string[]>(defaultColorSwatches);
    const [tempSelectedColors, setTempSelectedColors] = useState<{ bg?: string; text?: string }>({});
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
        setBgColor(parseColor(toHex6(screenControl.backgroundColor ?? 0)));
        setTextColor(parseColor(toHex6(screenControl.textColor ?? 0xFFFFFF)));

        setBackgroundImageId(screenControl.backgroundImageId ?? '');
        setCurrentPageId(String(screenControl.currentPageId ?? 0));
        setFeatures(screenControl.features);
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
            standbyDisplay,
            backgroundColor: bg,
            textColor: fg,
            backgroundImageId,
            currentPageId: pid,
            features,
            featuresOrder,
        };
    }, [brightness, standbyDisplay, bgColor, textColor, backgroundImageId, currentPageId, features, featuresOrder, screenControl.backgroundColor, screenControl.textColor]);

    const push = async () => {
        await updateScreenControl(nextConfig, true);
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
        setIsDownloadingBgImages(true);
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
            userFrameCount: number;
            userFps: number;
            userFormat: number;
            sysValid: boolean;
            sysWidth: number;
            sysHeight: number;
            sysSize: number;
            sysFrameCount: number;
            sysFps: number;
            sysFormat: number;
        }>((resolve, reject) => {
            const timeout = window.setTimeout(() => {
                unsubscribe();
                reject(new Error('Get info timeout'));
            }, 5000);

            const unsubscribe = onBinaryMessage((data) => {
                const view = new DataView(data);
                if (view.byteLength < 64) return;
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
                const userFrameCount = view.getUint8(16);
                const userFps = view.getUint8(17);
                const userFormat = view.getUint8(18);
                const sysWidth = view.getUint16(20, true);
                const sysHeight = view.getUint16(22, true);
                const sysSize = view.getUint32(24, true);
                const sysFrameCount = view.getUint8(28);
                const sysFps = view.getUint8(29);
                const sysFormat = view.getUint8(30);
                resolve({ userValid, userWidth, userHeight, userSize, userFrameCount, userFps, userFormat, sysValid, sysWidth, sysHeight, sysSize, sysFrameCount, sysFps, sysFormat });
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
            return all;
        };

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

        if (info.sysValid && info.sysWidth > 0 && info.sysHeight > 0 && info.sysSize > 0) {
            const bytes = await readImage(1, info.sysWidth, info.sysHeight, info.sysSize);
            const previews = buildPreviews(bytes, info.sysWidth, info.sysHeight, info.sysFormat, info.sysFrameCount || 1, info.sysFps || 0);
            const sys = { id: SYSTEM_BG_ID, width: info.sysWidth, height: info.sysHeight, previewUrl: previews.previewUrl };
            setSystemAsset(sys);
            cache.system = sys;
        }

        if (info.userValid && info.userWidth > 0 && info.userHeight > 0 && info.userSize > 0) {
            const bytes = await readImage(0, info.userWidth, info.userHeight, info.userSize);
            const previews = buildPreviews(bytes, info.userWidth, info.userHeight, info.userFormat, info.userFrameCount || 1, info.userFps || 0);
            const usr = { id: USER_BG_ID, width: info.userWidth, height: info.userHeight, previewUrl: previews.previewUrl, frames: previews.frames, fps: previews.fps, frameCount: previews.frameCount };
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

    useEffect(() => {
        if (!wsConnected) return;
        void fetchBgImagesFromDeviceOnce();
    }, [wsConnected]);

    const uploadUserBackgroundImage = async (name: string, width: number, height: number, data: Uint8Array, opts: { frameCount: number; fps: number }) => {
        const CMD_BEGIN = 0x30;
        const CMD_CHUNK = 0x31;
        const CMD_COMMIT = 0x32;
        const RESP_BEGIN = 0xB0;
        const RESP_CHUNK = 0xB1;
        const RESP_COMMIT = 0xB2;

        const cid = (Math.random() * 0xFFFFFFFF) >>> 0;

        const frameCount = Math.max(1, Math.min(10, opts.frameCount | 0));
        const fps = Math.max(0, Math.min(5, opts.fps | 0));
        const imageType = frameCount > 1 ? 1 : 0;

        const begin = new ArrayBuffer(18);
        const beginView = new DataView(begin);
        beginView.setUint8(0, CMD_BEGIN);
        beginView.setUint8(1, imageType);
        beginView.setUint32(2, cid, true);
        beginView.setUint16(6, width, true);
        beginView.setUint16(8, height, true);
        beginView.setUint32(10, data.length, true);
        beginView.setUint8(14, frameCount);
        beginView.setUint8(15, fps);
        beginView.setUint16(16, 0, true);
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

        return { id: USER_BG_ID, name, width, height, frameCount, fps };
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
            await updateScreenControl({ ...nextConfig, standbyDisplay: 'backgroundImage', backgroundImageId: USER_BG_ID }, true);
        } finally {
            setIsUploadingUserImage(false);
            e.target.value = '';
        }
    };

    const handleSetBackground = async (id: string) => {
        setBackgroundImageId(id);
        setStandbyDisplay('backgroundImage');
        await updateScreenControl({ ...nextConfig, standbyDisplay: 'backgroundImage', backgroundImageId: id }, true);
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

                <TitleLabel title={t.SETTINGS_SCREEN_CONTROL_STANDBY_DISPLAY_LABEL} />
                <VStack align="start" gap={1}>
                    <RadioCard.Root
                        size={"sm"}
                        value={standbyDisplay}
                        variant={"subtle"}
                        onValueChange={(d) => {
                            const v = (d as { value: 'none'|'backgroundImage'|'buttonLayout' }).value;
                            setStandbyDisplay(v);
                            const update: Partial<ScreenControlConfig> = { standbyDisplay: v };
                            if (v === 'backgroundImage') {
                                const targetId = backgroundImageId || userAsset?.id || SYSTEM_BG_ID;
                                setBackgroundImageId(targetId);
                                (update as Partial<ScreenControlConfig>).backgroundImageId = targetId;
                            }
                            void updateScreenControl({ ...nextConfig, ...update }, true);
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
                <Text fontSize="xs" color="gray.400" whiteSpace="pre-wrap" >{t.SETTINGS_SCREEN_CONTROL_BACKGROUND_IMAGE_LIMIT_TIP.replace('{seconds}', (Math.floor(gifMaxFrames/gifTargetFps)).toString())}</Text>
               

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
                    onValueChange={(d) => {
                        const v = (d as { value: string }).value as ScreenControlFeatureKey;
                        if (!v || v === firstFeatureKey) return;
                        setFirstFeatureKey(v);
                        const pageId = featureKeyToId[v];
                        void updateScreenControl({ ...nextConfig, currentPageId: pageId }, true);
                    }}
                >
                <Table.Root size="sm" colorPalette="green" interactive>
                    <Table.Body>
                        {orderedFeatureItems.map((item) => (
                            <Table.Row
                                key={item.key}
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
                                onDrop={(e) => {
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
                                    void updateScreenControl({ ...nextConfig, featuresOrder: nextOrder }, true);
                                }}
                                onDragEnd={() => {
                                    dragFeatureKeyRef.current = null;
                                    setDropIndicator(null);
                                }}
                            >
                                <Table.Cell py={3}>
                                    <HStack gap={2}>
                                        <Box color="gray.500" cursor={disabled ? "default" : "grab"}>
                                            <LuGripVertical />
                                        </Box>
                                        <Text>{item.label}</Text>
                                    </HStack>
                                </Table.Cell>
                                <Table.Cell py={3}>
                                    <HStack gap={2}>
                                        <RadioGroup.Item value={item.key} >
                                            <RadioGroup.ItemHiddenInput />
                                            <RadioGroup.ItemIndicator />
                                            {firstFeatureKey === item.key && <RadioGroup.ItemText fontSize="xs" color="gray.400" >{t.SETTINGS_SCREEN_CONTROL_FIRST_SCREEN_LABEL}</RadioGroup.ItemText>}
                                        </RadioGroup.Item>
                                    </HStack>
                                </Table.Cell>
                                <Table.Cell py={3} textAlign="end">
                                    <Switch
                                        checked={features[item.key]}
                                        disabled={disabled}
                                        onCheckedChange={(e: { checked: boolean }) => {
                                            const nf = { ...features, [item.key]: e.checked } as ScreenControlFeatures;
                                            setFeatures(nf);
                                            void updateScreenControl({ ...nextConfig, features: nf }, true);
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
