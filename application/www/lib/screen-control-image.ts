import { decompressFrames, parseGIF } from 'gifuct-js';
import { calculateImageCoverRect } from './image-cover';

export type ProcessedRGB565Image = { width: number; height: number; data: Uint8Array; previewUrl: string };
export type ProcessedRGB565Sequence = ProcessedRGB565Image & { frames: string[]; fps: number; frameCount: number };

export type RGB565ContentBounds = { x: number; y: number; width: number; height: number };

/**
 * Finds full rows/columns of exact RGB565 black around an image. Older gallery
 * assets were letterboxed before being converted to RGB565, so CSS cannot hide
 * those bars. The 75% guard avoids magnifying tiny or intentionally dark art.
 */
export const findRgb565ContentBounds = (
    rgb565le: Uint8Array,
    width: number,
    height: number,
): RGB565ContentBounds => {
    const full = { x: 0, y: 0, width, height };
    if (!Number.isInteger(width) || !Number.isInteger(height) || width <= 0 || height <= 0 ||
        rgb565le.byteLength !== width * height * 2) {
        return full;
    }

    const isBlack = (x: number, y: number) => {
        const offset = (y * width + x) * 2;
        return rgb565le[offset] === 0 && rgb565le[offset + 1] === 0;
    };
    const columnIsBlack = (x: number) => {
        for (let y = 0; y < height; y++) if (!isBlack(x, y)) return false;
        return true;
    };
    const rowIsBlack = (y: number) => {
        for (let x = 0; x < width; x++) if (!isBlack(x, y)) return false;
        return true;
    };

    let left = 0;
    let right = width - 1;
    let top = 0;
    let bottom = height - 1;
    while (left <= right && columnIsBlack(left)) left++;
    while (right >= left && columnIsBlack(right)) right--;
    while (top <= bottom && rowIsBlack(top)) top++;
    while (bottom >= top && rowIsBlack(bottom)) bottom--;

    const contentWidth = right - left + 1;
    const contentHeight = bottom - top + 1;
    if (contentWidth < width * 0.75 || contentHeight < height * 0.75) return full;
    return { x: left, y: top, width: contentWidth, height: contentHeight };
};

export const rgb565ToPngDataUrl = (rgb565le: Uint8Array, width: number, height: number) => {
    if (!Number.isInteger(width) || !Number.isInteger(height) || width <= 0 || height <= 0 ||
        width > 320 || height > 172 || rgb565le.byteLength !== width * height * 2) {
        throw new Error('RGB565 image payload does not match its dimensions');
    }
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
    const bounds = findRgb565ContentBounds(rgb565le, width, height);
    if (bounds.x === 0 && bounds.y === 0 && bounds.width === width && bounds.height === height) {
        return canvas.toDataURL('image/png');
    }

    const fitted = document.createElement('canvas');
    fitted.width = width;
    fitted.height = height;
    const fittedCtx = fitted.getContext('2d')!;
    fittedCtx.imageSmoothingEnabled = true;
    fittedCtx.imageSmoothingQuality = 'high';
    fittedCtx.drawImage(
        canvas,
        bounds.x,
        bounds.y,
        bounds.width,
        bounds.height,
        0,
        0,
        width,
        height,
    );
    return fitted.toDataURL('image/png');
};

