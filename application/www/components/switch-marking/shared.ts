import {
    SwitchMappingCatalogItem,
    SwitchMappingPayload,
} from "@/types/adc";
import { keyframes } from "@emotion/react";
import {
    CategoryScale,
    Chart as ChartJS,
    Legend,
    LinearScale,
    LineElement,
    PointElement,
    Title,
    Tooltip
} from "chart.js";

ChartJS.register(
    CategoryScale,
    LinearScale,
    PointElement,
    LineElement,
    Title,
    Tooltip,
    Legend,
);

export interface AxisListItem {
    mappingId: string;
    name: string;
    catalogId: string | null;
    hasImage: boolean;
    imageUpdatedAt: string | null;
    onDevice: boolean;
    serverItem: SwitchMappingCatalogItem | null;
}

export interface EditorState {
    mode: "create" | "edit";
    item: AxisListItem | null;
}

export interface CurveEditorState {
    item: AxisListItem;
    mapping: SwitchMappingPayload;
    originalLength: number;
}

export const CARD_WIDTH = "104px";

export const CARD_HEIGHT = "103px";

export const COVER_HEIGHT = "64px";

export const COVER_ASPECT_RATIO = "23 / 16";

export const COVER_OUTPUT_WIDTH = 736;

export const COVER_OUTPUT_HEIGHT = 512;

export const COVER_ZOOM_MIN = 1;

export const COVER_ZOOM_MAX = 4;

export interface Size {
    width: number;
    height: number;
}

export interface Point {
    x: number;
    y: number;
}

export const clamp = (value: number, minimum: number, maximum: number) =>
    Math.min(maximum, Math.max(minimum, value));

export const blobToDataUrl = (blob: Blob): Promise<string> => new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = () => {
        if (typeof reader.result === "string") {
            resolve(reader.result);
            return;
        }
        reject(new Error("Unable to read switch cover image"));
    };
    reader.onerror = () => reject(reader.error || new Error("Unable to read switch cover image"));
    reader.readAsDataURL(blob);
});

export const coverMetrics = (image: Size, viewport: Size, zoom: number) => {
    if (image.width <= 0 || image.height <= 0 ||
        viewport.width <= 0 || viewport.height <= 0) {
        return { width: 0, height: 0, scale: 1, maxX: 0, maxY: 0 };
    }
    const scale = Math.max(
        viewport.width / image.width,
        viewport.height / image.height,
    ) * zoom;
    const width = image.width * scale;
    const height = image.height * scale;
    return {
        width,
        height,
        scale,
        maxX: Math.max(0, (width - viewport.width) / 2),
        maxY: Math.max(0, (height - viewport.height) / 2),
    };
};

export const clampCoverOffset = (
    point: Point,
    image: Size,
    viewport: Size,
    zoom: number,
): Point => {
    const metrics = coverMetrics(image, viewport, zoom);
    return {
        x: clamp(point.x, -metrics.maxX, metrics.maxX),
        y: clamp(point.y, -metrics.maxY, metrics.maxY),
    };
};

export const downloadPulse = keyframes`
    0%, 100% { opacity: 0.45; }
    50% { opacity: 1; }
`;
