'use client';
import { ExclusiveDialog } from '@/components/ui/exclusive-dialog';
import { OVERLAY_PRIORITY } from '@/lib/overlay-coordinator';
import {
    SWITCH_MARKING_LENGTH_MAX,
    SWITCH_MARKING_LENGTH_MIN,
    SWITCH_MARKING_STEP_MAX,
    SWITCH_MARKING_STEP_MIN,
} from "@/types/gamepad-config";
import {
    Box,
    Button,
    Dialog,
    Flex,
    HStack,
    IconButton,
    Image,
    Input,
    Text,
    VStack
} from "@chakra-ui/react";
import {
    LuImagePlus,
    LuMinus,
    LuPlus,
    LuRotateCcw
} from "react-icons/lu";
import { clampCoverOffset, COVER_ASPECT_RATIO, COVER_ZOOM_MAX, COVER_ZOOM_MIN } from './shared';
import type { useSwitchMarkingController } from './use-switch-marking-controller';

type Props = Pick<ReturnType<typeof useSwitchMarkingController>, 'editor' | 'closeEditor' | 'busyId' | 't' | 'editorName' | 'setEditorName' | 'editorLength' | 'setEditorLength' | 'editorStep' | 'setEditorStep' | 'editorCropRef' | 'editorImage' | 'editorDragging' | 'setEditorCoverHovered' | 'openEditorImagePicker' | 'beginEditorCropDrag' | 'moveEditorCrop' | 'endEditorCropDrag' | 'zoomEditorCropWithWheel' | 'resetEditorCropTransform' | 'editorImagePreview' | 'editorCoverMetrics' | 'editorOffset' | 'setEditorImageSize' | 'setEditorOffset' | 'editorCropSize' | 'editorZoom' | 'editorCoverHovered' | 'changeEditorZoom' | 'editorImageInputRef' | 'selectEditorImage' | 'saveEditor'>;
export function SwitchMappingEditor(props: Props) {
    const {
        editor, closeEditor, busyId, t, editorName, setEditorName, editorLength, setEditorLength,
        editorStep, setEditorStep, editorCropRef, editorImage, editorDragging, setEditorCoverHovered,
        openEditorImagePicker, beginEditorCropDrag, moveEditorCrop, endEditorCropDrag,
        zoomEditorCropWithWheel, resetEditorCropTransform, editorImagePreview, editorCoverMetrics,
        editorOffset, setEditorImageSize, setEditorOffset, editorCropSize, editorZoom,
        editorCoverHovered, changeEditorZoom, editorImageInputRef, selectEditorImage, saveEditor,
    } = props;
    return (<ExclusiveDialog priority={OVERLAY_PRIORITY.editor}
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
    </ExclusiveDialog>);
}
