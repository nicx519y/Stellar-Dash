import {
    Badge,
    Box,
    Button,
    Dialog,
    Flex,
    HStack,
    IconButton,
    Image,
    Input,
    Portal,
    Spinner,
    Table,
    Text,
    VStack,
} from "@chakra-ui/react";
import { keyframes } from "@emotion/react";
import { Line } from "react-chartjs-2";
import {
    CategoryScale,
    Chart as ChartJS,
    ChartData,
    ChartOptions,
    Legend,
    LinearScale,
    LineElement,
    PointElement,
    Title,
    Tooltip,
} from "chart.js";
import {
    ChangeEvent,
    PointerEvent as ReactPointerEvent,
    WheelEvent as ReactWheelEvent,
    useEffect,
    useMemo,
    useRef,
    useState,
} from "react";
import {
    LuDownload,
    LuImagePlus,
    LuMinus,
    LuPencil,
    LuPlus,
    LuRotateCcw,
    LuTrash2,
} from "react-icons/lu";

import { openConfirm } from "./dialog-confirm";
import { useColorMode } from "./ui/color-mode";
import { Tooltip as UiTooltip } from "./ui/tooltip";
import { showToast } from "./ui/toaster";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import { useLanguage } from "@/contexts/language-context";
import { useUserAuth } from "@/contexts/user-auth-context";
import {
    SWITCH_MARKING_LENGTH_MAX,
    SWITCH_MARKING_LENGTH_MIN,
    SWITCH_MARKING_STEP_MAX,
    SWITCH_MARKING_STEP_MIN,
} from "@/types/gamepad-config";
import {
    SwitchMappingCatalogItem,
    SwitchMappingPayload,
} from "@/types/adc";

ChartJS.register(
    CategoryScale,
    LinearScale,
    PointElement,
    LineElement,
    Title,
    Tooltip,
    Legend,
);

interface AxisListItem {
    mappingId: string;
    name: string;
    catalogId: string | null;
    hasImage: boolean;
    imageUpdatedAt: string | null;
    onDevice: boolean;
    serverItem: SwitchMappingCatalogItem | null;
}

interface EditorState {
    mode: "create" | "edit";
    item: AxisListItem | null;
}

interface CurveEditorState {
    item: AxisListItem;
    mapping: SwitchMappingPayload;
    originalLength: number;
}

const CARD_WIDTH = "104px";
const CARD_HEIGHT = "103px";
const COVER_HEIGHT = "64px";
// The card content is 92px wide after its border and padding. Keep the upload
// preview identical to the final 92x64 cover crop.
const COVER_ASPECT_RATIO = "23 / 16";
const COVER_OUTPUT_WIDTH = 736;
const COVER_OUTPUT_HEIGHT = 512;
const COVER_ZOOM_MIN = 1;
const COVER_ZOOM_MAX = 4;

interface Size {
    width: number;
    height: number;
}

interface Point {
    x: number;
    y: number;
}

const clamp = (value: number, minimum: number, maximum: number) =>
    Math.min(maximum, Math.max(minimum, value));