const processSourceToRGB565 = (source: CanvasImageSource, sourceWidth: number, sourceHeight: number) => {
    const maxW = 320;
    const maxH = 172;
    const canvas = document.createElement('canvas');
    canvas.width = maxW;
    canvas.height = maxH;
    const ctx = canvas.getContext('2d', { willReadFrequently: true })!;
    const crop = calculateImageCoverRect(sourceWidth, sourceHeight, maxW, maxH);
    ctx.imageSmoothingEnabled = true;
    ctx.imageSmoothingQuality = 'high';
    ctx.drawImage(
        source,
        crop.sourceX,
        crop.sourceY,
        crop.sourceWidth,
        crop.sourceHeight,
        0,
        0,
        maxW,
        maxH,
    );
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

export const processImageToRGB565 = async (file: File): Promise<ProcessedRGB565Image> => {
    const bitmap = await createImageBitmap(file);
    return processSourceToRGB565(bitmap, bitmap.width, bitmap.height);
};

const selectFrames = (frameTimesUs: number[], totalUs: number, targetFps: number, maxFrames: number) => {
    const selected: number[] = [];
    const frameCount = frameTimesUs.length;
    if (frameCount <= 0 || totalUs <= 0) return [0];

    const targetCount = Math.min(
        maxFrames,
        frameCount,
        Math.max(1, Math.round((totalUs / 1_000_000) * targetFps)),
    );
    const used = new Set<number>();
    for (let n = 0; n < targetCount; n++) {
        const targetTime = targetCount <= 1
            ? 0
            : Math.floor((n * Math.max(0, totalUs - 1)) / (targetCount - 1));
        let best = -1;
        let bestDiff = Number.POSITIVE_INFINITY;
        for (let i = 0; i < frameCount; i++) {
            if (used.has(i)) continue;
            const diff = Math.abs(frameTimesUs[i] - targetTime);
            if (diff < bestDiff) {
                bestDiff = diff;
                best = i;
            }
        }
        if (best >= 0) {
            used.add(best);
            selected.push(best);
        }
    }
    return selected.length > 0 ? selected : [0];
};

const blendPatchInto = (dstRgba: Uint8ClampedArray, dstWidth: number, patch: Uint8ClampedArray, patchWidth: number, patchHeight: number, left: number, top: number) => {
    const maxX = Math.min(dstWidth, left + patchWidth);
    const maxY = Math.min((dstRgba.length / 4) / dstWidth, top + patchHeight);
    const startX = Math.max(0, left);
    const startY = Math.max(0, top);

    for (let y = startY; y < maxY; y++) {
        const py = y - top;
        for (let x = startX; x < maxX; x++) {
            const px = x - left;
            const pi = (py * patchWidth + px) * 4;
            const sa = patch[pi + 3];
            if (sa === 0) continue;
            const di = (y * dstWidth + x) * 4;
            if (sa === 255) {
                dstRgba[di] = patch[pi];
                dstRgba[di + 1] = patch[pi + 1];
                dstRgba[di + 2] = patch[pi + 2];
                dstRgba[di + 3] = 255;
                continue;
            }
            const invA = 255 - sa;
            dstRgba[di] = (patch[pi] * sa + dstRgba[di] * invA) / 255;
            dstRgba[di + 1] = (patch[pi + 1] * sa + dstRgba[di + 1] * invA) / 255;
            dstRgba[di + 2] = (patch[pi + 2] * sa + dstRgba[di + 2] * invA) / 255;
            dstRgba[di + 3] = 255;
        }
    }
};
const ensureLastFrameIncluded = (selected: number[], frameCount: number, maxFrames: number) => {
    const uniq = Array.from(new Set(selected.filter((i) => i >= 0 && i < frameCount)));
    uniq.sort((a, b) => a - b);
    if (frameCount <= 0) return [0];
    const last = frameCount - 1;
    if (uniq[uniq.length - 1] !== last) {
        if (uniq.length >= maxFrames) {
            uniq[uniq.length - 1] = last;
        } else {
            uniq.push(last);
        }
        uniq.sort((a, b) => a - b);
    }
    return uniq.length > 0 ? uniq : [0];
};

export const selectGifFrameIndices = (
    frameTimesUs: number[],
    totalUs: number,
    targetFpsInput = 3,
    maxFramesInput = 6,
) => {
    const targetFps = Math.max(1, Math.min(5, Math.floor(targetFpsInput)));
    const maxFrames = Math.max(1, Math.min(6, Math.floor(maxFramesInput)));
    return ensureLastFrameIncluded(
        selectFrames(frameTimesUs, totalUs, targetFps, maxFrames),
        frameTimesUs.length,
        maxFrames,
    );
};


export const processGifToRGB565Sequence = async (file: File, targetFpsInput: number, maxFramesInput: number): Promise<ProcessedRGB565Sequence> => {
    const targetFps = Math.max(1, Math.min(5, Math.floor(targetFpsInput)));
    const maxFrames = Math.max(1, Math.min(6, Math.floor(maxFramesInput)));
    const buf = await file.arrayBuffer();

    const gif = parseGIF(new Uint8Array(buf));
    const lsd = (gif as unknown as { lsd?: { width?: number; height?: number } }).lsd;
    const frames = decompressFrames(gif, true);
    const inputFrameCount = frames.length;
    let inputWidth = (lsd?.width ?? 0) as number;
    let inputHeight = (lsd?.height ?? 0) as number;
    if (!inputWidth || !inputHeight) {
        let w = 0;
        let h = 0;
        for (const f of frames) {
            const d = f?.dims;
            if (!d) continue;
            w = Math.max(w, (d.left ?? 0) + (d.width ?? 0));
            h = Math.max(h, (d.top ?? 0) + (d.height ?? 0));
        }
        inputWidth = w;
        inputHeight = h;
    }

    if (!inputWidth || !inputHeight || inputFrameCount <= 1) {
        const single = await processImageToRGB565(file);
        return { ...single, frames: [single.previewUrl], fps: targetFps, frameCount: 1 };
    }

    let totalUs = 0;
    const frameTimesUs: number[] = [];
    for (let i = 0; i < inputFrameCount; i++) {
        frameTimesUs[i] = totalUs;
        const frame = frames[i];
        const delayCs = typeof frame?.delay === 'number' && frame.delay > 0 ? frame.delay : 10;
        totalUs += delayCs * 10_000;
    }
    if (totalUs <= 0) totalUs = inputFrameCount * 200_000;

    const selected = selectGifFrameIndices(frameTimesUs, totalUs, targetFps, maxFrames);
    const selectedSet = new Set<number>(selected);

    const srcCanvas = document.createElement('canvas');
    srcCanvas.width = inputWidth;
    srcCanvas.height = inputHeight;
    const srcCtx = srcCanvas.getContext('2d', { willReadFrequently: true })!;

    const framesData: Uint8Array[] = [];
    const previewUrls: string[] = [];

    const canvasRgba = new Uint8ClampedArray(inputWidth * inputHeight * 4);

    for (let i = 0; i < inputFrameCount; i++) {
        const f = frames[i];
        const patch = f?.patch;
        const dims = f?.dims;
        const disposalType = (f as unknown as { disposalType?: number }).disposalType ?? 0;
        let restoreRgba: Uint8ClampedArray | null = null;

        if (disposalType === 3) {
            restoreRgba = new Uint8ClampedArray(canvasRgba);
        }

        if (patch instanceof Uint8ClampedArray) {
            const w = (dims?.width ?? inputWidth) as number;
            const h = (dims?.height ?? inputHeight) as number;
            const left = (dims?.left ?? 0) as number;
            const top = (dims?.top ?? 0) as number;

            if (w > 0 && h > 0 && patch.length === w * h * 4) {
                blendPatchInto(canvasRgba, inputWidth, patch, w, h, left, top);
            }
        }

        if (selectedSet.has(i)) {
            const imageData = new ImageData(new Uint8ClampedArray(canvasRgba), inputWidth, inputHeight);
            srcCtx.putImageData(imageData, 0, 0);
            const processed = processSourceToRGB565(srcCanvas, inputWidth, inputHeight);
            framesData.push(processed.data);
            previewUrls.push(processed.previewUrl);
        }

        if (disposalType === 2 && dims) {
            const left = dims.left ?? 0;
            const top = dims.top ?? 0;
            const w = dims.width ?? 0;
            const h = dims.height ?? 0;
            if (w > 0 && h > 0) {
                for (let y = Math.max(0, top); y < Math.min(inputHeight, top + h); y++) {
                    for (let x = Math.max(0, left); x < Math.min(inputWidth, left + w); x++) {
                        const di = (y * inputWidth + x) * 4;
                        canvasRgba[di] = 0;
                        canvasRgba[di + 1] = 0;
                        canvasRgba[di + 2] = 0;
                        canvasRgba[di + 3] = 0;
                    }
                }
            }
        } else if (disposalType === 3 && restoreRgba) {
            canvasRgba.set(restoreRgba);
        }
    }

    if (framesData.length <= 0) {
        const single = await processImageToRGB565(file);
        return { ...single, frames: [single.previewUrl], fps: targetFps, frameCount: 1 };
    }

    const frameSize = framesData[0]?.length ?? 320 * 172 * 2;
    const combined = new Uint8Array(frameSize * framesData.length);
    framesData.forEach((f, i) => {
        combined.set(f, i * frameSize);
    });

    return { width: 320, height: 172, data: combined, previewUrl: previewUrls[0], frames: previewUrls, fps: targetFps, frameCount: framesData.length };
};
