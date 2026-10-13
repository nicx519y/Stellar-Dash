'use client';
import { useGamepadFields } from "@/contexts/gamepad-config-context";
import { useLanguage } from "@/contexts/language-context";
import { useUserAuth } from "@/contexts/user-auth-context";
import {
    SwitchMappingCatalogItem,
    SwitchMappingPayload,
} from "@/types/adc";
import {
    SWITCH_MARKING_LENGTH_MAX,
    SWITCH_MARKING_LENGTH_MIN,
    SWITCH_MARKING_STEP_MAX,
    SWITCH_MARKING_STEP_MIN,
} from "@/types/gamepad-config";
import {
    ChartData,
    ChartOptions
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
import { openConfirm } from "../dialog-confirm";
import { useColorMode } from "../ui/color-mode";
import { showToast } from "../ui/toaster";
import {
    AxisListItem, blobToDataUrl, clamp, clampCoverOffset, COVER_OUTPUT_HEIGHT, COVER_OUTPUT_WIDTH,
    COVER_ZOOM_MAX, COVER_ZOOM_MIN, coverMetrics, CurveEditorState, EditorState, Point, Size,
} from './shared';

export function useSwitchMarkingController() {

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
    } = useGamepadFields("deviceConnected", "dataIsReady", "mappingList", "defaultMappingId", "activeMapping", "mappingStorageMode", "mappingSource", "markingStatus", "fetchMappingList", "fetchActiveMapping", "fetchMarkingStatus", "fetchSwitchMappingCatalog", "fetchSwitchMappingDetail", "fetchSwitchMappingImage", "uploadSwitchMappingImage", "updateSwitchMappingMetadata", "updateSwitchMappingCurve", "deleteSwitchMapping", "installSwitchMapping", "clearInstalledSwitchMapping", "createSwitchMappingFromCurrent", "startMarking", "stopMarking", "stepMarking", "syncMarkingProgress");

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
            fetchCatalogRef.current(isAdmin),
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
                const image = await fetchImageRef.current(item.catalogId, isAdmin);
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
    }, [catalog, isAdmin]);

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
        const items = await fetchCatalogRef.current(isAdmin);
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
            const detail = await fetchSwitchMappingDetail(item.catalogId, isAdmin);
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
                    // Recording remains attached to the curve currently on the device.
                    // The server may have forked a published revision into a new draft.
                    setSelectedMapping({ ...detail.revision.mapping, id: mapping.id });
                    setCatalog(current => current.map(item =>
                        item.catalogId === detail.catalogId
                            ? {
                                ...item,
                                isDraft: true,
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
                        revisionId: detail.revision.revisionId,
                        isDraft: true,
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
    return {
        t, mappingSource, busyId, initialized, axisItems, selectedMappingId, catalogImages, isAdmin,
        hoveredMappingId, recordingBusy, markingStatus, setHoveredMappingId, selectAxis, openEditor,
        deleteAxis, displayedMapping, serverSyncBusy, selectedIsInstalled, defaultMappingId,
        runRecordingAction, stopMarking, activeMapping, setSelectedMappingId, setSelectedMapping,
        startMarking, stepMarking, selectedAxisItem, openCurveEditor, chartData, chartOptions, editor,
        closeEditor, editorName, setEditorName, editorLength, setEditorLength, editorStep,
        setEditorStep, editorCropRef, editorImage, editorDragging, setEditorCoverHovered,
        openEditorImagePicker, beginEditorCropDrag, moveEditorCrop, endEditorCropDrag,
        zoomEditorCropWithWheel, resetEditorCropTransform, editorImagePreview, editorCoverMetrics,
        editorOffset, setEditorImageSize, setEditorOffset, editorCropSize, editorZoom,
        editorCoverHovered, changeEditorZoom, editorImageInputRef, selectEditorImage, saveEditor,
        curveEditor, closeCurveEditor, curveEditorLength, changeCurveEditorLength,
        curveEditorColumnCount, curveEditorValues, changeCurveEditorValue, saveCurveEditor,
    };
}