const blobToDataUrl = (blob: Blob): Promise<string> => new Promise((resolve, reject) => {
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

const coverMetrics = (image: Size, viewport: Size, zoom: number) => {
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

const clampCoverOffset = (
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
const downloadPulse = keyframes`
    0%, 100% { opacity: 0.45; }
    50% { opacity: 1; }
`;

export function SwitchMarkingContent() {
    const { colorMode } = useColorMode();
    const { t } = useLanguage();
    const { session } = useUserAuth();
    const isAdmin = session.authenticated && session.user?.role === "admin";
    const {
        deviceConnected,
        dataIsReady,
        mappingList,
        defaultMappingId,
        activeMapping,
        mappingStorageMode,
        mappingSource,
        markingStatus,
        fetchMappingList,
        fetchActiveMapping,
        fetchMarkingStatus,
        fetchSwitchMappingCatalog,
        fetchSwitchMappingDetail,
        fetchSwitchMappingImage,
        uploadSwitchMappingImage,
        updateSwitchMappingMetadata,
        updateSwitchMappingCurve,
        deleteSwitchMapping,
        installSwitchMapping,
        clearInstalledSwitchMapping,
        createSwitchMappingFromCurrent,
        startMarking,
        stopMarking,
        stepMarking,
        syncMarkingProgress,
    } = useGamepadConfig();

    const [initialized, setInitialized] = useState(false);
    const [catalog, setCatalog] = useState<SwitchMappingCatalogItem[]>([]);
    const [catalogImages, setCatalogImages] = useState<Record<string, string>>({});
    const [busyId, setBusyId] = useState<string | null>(null);
    const [recordingBusy, setRecordingBusy] = useState(false);
    const [serverSyncBusy, setServerSyncBusy] = useState(false);
    const [selectedMappingId, setSelectedMappingId] = useState<string | null>(null);
    const [selectedMapping, setSelectedMapping] = useState<SwitchMappingPayload | null>(null);
    const [hoveredMappingId, setHoveredMappingId] = useState<string | null>(null);
    const [editor, setEditor] = useState<EditorState | null>(null);
    const [curveEditor, setCurveEditor] = useState<CurveEditorState | null>(null);
    const [curveEditorLength, setCurveEditorLength] = useState(0);
    const [curveEditorValues, setCurveEditorValues] = useState<number[]>([]);
    const [editorName, setEditorName] = useState("");
    const [editorLength, setEditorLength] = useState(29);
    const [editorStep, setEditorStep] = useState(0.1);
    const [editorImage, setEditorImage] = useState<File | null>(null);
    const [editorImagePreview, setEditorImagePreview] = useState<string | null>(null);
    const [editorImageSize, setEditorImageSize] = useState<Size>({ width: 0, height: 0 });
    const [editorCropSize, setEditorCropSize] = useState<Size>({ width: 368, height: 256 });
    const [editorZoom, setEditorZoom] = useState(1);
    const [editorOffset, setEditorOffset] = useState<Point>({ x: 0, y: 0 });
    const [editorDragging, setEditorDragging] = useState(false);
    const [editorCoverHovered, setEditorCoverHovered] = useState(false);
    const editorPreviewGenerationRef = useRef(0);
    const editorCropRef = useRef<HTMLDivElement | null>(null);
    const editorImageInputRef = useRef<HTMLInputElement | null>(null);
    const editorDragRef = useRef<{
        pointerId: number;
        startClient: Point;
        startOffset: Point;
    } | null>(null);

    const initializationRunningRef = useRef(false);
    const initializationGenerationRef = useRef(0);
    const fetchMappingListRef = useRef(fetchMappingList);
    const fetchActiveMappingRef = useRef(fetchActiveMapping);
    const fetchMarkingStatusRef = useRef(fetchMarkingStatus);
    const fetchCatalogRef = useRef(fetchSwitchMappingCatalog);
    const fetchImageRef = useRef(fetchSwitchMappingImage);
    fetchMappingListRef.current = fetchMappingList;
    fetchActiveMappingRef.current = fetchActiveMapping;
    fetchMarkingStatusRef.current = fetchMarkingStatus;
    fetchCatalogRef.current = fetchSwitchMappingCatalog;
    fetchImageRef.current = fetchSwitchMappingImage;

    const catalogAdminModeRef = useRef(isAdmin);
    useEffect(() => {
        if (catalogAdminModeRef.current === isAdmin) return;
        catalogAdminModeRef.current = isAdmin;
        initializationGenerationRef.current += 1;
        initializationRunningRef.current = false;
        setInitialized(false);
    }, [isAdmin]);

    const axisItems = useMemo<AxisListItem[]>(() => {
        const items = new Map<string, AxisListItem>();
        catalog.forEach(item => {
            items.set(item.revisionId, {
                mappingId: item.revisionId,
                name: item.displayName,
                catalogId: item.catalogId,
                hasImage: item.hasImage,
                imageUpdatedAt: item.imageUpdatedAt,
                onDevice: false,
                serverItem: item,
            });
        });
        mappingList
            .filter(item => item.id && item.name)
            .forEach(deviceMapping => {
                const server = items.get(deviceMapping.id);
                items.set(deviceMapping.id, {
                    mappingId: deviceMapping.id,
                    name: server?.name || deviceMapping.name,
                    catalogId: server?.catalogId || null,
                    hasImage: server?.hasImage || false,
                    imageUpdatedAt: server?.imageUpdatedAt || null,
                    onDevice: true,
                    serverItem: server?.serverItem || null,
                });
            });
        return [...items.values()];
    }, [catalog, mappingList]);

    useEffect(() => {
        if (!deviceConnected) {
            initializationGenerationRef.current += 1;
            initializationRunningRef.current = false;
            setInitialized(false);
            setCatalog([]);
            setSelectedMappingId(null);
            setSelectedMapping(null);
            return;
        }
        if (!dataIsReady || initialized || initializationRunningRef.current) return;

        initializationRunningRef.current = true;
        const generation = ++initializationGenerationRef.current;
        void Promise.allSettled([
            fetchMappingListRef.current(),
            fetchCatalogRef.current(false),
        ]).then(results => {
            if (generation !== initializationGenerationRef.current) return;
            const catalogResult = results[1];
            if (catalogResult.status === "fulfilled") {
                setCatalog(catalogResult.value);
            } else {
                setCatalog([]);
                showToast({
                    title: t.SWITCH_MAPPING_CATALOG_LOAD_FAILED,
                    description: catalogResult.reason instanceof Error
                        ? catalogResult.reason.message
                        : String(catalogResult.reason),
                    type: "error",
                });
            }
            setInitialized(true);
        }).finally(() => {
            if (generation === initializationGenerationRef.current) {
                initializationRunningRef.current = false;
            }
        });
    }, [deviceConnected, dataIsReady, initialized, isAdmin, t.SWITCH_MAPPING_CATALOG_LOAD_FAILED]);

    useEffect(() => {
        if (!defaultMappingId || activeMapping?.id === defaultMappingId) return;
        if (!mappingList.some(mapping => mapping.id === defaultMappingId)) return;
        void fetchActiveMappingRef.current(defaultMappingId).catch(error => {
            showToast({
                title: t.SWITCH_MAPPING_DEVICE_READ_FAILED,
                description: error instanceof Error ? error.message : String(error),
                type: "error",
            });
        });
    }, [defaultMappingId, mappingList, activeMapping?.id, t.SWITCH_MAPPING_DEVICE_READ_FAILED]);

    useEffect(() => {
        if (!isAdmin || !deviceConnected || !dataIsReady) return;
        void fetchMarkingStatusRef.current().catch(() => undefined);
    }, [isAdmin, deviceConnected, dataIsReady]);

    useEffect(() => {
        if (!activeMapping?.id) return;
        if (!selectedMappingId || selectedMappingId === defaultMappingId ||
            selectedMappingId === activeMapping.id) {
            setSelectedMappingId(activeMapping.id);
            setSelectedMapping(activeMapping);
        }
    }, [activeMapping, defaultMappingId, selectedMappingId]);

    useEffect(() => {
        let cancelled = false;
        void Promise.all(catalog.filter(item => item.hasImage).map(async item => {
            try {
                const image = await fetchImageRef.current(item.catalogId, false);
                return image
                    ? { catalogId: item.catalogId, image: await blobToDataUrl(image) }
                    : null;
            } catch {
                return null;
            }
        })).then(images => {
            if (cancelled) return;
            const next: Record<string, string> = {};
            images.forEach(image => {
                if (!image) return;
                next[image.catalogId] = image.image;
            });
            setCatalogImages(next);
        });
        return () => {
            cancelled = true;
        };
    }, [catalog]);

    useEffect(() => {
        if (!editor) return;
        const crop = editorCropRef.current;
        if (!crop) return;
        const updateSize = () => {
            const next = {
                width: crop.clientWidth,
                height: crop.clientHeight,
            };
            if (next.width <= 0 || next.height <= 0) return;
            setEditorCropSize(next);
            setEditorOffset(current => clampCoverOffset(
                current, editorImageSize, next, editorZoom,
            ));
        };
        updateSize();
        const observer = new ResizeObserver(updateSize);
        observer.observe(crop);
        return () => observer.disconnect();
    }, [editor, editorImageSize, editorZoom]);

    const editorCoverMetrics = useMemo(
        () => coverMetrics(editorImageSize, editorCropSize, editorZoom),
        [editorCropSize, editorImageSize, editorZoom],
    );

    const refreshCatalog = async () => {
        const items = await fetchCatalogRef.current(false);
        setCatalog(items);
        return items;
    };

    const selectAxis = async (item: AxisListItem) => {
        if (busyId || recordingBusy || serverSyncBusy) return;
        if (markingStatus.is_marking && item.mappingId !== defaultMappingId) return;
        setSelectedMappingId(item.mappingId);
        if (item.mappingId === defaultMappingId) {
            if (activeMapping?.id === item.mappingId) setSelectedMapping(activeMapping);
            return;
        }
        if (!item.catalogId || !item.serverItem) return;
        setBusyId(item.catalogId);
        try {
            const detail = await fetchSwitchMappingDetail(item.catalogId);
            setSelectedMapping(detail.revision.mapping);
            if (mappingStorageMode !== "shared-singleton") {
                showToast({
                    title: t.SWITCH_MAPPING_FIRMWARE_UPGRADE_TITLE,
                    description: t.SWITCH_MAPPING_FIRMWARE_UPGRADE_MESSAGE,
                    type: "error",
                });
                return;
            }
            const installed = await installSwitchMapping(item.catalogId, detail);
            setSelectedMappingId(installed.id);
            setSelectedMapping(installed);
            showToast({
                title: t.SWITCH_MAPPING_INSTALL_SUCCESS.replace("{name}", item.name),
                description: t.SWITCH_MAPPING_INSTALL_SUCCESS_DETAIL,
                type: "success",
            });
        } catch (error) {
            showToast({
                title: t.SWITCH_MAPPING_INSTALL_FAILED,
                description: error instanceof Error ? error.message : String(error),
                type: "error",
            });
        } finally {
            setBusyId(null);
        }
    };

    const resetEditorCrop = () => {
        setEditorImageSize({ width: 0, height: 0 });
        setEditorZoom(1);
        setEditorOffset({ x: 0, y: 0 });
        setEditorDragging(false);
        setEditorCoverHovered(false);
        editorDragRef.current = null;
    };

    const openEditor = (mode: "create" | "edit", item: AxisListItem | null) => {
        editorPreviewGenerationRef.current += 1;
        setEditor({ mode, item });
        setEditorName(mode === "edit" ? item?.name || "" : "");
        setEditorLength(29);
        setEditorStep(0.1);
        setEditorImage(null);
        setEditorImagePreview(
            mode === "edit" && item?.catalogId
                ? catalogImages[item.catalogId] || null
                : null,
        );
        resetEditorCrop();
    };

    useEffect(() => {
        if (editor?.mode !== "edit" || editorImage || !editor.item?.catalogId) return;
        setEditorImagePreview(catalogImages[editor.item.catalogId] || null);
    }, [catalogImages, editor, editorImage]);

    const closeEditor = () => {
        if (busyId) return;
        editorPreviewGenerationRef.current += 1;
        setEditor(null);
        setEditorName("");
        setEditorLength(29);
        setEditorStep(0.1);
        setEditorImage(null);
        setEditorImagePreview(null);
        resetEditorCrop();
    };

    const selectEditorImage = (event: ChangeEvent<HTMLInputElement>) => {
        const file = event.target.files?.[0] || null;
        event.target.value = "";
        if (!file) return;
        if (!["image/jpeg", "image/png", "image/webp"].includes(file.type) ||
            file.size < 12 || file.size > 2 * 1024 * 1024) {
            showToast({
                title: t.SWITCH_MAPPING_IMAGE_INVALID_TITLE,
                description: t.SWITCH_MAPPING_IMAGE_INVALID_MESSAGE,
                type: "error",
            });
            return;
        }
        const previewGeneration = editorPreviewGenerationRef.current + 1;
        editorPreviewGenerationRef.current = previewGeneration;
        setEditorImage(file);
        setEditorImagePreview(null);
        resetEditorCrop();

        // A data URL is intentionally used here. Blob URLs were revoked by
        // React StrictMode's development-only effect cleanup before Chakra's
        // Image had loaded them, leaving only the alt text in the crop frame.
        const reader = new FileReader();
        reader.onload = () => {
            if (editorPreviewGenerationRef.current !== previewGeneration) return;
            const result = reader.result;
            setEditorImagePreview(typeof result === "string" ? result : null);
        };
        reader.onerror = () => {
            if (editorPreviewGenerationRef.current !== previewGeneration) return;
            setEditorImage(null);
            setEditorImagePreview(null);
            showToast({
                title: t.SWITCH_MAPPING_IMAGE_INVALID_TITLE,
                description: t.SWITCH_MAPPING_IMAGE_INVALID_MESSAGE,
                type: "error",
            });
        };
        reader.readAsDataURL(file);
    };

    const changeEditorZoom = (value: number) => {
        const nextZoom = clamp(value, COVER_ZOOM_MIN, COVER_ZOOM_MAX);
        setEditorZoom(nextZoom);
        setEditorOffset(current => clampCoverOffset(
            current, editorImageSize, editorCropSize, nextZoom,
        ));
    };

    const resetEditorCropTransform = () => {
        setEditorZoom(1);
        setEditorOffset({ x: 0, y: 0 });
    };

    const beginEditorCropDrag = (event: ReactPointerEvent<HTMLDivElement>) => {
        if (!editorImage || !editorImagePreview || event.button !== 0) return;
        event.currentTarget.setPointerCapture(event.pointerId);
        editorDragRef.current = {
            pointerId: event.pointerId,
            startClient: { x: event.clientX, y: event.clientY },
            startOffset: editorOffset,
        };
        setEditorDragging(true);
    };

    const moveEditorCrop = (event: ReactPointerEvent<HTMLDivElement>) => {
        const drag = editorDragRef.current;
        if (!drag || drag.pointerId !== event.pointerId) return;
        setEditorOffset(clampCoverOffset({
            x: drag.startOffset.x + event.clientX - drag.startClient.x,
            y: drag.startOffset.y + event.clientY - drag.startClient.y,
        }, editorImageSize, editorCropSize, editorZoom));
    };

    const endEditorCropDrag = (event: ReactPointerEvent<HTMLDivElement>) => {
        const drag = editorDragRef.current;
        if (!drag || drag.pointerId !== event.pointerId) return;
        if (event.currentTarget.hasPointerCapture(event.pointerId)) {
            event.currentTarget.releasePointerCapture(event.pointerId);
        }
        editorDragRef.current = null;
        setEditorDragging(false);
    };

    const zoomEditorCropWithWheel = (event: ReactWheelEvent<HTMLDivElement>) => {
        if (!editorImage || !editorImagePreview) return;
        event.preventDefault();
        changeEditorZoom(editorZoom + (event.deltaY < 0 ? 0.12 : -0.12));
    };

    const createCroppedCoverFile = async (): Promise<File | null> => {
        if (!editorImage || !editorImagePreview) return null;
        const image = document.createElement("img");
        image.src = editorImagePreview;
        await image.decode();

        const viewport = editorCropRef.current
            ? {
                width: editorCropRef.current.clientWidth,
                height: editorCropRef.current.clientHeight,
            }
            : editorCropSize;
        const natural = { width: image.naturalWidth, height: image.naturalHeight };
        const metrics = coverMetrics(natural, viewport, editorZoom);
        const sourceWidth = viewport.width / metrics.scale;
        const sourceHeight = viewport.height / metrics.scale;
        const sourceX = clamp(
            ((metrics.width - viewport.width) / 2 - editorOffset.x) / metrics.scale,
            0,
            Math.max(0, natural.width - sourceWidth),
        );
        const sourceY = clamp(
            ((metrics.height - viewport.height) / 2 - editorOffset.y) / metrics.scale,
            0,
            Math.max(0, natural.height - sourceHeight),
        );

        const canvas = document.createElement("canvas");
        canvas.width = COVER_OUTPUT_WIDTH;
        canvas.height = COVER_OUTPUT_HEIGHT;
        const context = canvas.getContext("2d");
        if (!context) throw new Error("Canvas is not available");
        context.imageSmoothingEnabled = true;
        context.imageSmoothingQuality = "high";
        context.drawImage(
            image,
            sourceX,
            sourceY,
            sourceWidth,
            sourceHeight,
            0,
            0,
            COVER_OUTPUT_WIDTH,
            COVER_OUTPUT_HEIGHT,
        );
        const blob = await new Promise<Blob | null>(resolve =>
            canvas.toBlob(resolve, "image/webp", 0.9)
        );
        if (!blob) throw new Error("Unable to crop cover image");
        const baseName = editorImage.name.replace(/\.[^.]+$/, "") || "switch-cover";
        return new File([blob], `${baseName}.webp`, { type: "image/webp" });
    };

    const openEditorImagePicker = () => {
        if (busyId || editorImage) return;
        editorImageInputRef.current?.click();
    };

    const saveEditor = async () => {
        if (!editor) return;
        const name = editorName.trim();
        if (!name || name.length > 80) {
            showToast({
                title: t.SWITCH_MAPPING_NAME_INVALID_TITLE,
                description: t.SWITCH_MAPPING_NAME_INVALID_MESSAGE,
                type: "error",
            });
            return;
        }
        const duplicateName = catalog.some(item =>
            item.catalogId !== editor.item?.catalogId &&
            item.displayName.trim().toLocaleLowerCase() === name.toLocaleLowerCase()
        );
        if (duplicateName) {
            showToast({
                title: t.SWITCH_MAPPING_NAME_DUPLICATE_TITLE,
                description: t.SWITCH_MAPPING_NAME_DUPLICATE_MESSAGE,
                type: "error",
            });
            return;
        }
        if (editor.mode === "create" &&
            (!Number.isInteger(editorLength) ||
                editorLength < SWITCH_MARKING_LENGTH_MIN ||
                editorLength > SWITCH_MARKING_LENGTH_MAX ||
                !Number.isFinite(editorStep) ||
                editorStep < SWITCH_MARKING_STEP_MIN ||
                editorStep > SWITCH_MARKING_STEP_MAX)) {
            showToast({
                title: t.SWITCH_MAPPING_PARAMETERS_INVALID_TITLE,
                description: t.SWITCH_MAPPING_PARAMETERS_INVALID_MESSAGE,
                type: "error",
            });
            return;
        }
        const operationId = editor.mode === "create"
            ? "create"
            : editor.item?.catalogId || "edit";
        setBusyId(operationId);
        try {
            const croppedImage = await createCroppedCoverFile();
            if (editor.mode === "create") {
                await createSwitchMappingFromCurrent({
                    displayName: name,
                    description: "",
                    length: editorLength,
                    step: editorStep,
                    image: croppedImage,
                });
            } else {
                const item = editor.item;
                if (!item?.catalogId || !item.serverItem) {
                    throw new Error(t.SWITCH_MAPPING_NOT_IN_CATALOG);
                }
                await updateSwitchMappingMetadata(
                    item.catalogId,
                    name,
                    item.serverItem.description,
                );
                if (croppedImage) {
                    await uploadSwitchMappingImage(item.catalogId, croppedImage);
                }
            }
            await refreshCatalog();
            setEditor(null);
            setEditorName("");
            setEditorLength(29);
            setEditorStep(0.1);
            setEditorImage(null);
            setEditorImagePreview(null);
            resetEditorCrop();
            showToast({
                title: editor.mode === "create"
                    ? t.SWITCH_MAPPING_CREATE_SUCCESS
                    : t.SWITCH_MAPPING_EDIT_SUCCESS,
                type: "success",
            });
        } catch (error) {
            showToast({
                title: editor.mode === "create"
                    ? t.SWITCH_MAPPING_CREATE_FAILED
                    : t.SWITCH_MAPPING_EDIT_FAILED,
                description: error instanceof Error ? error.message : String(error),
                type: "error",
            });
        } finally {
            setBusyId(null);
        }
    };

    const deleteAxis = async (item: AxisListItem) => {
        if (!item.catalogId || busyId) return;
        const active = item.mappingId === defaultMappingId;
        const confirmed = await openConfirm({
            title: t.SWITCH_MAPPING_DELETE_TITLE.replace("{name}", item.name),
            message: active && mappingSource === "server-installed"
                ? t.SWITCH_MAPPING_DELETE_ACTIVE_MESSAGE
                : t.SWITCH_MAPPING_DELETE_MESSAGE,
        });
        if (!confirmed) return;

        setBusyId(item.catalogId);
        let deviceCleared = false;
        try {
            if (active && mappingSource === "server-installed") {
                await clearInstalledSwitchMapping(item.mappingId);
                deviceCleared = true;
            }
            await deleteSwitchMapping(item.catalogId);
            await refreshCatalog();
            showToast({
                title: t.SWITCH_MAPPING_DELETE_SUCCESS.replace("{name}", item.name),
                description: deviceCleared ? t.SWITCH_MAPPING_DELETE_DEVICE_FALLBACK : undefined,
                type: "success",
            });
        } catch (error) {
            showToast({
                title: t.SWITCH_MAPPING_DELETE_FAILED,
                description: deviceCleared
                    ? t.SWITCH_MAPPING_DELETE_PARTIAL.replace(
                        "{error}",
                        error instanceof Error ? error.message : String(error),
                    )
                    : error instanceof Error ? error.message : String(error),
                type: "error",
            });
        } finally {
            setBusyId(null);
        }
    };

    const runRecordingAction = async (
        action: () => Promise<void>,
        allowDuringSync: boolean = false,
    ) => {
        if (recordingBusy || (!allowDuringSync && serverSyncBusy) || busyId) return;
        setRecordingBusy(true);
        try {
            await action();
        } catch (error) {
            showToast({
                title: t.SWITCH_MAPPING_RECORDING_FAILED,
                description: error instanceof Error ? error.message : String(error),
                type: "error",
            });
        } finally {
            setRecordingBusy(false);
        }
    };

    const selectedIsInstalled = Boolean(
        selectedMappingId && selectedMappingId === defaultMappingId,
    );

    const lastServerSyncRef = useRef("");
    useEffect(() => {
        if (!isAdmin || !selectedIsInstalled || markingStatus.is_sampling ||
            markingStatus.index < 0 || markingStatus.id !== selectedMappingId) {
            return;
        }
        const selectedItem = axisItems.find(item => item.mappingId === selectedMappingId);
        if (!selectedItem?.catalogId) return;

        const mapping: SwitchMappingPayload = {
            id: markingStatus.id,
            name: markingStatus.mapping_name,
            length: markingStatus.length,
            step: markingStatus.step,
            samplingNoise: markingStatus.sampling_noise,
            samplingFrequency: markingStatus.sampling_frequency,
            originalValues: [...markingStatus.values],
        };
        const syncKey = `${selectedItem.catalogId}:${mapping.id}:${markingStatus.index}:` +
            `${mapping.samplingNoise}:${mapping.samplingFrequency}:${mapping.originalValues.join(",")}`;
        if (lastServerSyncRef.current === syncKey) return;
        lastServerSyncRef.current = syncKey;

        setServerSyncBusy(true);
        void (async () => {
            let lastError: unknown = null;
            for (let attempt = 0; attempt < 3; attempt += 1) {
                try {
                    const [, detail] = await Promise.all([
                        syncMarkingProgress(),
                        updateSwitchMappingCurve(
                            selectedItem.catalogId as string,
                            mapping,
                        ),
                    ]);
                    setSelectedMapping(detail.revision.mapping);
                    setCatalog(current => current.map(item =>
                        item.catalogId === detail.catalogId
                            ? {
                                ...item,
                                sha256: detail.revision.sha256,
                                updatedAt: detail.updatedAt,
                            }
                            : item
                    ));
                    return;
                } catch (error) {
                    lastError = error;
                    if (attempt < 2) {
                        await new Promise(resolve => window.setTimeout(resolve, 150 * (attempt + 1)));
                    }
                }
            }
            lastServerSyncRef.current = "";
            showToast({
                title: t.SWITCH_MAPPING_RECORDING_FAILED,
                description: lastError instanceof Error ? lastError.message : String(lastError),
                type: "error",
            });
            await stopMarking().catch(() => undefined);
        })().finally(() => {
            setServerSyncBusy(false);
        });
    }, [
        axisItems,
        isAdmin,
        markingStatus,
        selectedIsInstalled,
        selectedMappingId,
        stopMarking,
        syncMarkingProgress,
        t.SWITCH_MAPPING_RECORDING_FAILED,
        updateSwitchMappingCurve,
    ]);

    const displayedMapping = useMemo<SwitchMappingPayload | null>(() => {
        const recordingVisible = markingStatus.id === selectedMappingId &&
            markingStatus.length >= 2 &&
            (markingStatus.is_marking || markingStatus.is_sampling || markingStatus.is_completed);
        if (recordingVisible) {
            return {
                id: markingStatus.id,
                name: markingStatus.mapping_name,
                length: markingStatus.length,
                step: markingStatus.step,
                samplingNoise: markingStatus.sampling_noise,
                samplingFrequency: markingStatus.sampling_frequency,
                originalValues: [...markingStatus.values],
            };
        }
        return selectedMapping || activeMapping;
    }, [markingStatus, selectedMapping, selectedMappingId, activeMapping]);

    const selectedAxisItem = useMemo(
        () => axisItems.find(item => item.mappingId === selectedMappingId) || null,
        [axisItems, selectedMappingId],
    );

    const openCurveEditor = () => {
        if (!isAdmin || !selectedAxisItem?.catalogId || !selectedAxisItem.serverItem ||
            !displayedMapping || displayedMapping.id !== selectedAxisItem.mappingId ||
            busyId || recordingBusy || serverSyncBusy || markingStatus.is_marking) {
            return;
        }
        setCurveEditor({
            item: selectedAxisItem,
            mapping: displayedMapping,
            originalLength: displayedMapping.length,
        });
        setCurveEditorLength(displayedMapping.length);
        setCurveEditorValues([...displayedMapping.originalValues]);
    };

    const closeCurveEditor = () => {
        if (busyId) return;
        setCurveEditor(null);
        setCurveEditorLength(0);
        setCurveEditorValues([]);
    };

    const changeCurveEditorLength = (value: number) => {
        setCurveEditorLength(value);
        if (!Number.isInteger(value) || value < 0 || value > SWITCH_MARKING_LENGTH_MAX) {
            return;
        }
        setCurveEditorValues(current => Array.from(
            { length: Math.max(current.length, value) },
            (_, index) => current[index] ?? 0,
        ));
    };

    const changeCurveEditorValue = (index: number, value: number) => {
        setCurveEditorValues(current => {
            const next = [...current];
            next[index] = value;
            return next;
        });
    };

    const curveEditorColumnCount = useMemo(() => {
        if (!curveEditor) return 0;
        const requestedLength = Number.isInteger(curveEditorLength)
            ? clamp(curveEditorLength, 0, SWITCH_MARKING_LENGTH_MAX)
            : 0;
        return Math.max(
            curveEditor.originalLength,
            requestedLength,
            curveEditorValues.length,
        );
    }, [curveEditor, curveEditorLength, curveEditorValues.length]);

    const saveCurveEditor = async () => {
        if (!curveEditor?.item.catalogId || busyId) return;
        const activeValues = curveEditorValues.slice(0, curveEditorLength);
        const valid = Number.isInteger(curveEditorLength) &&
            curveEditorLength >= SWITCH_MARKING_LENGTH_MIN &&
            curveEditorLength <= SWITCH_MARKING_LENGTH_MAX &&
            activeValues.length === curveEditorLength &&
            activeValues.every(value => Number.isInteger(value) && value >= 0 && value <= 0xffff);
        if (!valid) {
            showToast({
                title: t.SWITCH_MAPPING_CURVE_INVALID_TITLE,
                description: t.SWITCH_MAPPING_CURVE_INVALID_MESSAGE,
                type: "error",
            });
            return;
        }

        const catalogId = curveEditor.item.catalogId;
        const mapping: SwitchMappingPayload = {
            ...curveEditor.mapping,
            length: curveEditorLength,
            originalValues: activeValues,
        };
        setBusyId(`curve:${catalogId}`);
        try {
            const detail = await updateSwitchMappingCurve(catalogId, mapping);
            const installed = await installSwitchMapping(catalogId, detail);
            setSelectedMappingId(installed.id);
            setSelectedMapping(installed);
            setCatalog(current => current.map(item =>
                item.catalogId === detail.catalogId
                    ? {
                        ...item,
                        sha256: detail.revision.sha256,
                        updatedAt: detail.updatedAt,
                    }
                    : item
            ));
            setCurveEditor(null);
            setCurveEditorLength(0);
            setCurveEditorValues([]);
            showToast({
                title: t.SWITCH_MAPPING_CURVE_SAVE_SUCCESS,
                type: "success",
            });
        } catch (error) {
            showToast({
                title: t.SWITCH_MAPPING_CURVE_SAVE_FAILED,
                description: error instanceof Error ? error.message : String(error),
                type: "error",
            });
        } finally {
            setBusyId(null);
        }
    };

    const gridColor = colorMode === "dark"
        ? "rgba(255,255,255,0.1)"
        : "rgba(0,0,0,0.1)";
    const chartOptions = useMemo<ChartOptions<"line">>(() => ({
        responsive: true,
        maintainAspectRatio: false,
        plugins: {
            legend: { display: false },
            title: { display: false },
        },
        scales: {
            x: { grid: { color: gridColor } },
            y: { grid: { color: gridColor } },
        },
        animation: { duration: 500, easing: "easeInOutCubic" },
    }), [gridColor]);
    const chartData = useMemo<ChartData<"line">>(() => ({
        labels: Array.from(
            { length: displayedMapping?.length || 0 },
            (_, index) => (index * (displayedMapping?.step || 0)).toFixed(2),
        ),
        datasets: [{
            label: displayedMapping?.name || "",
            cubicInterpolationMode: "monotone" as const,
            tension: 0.4,
            fill: true,
            backgroundColor: "rgba(75,192,192,0.2)",
            borderColor: "rgba(75,192,192,1)",
            data: displayedMapping?.originalValues || [],
        }],
    }), [displayedMapping]);

    return (
        <Flex
            direction="column"
            width="1700px"
            maxWidth="100%"
            height="100%"
            padding="30px"
            gap={4}
            overflow="hidden"
        >
            <Box width="100%">
                <HStack justifyContent="space-between" mb={3}>
                    <HStack gap={2}>
                        <Text fontWeight="bold">{t.SWITCH_MAPPING_CATALOG_TITLE}</Text>
                        {mappingSource && (
                            <Badge colorPalette={mappingSource === "server-installed" ? "green" : "gray"}>
                                {mappingSource === "server-installed"
                                    ? t.SWITCH_MAPPING_SOURCE_SERVER
                                    : t.SWITCH_MAPPING_SOURCE_FACTORY}
                            </Badge>
                        )}
                    </HStack>
                    {busyId && <Spinner size="sm" />}
                </HStack>

                {!initialized ? (
                    <Flex height={CARD_HEIGHT} alignItems="center" justifyContent="center">
                        <Spinner />
                    </Flex>
                ) : (
                    <Flex
                        width="100%"
                        minWidth={0}
                        gap={3}
                        overflowX="auto"
                        overflowY="hidden"
                        paddingBottom={2}
                        alignItems="stretch"
                    >
                        {axisItems.map(item => {
                            const selected = item.mappingId === selectedMappingId;
                            const installing = item.catalogId !== null && busyId === item.catalogId;
                            const imageUrl = item.catalogId ? catalogImages[item.catalogId] : undefined;
                            const showActions = isAdmin && item.catalogId &&
                                hoveredMappingId === item.mappingId;
                            return (
                                <Box
                                    key={item.mappingId}
                                    role="button"
                                    tabIndex={0}
                                    flex={`0 0 ${CARD_WIDTH}`}
                                    width={CARD_WIDTH}
                                    minWidth={CARD_WIDTH}
                                    maxWidth={CARD_WIDTH}
                                    height={CARD_HEIGHT}
                                    borderWidth="2px"
                                    borderColor={selected ? "blue.500" : "border"}
                                    borderRadius="lg"
                                    padding={1}
                                    position="relative"
                                    cursor={!item.serverItem || busyId || recordingBusy || markingStatus.is_marking
                                        ? "default"
                                        : "pointer"}
                                    opacity={busyId !== null && !installing ? 0.65 : 1}
                                    bg={selected ? "blue.subtle" : "bg"}
                                    _hover={!selected && item.serverItem && !busyId && !recordingBusy &&
                                        !markingStatus.is_marking
                                        ? { borderColor: "blue.300" }
                                        : undefined}
                                    onMouseEnter={() => setHoveredMappingId(item.mappingId)}
                                    onMouseLeave={() => setHoveredMappingId(null)}
                                    onFocus={() => setHoveredMappingId(item.mappingId)}
                                    onClick={() => void selectAxis(item)}
                                    onKeyDown={event => {
                                        if (event.key === "Enter" || event.key === " ") {
                                            event.preventDefault();
                                            void selectAxis(item);
                                        }
                                    }}
                                >
                                    <Box
                                        height={COVER_HEIGHT}
                                        borderWidth="1px"
                                        borderColor="border"
                                        borderRadius="md"
                                        overflow="hidden"
                                        position="relative"
                                        bg="transparent"
                                    >
                                        {imageUrl && (
                                            <Image
                                                src={imageUrl}
                                                alt={t.SWITCH_MAPPING_COVER_ALT.replace("{name}", item.name)}
                                                width="100%"
                                                height="100%"
                                                objectFit="cover"
                                            />
                                        )}
                                        {showActions && (
                                            <HStack position="absolute" right="1" top="1" gap={1}>
                                                <IconButton
                                                    aria-label={t.SWITCH_MAPPING_EDIT_ARIA.replace("{name}", item.name)}
                                                    title={t.SWITCH_MAPPING_EDIT_TITLE}
                                                    size="2xs"
                                                    variant="solid"
                                                    disabled={busyId !== null || recordingBusy || markingStatus.is_marking}
                                                    onClick={event => {
                                                        event.stopPropagation();
                                                        openEditor("edit", item);
                                                    }}
                                                >
                                                    <LuPencil />
                                                </IconButton>
                                                <IconButton
                                                    aria-label={t.SWITCH_MAPPING_DELETE_ARIA.replace("{name}", item.name)}
                                                    title={t.SWITCH_MAPPING_DELETE_ACTION_TITLE}
                                                    size="2xs"
                                                    colorPalette="red"
                                                    variant="solid"
                                                    disabled={busyId !== null || recordingBusy || markingStatus.is_marking}
                                                    onClick={event => {
                                                        event.stopPropagation();
                                                        void deleteAxis(item);
                                                    }}
                                                >
                                                    <LuTrash2 />
                                                </IconButton>
                                            </HStack>
                                        )}
                                    </Box>
                                    <Box
                                        mt={1}
                                        width="100%"
                                        minWidth={0}
                                        maxWidth="100%"
                                        paddingX="4px"
                                        overflow="hidden"
                                    >
                                        <UiTooltip content={item.name} openDelay={300} showArrow portalled>
                                            <Text
                                                width="100%"
                                                minWidth={0}
                                                maxWidth="100%"
                                                display="block"
                                                overflow="hidden"
                                                whiteSpace="nowrap"
                                                textOverflow="ellipsis"
                                                fontSize="xs"
                                                fontWeight="semibold"
                                            >
                                                {item.name}
                                            </Text>
                                        </UiTooltip>
                                    </Box>
                                    {installing && (
                                        <Flex
                                            position="absolute"
                                            inset="0"
                                            alignItems="center"
                                            justifyContent="center"
                                            pointerEvents="none"
                                            zIndex="2"
                                            aria-label={t.SWITCH_MAPPING_DOWNLOADING}
                                            title={t.SWITCH_MAPPING_DOWNLOADING}
                                        >
                                            <Flex
                                                width="44px"
                                                height="44px"
                                                alignItems="center"
                                                justifyContent="center"
                                                borderRadius="full"
                                                bg="blue.500"
                                                color="white"
                                                boxShadow="md"
                                                animation={`${downloadPulse} 1.2s ease-in-out infinite`}
                                            >
                                                <Box as={LuDownload} boxSize="7" />
                                            </Flex>
                                        </Flex>
                                    )}
                                </Box>
                            );
                        })}
                        {isAdmin && (
                            <Box
                                role="button"
                                tabIndex={0}
                                aria-label={t.SWITCH_MAPPING_ADD_ARIA}
                                flex={`0 0 ${CARD_WIDTH}`}
                                width={CARD_WIDTH}
                                minWidth={CARD_WIDTH}
                                maxWidth={CARD_WIDTH}
                                height={CARD_HEIGHT}
                                borderWidth="2px"
                                borderStyle="dashed"
                                borderColor="border"
                                borderRadius="lg"
                                padding={1}
                                cursor={busyId || recordingBusy || markingStatus.is_marking
                                    ? "not-allowed"
                                    : "pointer"}
                                opacity={busyId || recordingBusy || markingStatus.is_marking ? 0.65 : 1}
                                _hover={busyId || recordingBusy || markingStatus.is_marking
                                    ? undefined
                                    : { borderColor: "blue.400", bg: "bg.muted" }}
                                onClick={() => !busyId && !recordingBusy &&
                                    !markingStatus.is_marking && openEditor("create", null)}
                                onKeyDown={event => {
                                    if (!busyId && !recordingBusy && !markingStatus.is_marking &&
                                        (event.key === "Enter" || event.key === " ")) {
                                        event.preventDefault();
                                        openEditor("create", null);
                                    }
                                }}
                            >
                                <Flex
                                    height={COVER_HEIGHT}
                                    borderWidth="1px"
                                    borderStyle="dashed"
                                    borderColor="border"
                                    borderRadius="md"
                                    alignItems="center"
                                    justifyContent="center"
                                >
                                    <LuPlus size={24} />
                                </Flex>
                                <Text mt={1} fontSize="xs" fontWeight="semibold" textAlign="center">
                                    {t.SWITCH_MAPPING_ADD_LABEL}
                                </Text>
                            </Box>
                        )}
                    </Flex>
                )}
            </Box>

            <Box
                width="100%"
                minHeight={0}
                flex="1 1 0"
                padding="8px 0"
                position="relative"
                overflow="hidden"
            >
                <VStack
                    position="absolute"
                    top="30px"
                    right="30px"
                    zIndex={1}
                    padding="2"
                    gap={2}
                    alignItems="flex-end"
                >
                    <HStack gap={2}>
                        <Badge colorPalette="blue" variant="outline" size="sm">
                            {t.SWITCH_MAPPING_SAMPLING_FREQUENCY}: {Number.isFinite(displayedMapping?.samplingFrequency)
                                ? `${displayedMapping?.samplingFrequency.toFixed(0)} Hz`
                                : "N/A"}
                        </Badge>
                        <Badge colorPalette="red" variant="outline" size="sm">
                            {t.SWITCH_MAPPING_SAMPLING_NOISE}: {Number.isFinite(displayedMapping?.samplingNoise)
                                ? displayedMapping?.samplingNoise.toFixed(0)
                                : "N/A"}
                        </Badge>
                    </HStack>
                    {isAdmin && initialized && (
                        <HStack gap={2}>
                            <Button
                                size="xs"
                                colorPalette={markingStatus.is_marking ? "red" : "green"}
                                variant={markingStatus.is_marking ? "outline" : "solid"}
                                loading={recordingBusy}
                                disabled={!markingStatus.is_marking &&
                                    (serverSyncBusy || !selectedIsInstalled ||
                                        !defaultMappingId || busyId !== null)}
                                onClick={() => {
                                    if (markingStatus.is_marking) {
                                        void runRecordingAction(stopMarking, true);
                                        return;
                                    }
                                    if (activeMapping?.id === defaultMappingId) {
                                        setSelectedMappingId(activeMapping.id);
                                        setSelectedMapping(activeMapping);
                                    }
                                    void runRecordingAction(() => startMarking(defaultMappingId));
                                }}
                            >
                                {markingStatus.is_marking
                                    ? t.SWITCH_MAPPING_RECORDING_STOP
                                    : t.SWITCH_MAPPING_RECORDING_START}
                            </Button>
                            <Button
                                size="xs"
                                colorPalette="blue"
                                disabled={!markingStatus.is_marking || markingStatus.is_sampling ||
                                    recordingBusy || serverSyncBusy || busyId !== null}
                                onClick={() => void runRecordingAction(stepMarking)}
                            >
                                {t.SWITCH_MAPPING_RECORDING_STEP}
                            </Button>
                            <Button
                                aria-label={t.SWITCH_MAPPING_CURVE_EDIT_ARIA}
                                title={t.SWITCH_MAPPING_CURVE_EDIT_TITLE}
                                size="xs"
                                colorPalette="blue"
                                variant="outline"
                                disabled={!selectedAxisItem?.catalogId || !selectedAxisItem.serverItem ||
                                    !displayedMapping || busyId !== null || recordingBusy ||
                                    serverSyncBusy || markingStatus.is_marking}
                                onClick={openCurveEditor}
                            >
                                {t.SWITCH_MAPPING_CURVE_EDIT_TITLE}
                            </Button>
                        </HStack>
                    )}
                </VStack>
                <Box height="100%" minHeight={0}>
                    <Line data={chartData} options={chartOptions} />
                </Box>
            </Box>

            <Portal>
                <Dialog.Root
                    open={editor !== null}
                    onOpenChange={details => {
                        if (!details.open) closeEditor();
                    }}
                    closeOnInteractOutside={!busyId}
                    closeOnEscape={!busyId}
                >
                    <Dialog.Backdrop backdropFilter="blur(4px)" />
                    <Dialog.Positioner>
                        <Dialog.Content width="min(92vw, 520px)">
                            <Dialog.Header>
                                <Dialog.Title>
                                    {editor?.mode === "create"
                                        ? t.SWITCH_MAPPING_CREATE_DIALOG_TITLE
                                        : t.SWITCH_MAPPING_EDIT_DIALOG_TITLE}
                                </Dialog.Title>
                            </Dialog.Header>
                            <Dialog.Body>
                                <VStack alignItems="stretch" gap={4}>
                                    <Box>
                                        <Text fontSize="sm" mb={1}>{t.SWITCH_MAPPING_NAME_LABEL}</Text>
                                        <Input
                                            value={editorName}
                                            maxLength={80}
                                            disabled={busyId !== null}
                                            onChange={event => setEditorName(event.target.value)}
                                            placeholder={t.SWITCH_MAPPING_NAME_PLACEHOLDER}
                                        />
                                    </Box>
                                    {editor?.mode === "create" && (
                                        <HStack alignItems="flex-start" gap={3}>
                                            <Box flex="1 1 0">
                                                <Text fontSize="sm" mb={1}>
                                                    {t.SETTINGS_SWITCH_MARKING_LENGTH_LABEL}
                                                </Text>
                                                <Input
                                                    type="number"
                                                    min={SWITCH_MARKING_LENGTH_MIN}
                                                    max={SWITCH_MARKING_LENGTH_MAX}
                                                    step={1}
                                                    value={editorLength}
                                                    disabled={busyId !== null}
                                                    onChange={event => setEditorLength(Number(event.target.value))}
                                                />
                                            </Box>
                                            <Box flex="1 1 0">
                                                <Text fontSize="sm" mb={1}>
                                                    {t.SETTINGS_SWITCH_MARKING_STEP_LABEL}
                                                </Text>
                                                <Input
                                                    type="number"
                                                    min={SWITCH_MARKING_STEP_MIN}
                                                    max={SWITCH_MARKING_STEP_MAX}
                                                    step={0.1}
                                                    value={editorStep}
                                                    disabled={busyId !== null}
                                                    onChange={event => setEditorStep(Number(event.target.value))}
                                                />
                                            </Box>
                                        </HStack>
                                    )}
                                    <Box>
                                        <Text fontSize="sm" mb={1}>{t.SWITCH_MAPPING_COVER_LABEL}</Text>
                                        <Flex
                                            ref={editorCropRef}
                                            role={editorImage ? undefined : "button"}
                                            tabIndex={editorImage ? undefined : 0}
                                            aria-label={editorImage
                                                ? undefined
                                                : t.SWITCH_MAPPING_COVER_PICK_ACTION}
                                            width="min(100%, 368px)"
                                            aspectRatio={COVER_ASPECT_RATIO}
                                            mx="auto"
                                            borderWidth="1px"
                                            borderColor="border.emphasized"
                                            borderRadius="md"
                                            overflow="hidden"
                                            alignItems="center"
                                            justifyContent="center"
                                            position="relative"
                                            bg="bg.muted"
                                            cursor={editorImage
                                                ? editorDragging ? "grabbing" : "grab"
                                                : busyId ? "not-allowed" : "pointer"}
                                            touchAction="none"
                                            userSelect="none"
                                            onMouseEnter={() => setEditorCoverHovered(true)}
                                            onMouseLeave={() => setEditorCoverHovered(false)}
                                            onFocus={() => setEditorCoverHovered(true)}
                                            onBlur={() => setEditorCoverHovered(false)}
                                            onClick={openEditorImagePicker}
                                            onKeyDown={event => {
                                                if (!editorImage && (event.key === "Enter" || event.key === " ")) {
                                                    event.preventDefault();
                                                    openEditorImagePicker();
                                                }
                                            }}
                                            onPointerDown={beginEditorCropDrag}
                                            onPointerMove={moveEditorCrop}
                                            onPointerUp={endEditorCropDrag}
                                            onPointerCancel={endEditorCropDrag}
                                            onWheel={zoomEditorCropWithWheel}
                                            onDoubleClick={resetEditorCropTransform}
                                        >
                                            {editorImagePreview ? (
                                                <Image
                                                    src={editorImagePreview}
                                                    alt={t.SWITCH_MAPPING_COVER_PREVIEW_ALT}
                                                    draggable={false}
                                                    position="absolute"
                                                    left="50%"
                                                    top="50%"
                                                    maxWidth="none"
                                                    width={`${editorCoverMetrics.width}px`}
                                                    height={`${editorCoverMetrics.height}px`}
                                                    transform={`translate(-50%, -50%) translate(${editorOffset.x}px, ${editorOffset.y}px)`}
                                                    pointerEvents="none"
                                                    onLoad={event => {
                                                        const nextSize = {
                                                            width: event.currentTarget.naturalWidth,
                                                            height: event.currentTarget.naturalHeight,
                                                        };
                                                        setEditorImageSize(nextSize);
                                                        setEditorOffset(current => clampCoverOffset(
                                                            current,
                                                            nextSize,
                                                            editorCropSize,
                                                            editorZoom,
                                                        ));
                                                    }}
                                                />
                                            ) : (
                                                <Text color="fg.muted" fontSize="sm">
                                                    {t.SWITCH_MAPPING_NO_COVER}
                                                </Text>
                                            )}
                                            <Box
                                                position="absolute"
                                                inset="3px"
                                                borderWidth="1px"
                                                borderColor="whiteAlpha.500"
                                                borderRadius="sm"
                                                pointerEvents="none"
                                                boxShadow="inset 0 0 0 1px rgba(0, 0, 0, 0.22)"
                                            />
                                            {!editorImage && (
                                                <Flex
                                                    position="absolute"
                                                    inset="0"
                                                    alignItems="center"
                                                    justifyContent="center"
                                                    bg="blackAlpha.500"
                                                    opacity={editorCoverHovered && !busyId ? 1 : 0}
                                                    transition="opacity 0.16s ease"
                                                    pointerEvents="none"
                                                    aria-hidden="true"
                                                >
                                                    <Flex
                                                        width="44px"
                                                        height="44px"
                                                        alignItems="center"
                                                        justifyContent="center"
                                                        borderRadius="full"
                                                        bg="blackAlpha.700"
                                                        color="white"
                                                        boxShadow="md"
                                                    >
                                                        <LuImagePlus size={22} />
                                                    </Flex>
                                                </Flex>
                                            )}
                                        </Flex>
                                        {editorImage && editorImagePreview && (
                                            <HStack width="min(100%, 368px)" mx="auto" mt={2} gap={2}>
                                                <IconButton
                                                    aria-label={t.SWITCH_MAPPING_COVER_ZOOM_OUT}
                                                    title={t.SWITCH_MAPPING_COVER_ZOOM_OUT}
                                                    size="xs"
                                                    variant="outline"
                                                    disabled={busyId !== null || editorZoom <= COVER_ZOOM_MIN}
                                                    onClick={() => changeEditorZoom(editorZoom - 0.1)}
                                                >
                                                    <LuMinus />
                                                </IconButton>
                                                <Input
                                                    aria-label={t.SWITCH_MAPPING_COVER_ZOOM}
                                                    type="range"
                                                    min={COVER_ZOOM_MIN}
                                                    max={COVER_ZOOM_MAX}
                                                    step={0.01}
                                                    value={editorZoom}
                                                    disabled={busyId !== null}
                                                    flex="1 1 0"
                                                    px={0}
                                                    onChange={event => changeEditorZoom(Number(event.target.value))}
                                                />
                                                <Text width="44px" textAlign="right" fontSize="xs">
                                                    {Math.round(editorZoom * 100)}%
                                                </Text>
                                                <IconButton
                                                    aria-label={t.SWITCH_MAPPING_COVER_ZOOM_IN}
                                                    title={t.SWITCH_MAPPING_COVER_ZOOM_IN}
                                                    size="xs"
                                                    variant="outline"
                                                    disabled={busyId !== null || editorZoom >= COVER_ZOOM_MAX}
                                                    onClick={() => changeEditorZoom(editorZoom + 0.1)}
                                                >
                                                    <LuPlus />
                                                </IconButton>
                                                <IconButton
                                                    aria-label={t.SWITCH_MAPPING_COVER_RESET}
                                                    title={t.SWITCH_MAPPING_COVER_RESET}
                                                    size="xs"
                                                    variant="outline"
                                                    disabled={busyId !== null}
                                                    onClick={resetEditorCropTransform}
                                                >
                                                    <LuRotateCcw />
                                                </IconButton>
                                            </HStack>
                                        )}
                                        <input
                                            ref={editorImageInputRef}
                                            type="file"
                                            accept="image/jpeg,image/png,image/webp"
                                            disabled={busyId !== null}
                                            onChange={selectEditorImage}
                                            hidden
                                        />
                                        <Text mt={1} fontSize="xs" color="fg.muted">
                                            {t.SWITCH_MAPPING_COVER_HELPER}
                                        </Text>
                                        <Text mt={0.5} fontSize="xs" color="fg.muted">
                                            {t.SWITCH_MAPPING_COVER_CROP_HELPER}
                                        </Text>
                                        {editorImage && editorImagePreview && (
                                            <Text mt={0.5} fontSize="xs" color="fg.muted">
                                                {t.SWITCH_MAPPING_COVER_INTERACTION_HELPER}
                                            </Text>
                                        )}
                                    </Box>
                                    {editor?.mode === "create" && (
                                        <Text fontSize="xs" color="fg.muted">
                                            {t.SWITCH_MAPPING_CREATE_HELPER}
                                        </Text>
                                    )}
                                </VStack>
                            </Dialog.Body>
                            <Dialog.Footer>
                                <Button variant="outline" disabled={busyId !== null} onClick={closeEditor}>
                                    {t.BUTTON_CANCEL}
                                </Button>
                                <Button
                                    colorPalette="green"
                                    loading={busyId !== null}
                                    disabled={editorImage !== null && !editorImagePreview}
                                    onClick={() => void saveEditor()}
                                >
                                    {t.BUTTON_SAVE}
                                </Button>
                            </Dialog.Footer>
                        </Dialog.Content>
                    </Dialog.Positioner>
                </Dialog.Root>

                <Dialog.Root
                    open={curveEditor !== null}
                    onOpenChange={details => {
                        if (!details.open) closeCurveEditor();
                    }}
                    closeOnInteractOutside={!busyId}
                    closeOnEscape={!busyId}
                >
                    <Dialog.Backdrop backdropFilter="blur(4px)" />
                    <Dialog.Positioner>
                        <Dialog.Content
                            width="min(96vw, 1280px)"
                            maxWidth="96vw"
                            maxHeight="88vh"
                        >
                            <Dialog.Header>
                                <Dialog.Title>{t.SWITCH_MAPPING_CURVE_DIALOG_TITLE}</Dialog.Title>
                            </Dialog.Header>
                            <Dialog.Body minHeight={0} overflow="hidden">
                                <VStack alignItems="stretch" gap={4} minHeight={0}>
                                    <HStack alignItems="flex-end" gap={3}>
                                        <Box width="180px">
                                            <Text fontSize="sm" mb={1}>
                                                {t.SWITCH_MAPPING_CURVE_LENGTH_LABEL}
                                            </Text>
                                            <Input
                                                type="number"
                                                min={SWITCH_MARKING_LENGTH_MIN}
                                                max={SWITCH_MARKING_LENGTH_MAX}
                                                step={1}
                                                value={curveEditorLength}
                                                disabled={busyId !== null}
                                                onChange={event => changeCurveEditorLength(Number(event.target.value))}
                                            />
                                        </Box>
                                        <Text fontSize="sm" color="fg.muted" pb={2}>
                                            {t.SWITCH_MAPPING_CURVE_LENGTH_HELPER}
                                        </Text>
                                    </HStack>

                                    <Box
                                        borderWidth="1px"
                                        borderColor="border"
                                        borderRadius="md"
                                        overflowX="auto"
                                        overflowY="hidden"
                                        maxWidth="100%"
                                    >
                                        <Table.Root
                                            size="sm"
                                            variant="outline"
                                            tableLayout="fixed"
                                            minWidth={`${Math.max(620, 132 + curveEditorColumnCount * 96)}px`}
                                        >
                                            <Table.Header>
                                                <Table.Row>
                                                    <Table.ColumnHeader
                                                        width="132px"
                                                        minWidth="132px"
                                                        position="sticky"
                                                        left={0}
                                                        zIndex={2}
                                                        bg="bg"
                                                    >
                                                        {t.SWITCH_MAPPING_CURVE_STEP_HEADER}
                                                    </Table.ColumnHeader>
                                                    {Array.from({ length: curveEditorColumnCount }, (_, index) => {
                                                        const inactive = index >= curveEditorLength;
                                                        return (
                                                            <Table.ColumnHeader
                                                                key={index}
                                                                width="96px"
                                                                textAlign="center"
                                                                bg={inactive ? "bg.muted" : undefined}
                                                                color={inactive ? "fg.muted" : undefined}
                                                                opacity={inactive ? 0.55 : 1}
                                                            >
                                                                {index + 1}
                                                            </Table.ColumnHeader>
                                                        );
                                                    })}
                                                </Table.Row>
                                            </Table.Header>
                                            <Table.Body>
                                                <Table.Row>
                                                    <Table.Cell
                                                        position="sticky"
                                                        left={0}
                                                        zIndex={1}
                                                        bg="bg"
                                                        fontWeight="medium"
                                                    >
                                                        {t.SWITCH_MAPPING_CURVE_DISTANCE_HEADER}
                                                    </Table.Cell>
                                                    {Array.from({ length: curveEditorColumnCount }, (_, index) => {
                                                        const inactive = index >= curveEditorLength;
                                                        return (
                                                            <Table.Cell
                                                                key={index}
                                                                textAlign="center"
                                                                bg={inactive ? "bg.muted" : undefined}
                                                                color={inactive ? "fg.muted" : undefined}
                                                                opacity={inactive ? 0.55 : 1}
                                                            >
                                                                {(index * (curveEditor?.mapping.step || 0)).toFixed(2)} mm
                                                            </Table.Cell>
                                                        );
                                                    })}
                                                </Table.Row>
                                                <Table.Row>
                                                    <Table.Cell
                                                        position="sticky"
                                                        left={0}
                                                        zIndex={1}
                                                        bg="bg"
                                                        fontWeight="medium"
                                                    >
                                                        {t.SWITCH_MAPPING_CURVE_VALUE_HEADER}
                                                    </Table.Cell>
                                                    {Array.from({ length: curveEditorColumnCount }, (_, index) => {
                                                        const inactive = index >= curveEditorLength;
                                                        return (
                                                            <Table.Cell
                                                                key={index}
                                                                padding={2}
                                                                bg={inactive ? "bg.muted" : undefined}
                                                                opacity={inactive ? 0.55 : 1}
                                                            >
                                                                <Input
                                                                    aria-label={`${t.SWITCH_MAPPING_CURVE_VALUE_HEADER} ${index + 1}`}
                                                                    type="number"
                                                                    min={0}
                                                                    max={0xffff}
                                                                    step={1}
                                                                    size="sm"
                                                                    value={curveEditorValues[index] ?? 0}
                                                                    disabled={inactive || busyId !== null}
                                                                    onChange={event => changeCurveEditorValue(
                                                                        index,
                                                                        Number(event.target.value),
                                                                    )}
                                                                />
                                                            </Table.Cell>
                                                        );
                                                    })}
                                                </Table.Row>
                                            </Table.Body>
                                        </Table.Root>
                                    </Box>
                                </VStack>
                            </Dialog.Body>
                            <Dialog.Footer>
                                <Button variant="outline" disabled={busyId !== null} onClick={closeCurveEditor}>
                                    {t.BUTTON_CANCEL}
                                </Button>
                                <Button
                                    colorPalette="green"
                                    loading={busyId !== null}
                                    onClick={() => void saveCurveEditor()}
                                >
                                    {t.BUTTON_SAVE}
                                </Button>
                            </Dialog.Footer>
                        </Dialog.Content>
                    </Dialog.Positioner>
                </Dialog.Root>
            </Portal>
        </Flex>
    );
}
