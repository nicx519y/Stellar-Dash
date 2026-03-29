'use client';

import { Box, Button, Dialog, HStack, Portal, Text, VStack, Separator, Table, CloseButton } from "@chakra-ui/react";
import { BsRecordCircle, BsStopCircle  } from "react-icons/bs";
import { useEffect, useMemo, useRef, useState } from "react";
import { useLanguage } from "@/contexts/language-context";
import type { MacroConfig, MacroStep } from "@/types/gamepad-config";
import { MAX_MACRO_STEPS } from "@/types/gamepad-config";
import { KeyField } from "./key-field";
import { useButtonMonitor } from "@/hooks/use-button-monitor";
import type { ButtonStateBinaryData } from "@/lib/button-binary-parser";
import { isButtonTriggered } from "@/lib/button-binary-parser";
import { GameControllerButtonList, Platform, XInputButtonMap, PS4ButtonMap, SwitchButtonMap } from "@/types/gamepad-config";
import type { GameControllerButton } from "@/types/gamepad-config";

function buttonToFirmwareId(btn: GameControllerButton): number {
    if (btn === "DPAD_UP") return 1;
    if (btn === "DPAD_DOWN") return 2;
    if (btn === "DPAD_LEFT") return 3;
    if (btn === "DPAD_RIGHT") return 4;
    if (btn === "B1") return 5;
    if (btn === "B2") return 6;
    if (btn === "B3") return 7;
    if (btn === "B4") return 8;
    if (btn === "L1") return 9;
    if (btn === "R1") return 10;
    if (btn === "L2") return 11;
    if (btn === "R2") return 12;
    if (btn === "S1") return 13;
    if (btn === "S2") return 14;
    if (btn === "L3") return 15;
    if (btn === "R3") return 16;
    if (btn === "A1") return 17;
    if (btn === "A2") return 18;
    return 0;
}

export function MacroField(props: {
    value: MacroConfig;
    changeValue: (newValue: MacroConfig) => void;
    label: string;
    keyMapping: { [key in GameControllerButton]?: number[] };
    inputKey: number;
    inputMode: Platform;
    isActive?: boolean;
    suspendInputListening?: boolean;
    disabled?: boolean;
    onClick?: () => void;
    onRecordingChange?: (recording: boolean) => void;
}) {
    const { t } = useLanguage();
    const [open, setOpen] = useState(false);
    const [isRecording, setIsRecording] = useState(false);
    const [stopReason, setStopReason] = useState<"manual" | "full" | null>(null);
    const [recordedSteps, setRecordedSteps] = useState<MacroStep[]>(props.value.steps ?? []);
    const prevMaskRef = useRef<number>(0);
    const lastEventTsRef = useRef<number>(-1);
    const firstEventTsRef = useRef<number>(-1);
    const timelineEndTsRef = useRef<number>(-1);
    const [timelineNowMs, setTimelineNowMs] = useState<number>(0);
    const isRecordingRef = useRef(false);
    const onRecordingChangeRef = useRef(props.onRecordingChange);
    const keyMappingRef = useRef(props.keyMapping);
    const eventAbsMsRef = useRef<number[]>([]);

    const { startMonitoring, stopMonitoring } = useButtonMonitor({
        autoInitialize: false,
        controlMonitoringCommand: false,
        onButtonStatesChange: (data: ButtonStateBinaryData) => {
            if (!isRecordingRef.current) return;
            if (!data) return;

            const now = performance.now();

            let controllerMask = 0;
            const km = keyMappingRef.current ?? {};
            for (const [btn, keys] of Object.entries(km)) {
                const firmwareId = buttonToFirmwareId(btn as GameControllerButton);
                if (!firmwareId) continue;
                const bit = 1 << (firmwareId - 1);
                const keyList = (keys ?? []) as number[];
                for (const k of keyList) {
                    if (isButtonTriggered(data.triggerMask, k)) {
                        controllerMask |= bit;
                        break;
                    }
                }
            }

            const prev = prevMaskRef.current >>> 0;
            const cur = controllerMask >>> 0;
            if (cur === prev) return;
            prevMaskRef.current = cur;

            let deltaMs = 0;
            if (lastEventTsRef.current < 0) {
                deltaMs = 0;
                firstEventTsRef.current = now;
            } else {
                deltaMs = Math.round(now - lastEventTsRef.current);
            }
            lastEventTsRef.current = now;
            const timeMs = Math.max(0, Math.min(65535, deltaMs));
            const absMsExact = firstEventTsRef.current >= 0 ? Math.max(0, now - firstEventTsRef.current) : 0;

            setRecordedSteps(prevSteps => {
                if (prevSteps.length >= MAX_MACRO_STEPS) return prevSteps;
                const next = [...prevSteps, { timeMs, buttonMask: cur }];
                eventAbsMsRef.current = [...eventAbsMsRef.current, absMsExact];
                if (next.length >= MAX_MACRO_STEPS) {
                    setIsRecording(false);
                    isRecordingRef.current = false;
                    timelineEndTsRef.current = performance.now();
                    setStopReason("full");
                    stopMonitoring();
                }
                return next;
            });
        },
    });

    useEffect(() => {
        onRecordingChangeRef.current = props.onRecordingChange;
    }, [props.onRecordingChange]);

    useEffect(() => {
        if (isRecording) return;
        keyMappingRef.current = props.keyMapping;
    }, [props.keyMapping, isRecording]);

    useEffect(() => {
        isRecordingRef.current = isRecording;
    }, [isRecording]);

    useEffect(() => {
        if (open) return;
        if (isRecording) return;
        setRecordedSteps(props.value.steps ?? []);
        eventAbsMsRef.current = [];
    }, [props.value.steps, open, isRecording]);

    useEffect(() => {
        if (props.disabled) return;
        if (props.suspendInputListening) return;
        if (!props.isActive) return;
        if (open) return;
        if (isRecording) return;
        if (props.inputKey < 0) return;

        const keyId = props.inputKey;
        const current = props.value.triggerKeys ?? [];
        if (current.indexOf(keyId) !== -1) return;

        const maxKeys = 4;
        if (current.length >= maxKeys) return;

        props.changeValue({
            ...props.value,
            triggerKeys: [...current, keyId],
            steps: props.value.steps ?? [],
        });
    }, [props.inputKey, props.suspendInputListening]);

    useEffect(() => {
        onRecordingChangeRef.current?.(open);
    }, [open]);

    useEffect(() => {
        if (!open) {
            setIsRecording(false);
            isRecordingRef.current = false;
            setStopReason(null);
            stopMonitoring();
            setTimelineNowMs(0);
            firstEventTsRef.current = -1;
            timelineEndTsRef.current = -1;
            eventAbsMsRef.current = [];
        }
    }, [open]);

    const triggerKeyStrings = useMemo(() => {
        return (props.value.triggerKeys ?? []).map(k => (k + 1).toString());
    }, [props.value.triggerKeys]);

    const buttonLabelMap = useMemo(() => {
        switch (props.inputMode) {
            case Platform.XINPUT: return XInputButtonMap;
            case Platform.PS4: return PS4ButtonMap;
            case Platform.PS5: return PS4ButtonMap;
            case Platform.SWITCH: return SwitchButtonMap;
            default: return new Map<GameControllerButton, string>();
        }
    }, [props.inputMode]);

    const firmwareIdToLabel = useMemo(() => {
        const map = new Map<number, string>();
        for (const btn of GameControllerButtonList) {
            const fid = buttonToFirmwareId(btn as GameControllerButton);
            if (!fid) continue;
            map.set(fid, buttonLabelMap.get(btn as GameControllerButton) ?? btn);
        }
        return map;
    }, [buttonLabelMap]);

    const maskToLabels = (mask: number): string => {
        const labels: string[] = [];
        const m = mask >>> 0;
        for (let fid = 1; fid <= 32; fid++) {
            if ((m & (1 << (fid - 1))) !== 0) {
                labels.push(firmwareIdToLabel.get(fid) ?? `B${fid}`);
            }
        }
        return labels.length > 0 ? labels.join(" + ") : "-";
    };

    useEffect(() => {
        if (!open) return;
        if (!isRecording) return;
        let raf = 0;
        const loop = () => {
            const start = firstEventTsRef.current;
            if (start >= 0) setTimelineNowMs(performance.now() - start);
            raf = requestAnimationFrame(loop);
        };
        raf = requestAnimationFrame(loop);
        return () => cancelAnimationFrame(raf);
    }, [open, isRecording]);

    const frameRows = useMemo(() => {
        const frameMs = 1000 / 60;
        const absMsList: number[] = [];
        if (eventAbsMsRef.current.length === recordedSteps.length && recordedSteps.length > 0) {
            absMsList.push(...eventAbsMsRef.current);
        } else {
            let absMs = 0;
            for (const step of recordedSteps) {
                absMs += step.timeMs ?? 0;
                absMsList.push(absMs);
            }
        }

        const byFrame = new Map<number, { state: number }>();
        for (let i = 0; i < recordedSteps.length; i++) {
            const step = recordedSteps[i];
            const absMs = absMsList[i] ?? 0;
            const frame = Math.max(0, Math.floor(absMs / frameMs));
            const current = byFrame.get(frame) ?? { state: 0 };
            current.state = (step.buttonMask ?? 0) >>> 0;
            byFrame.set(frame, current);
        }

        const frames = Array.from(byFrame.keys()).sort((a, b) => a - b);
        if (frames.length === 0) return [];

        let endMs = absMsList.length > 0 ? absMsList[absMsList.length - 1] : 0;
        if (isRecording && firstEventTsRef.current >= 0) {
            endMs = Math.max(0, timelineNowMs);
        } else if (!isRecording && firstEventTsRef.current >= 0 && timelineEndTsRef.current >= firstEventTsRef.current) {
            endMs = Math.max(0, timelineEndTsRef.current - firstEventTsRef.current);
        }
        const endFrame = Math.max(0, Math.floor(endMs / frameMs));

        const rows = frames.map((frame, idx) => {
            const nextFrame = frames[idx + 1];
            const durationFrames = nextFrame !== undefined ? Math.max(1, nextFrame - frame) : Math.max(1, endFrame - frame + 1);
            const payload = byFrame.get(frame)!;
            return {
                frame,
                durationFrames,
                state: payload.state,
            };
        });
        return rows.reverse();
    }, [recordedSteps, isRecording, timelineNowMs]);

    const handleTriggerKeysChange = (newKeyIndexes: string[]) => {
        const triggerKeys = newKeyIndexes
            .map(v => parseInt(v) - 1)
            .filter(v => Number.isFinite(v) && v >= 0);
        props.changeValue({ ...props.value, triggerKeys, steps: props.value.steps ?? [] });
    };

    const handleOpen = () => {
        if (props.disabled) return;
        setRecordedSteps(props.value.steps ?? []);
        eventAbsMsRef.current = [];
        setStopReason(null);
        setIsRecording(false);
        setOpen(true);
    };

    const handleStartRecording = async () => {
        if (props.disabled) return;
        setRecordedSteps([]);
        setStopReason(null);
        prevMaskRef.current = 0;
        lastEventTsRef.current = -1;
        firstEventTsRef.current = -1;
        timelineEndTsRef.current = -1;
        setTimelineNowMs(0);
        keyMappingRef.current = props.keyMapping;
        eventAbsMsRef.current = [];
        setIsRecording(true);
        isRecordingRef.current = true;
        await startMonitoring();
    };

    const handleStopRecording = async () => {
        setIsRecording(false);
        isRecordingRef.current = false;
        timelineEndTsRef.current = performance.now();
        setStopReason("manual");
        await stopMonitoring();
    };

    const handleClose = () => {
        props.changeValue({ ...props.value, steps: recordedSteps });
        isRecordingRef.current = false;
        timelineEndTsRef.current = performance.now();
        setOpen(false);
    };

    const stepsCount = recordedSteps.length;
    const durationFrames = useMemo(() => {
        const totalMs = recordedSteps.reduce((acc, step) => acc + (step.timeMs ?? 0), 0);
        return Math.max(0, Math.round(totalMs / (1000 / 60)));
    }, [recordedSteps]);

    return (
        <>
            <HStack gap={2}>
                <Text fontSize="10px" color="gray.400" fontWeight="bold" lineHeight="1rem">
                    {`[ ${props.label} ]`}
                </Text>
                <KeyField
                    value={triggerKeyStrings}
                    isActive={props.isActive ?? false}
                    disabled={props.disabled ?? false}
                    onClick={props.onClick ?? undefined}
                    changeValue={handleTriggerKeysChange}
                    width={170}
                />
                <Separator orientation="vertical" height="5" />
                <Box width="300px" height="30px" display="flex" alignItems="center">
                    <Text fontSize="10px" color="gray.300">
                        steps: {stepsCount} | duration: {durationFrames}f
                    </Text>
                </Box>
                <Button
                    size="xs"
                    colorPalette="blue"
                    variant="surface"
                    onClick={handleOpen}
                    disabled={props.disabled}
                    style={{
                        minWidth: "60px",
                        height: "29px",
                    }}
                >
                    {t.MACRO_FIELD_RECORD_BUTTON}
                </Button>
            </HStack>

            {open && (
                <Portal>
                    <Box
                        position="fixed"
                        zIndex={9998}
                        top={0}
                        left={0}
                        right={0}
                        bottom={0}
                        display="flex"
                        alignItems="center"
                        justifyContent="center"
                    >
                        <Box
                            position="absolute"
                            top={0}
                            left={0}
                            right={0}
                            bottom={0}
                            bg="blackAlpha.100"
                            backdropFilter="blur(4px)"
                        />
                        <Dialog.Root
                            open={open}
                            modal
                            size="lg"
                            onPointerDownOutside={(e) => e.preventDefault()}
                            onEscapeKeyDown={(e) => e.preventDefault()}
                        >
                            <Dialog.Positioner>
                                <Dialog.Backdrop onClick={handleClose} />
                                <Dialog.Content>
                                    <Dialog.Header>
                                        <Dialog.Title fontSize="sm" opacity={0.75}>
                                            {t.MACRO_DIALOG_TITLE} [ {props.label} ]
                                        </Dialog.Title>
                                    </Dialog.Header>
                                    <Dialog.Body>
                                        <VStack gap={3} alignItems="stretch">
                                            <Text fontSize="12px" color="gray.400">
                                                {t.MACRO_DIALOG_MESSAGE}
                                            </Text>
                                            <HStack gap={3}>
                                                <Button
                                                    size="xs"
                                                    colorPalette="green"
                                                    onClick={handleStartRecording}
                                                    disabled={isRecording}
                                                >
                                                    <BsRecordCircle />
                                                    {t.MACRO_DIALOG_BUTTON_START}
                                                </Button>
                                                <Button
                                                    size="xs"
                                                    colorPalette="red"
                                                    onClick={handleStopRecording}
                                                    disabled={!isRecording}
                                                >
                                                    <BsStopCircle />
                                                    {t.MACRO_DIALOG_BUTTON_STOP}
                                                </Button>
                                                <Text fontSize="12px" color="gray.400">
                                                    {stopReason === "manual"
                                                        ? t.MACRO_DIALOG_STOP_REASON_MANUAL
                                                        : stopReason === "full"
                                                            ? t.MACRO_DIALOG_STOP_REASON_FULL
                                                            : isRecording
                                                                ? "Recording..."
                                                                : ""}
                                                </Text>
                                            </HStack>
                                            <Box maxH="600px" overflowY="auto">
                                                <Table.Root fontSize="sm" colorPalette="green" interactive opacity={0.9}>
                                                    <Table.Header fontSize="xs">
                                                        <Table.Row>
                                                            <Table.ColumnHeader width="20%" textAlign="center">帧数</Table.ColumnHeader>
                                                            <Table.ColumnHeader width="80%" textAlign="center">按下</Table.ColumnHeader>
                                                        </Table.Row>
                                                    </Table.Header>
                                                    <Table.Body fontSize="xs">
                                                        {frameRows.length === 0 ? (
                                                            <Table.Row>
                                                                <Table.Cell colSpan={2} textAlign="center" color="gray.500">
                                                                    {isRecording ? "等待按键..." : "暂无录制数据"}
                                                                </Table.Cell>
                                                            </Table.Row>
                                                        ) : (
                                                            frameRows.map((r) => (
                                                                <Table.Row key={r.frame}>
                                                                    <Table.Cell textAlign="center">{r.durationFrames}</Table.Cell>
                                                                    <Table.Cell>{maskToLabels(r.state)}</Table.Cell>
                                                                </Table.Row>
                                                            ))
                                                        )}
                                                    </Table.Body>
                                                </Table.Root>
                                            </Box>
                                        </VStack>
                                    </Dialog.Body>
                                    <Dialog.Footer>
                                        <Button w="140px" size="sm" colorPalette={"blue"} onClick={handleClose}>
                                            {t.MACRO_DIALOG_BUTTON_CLOSE}
                                        </Button>
                                    </Dialog.Footer>
                                    <Dialog.CloseTrigger asChild>
                                        <CloseButton size="sm" onClick={handleClose} />
                                    </Dialog.CloseTrigger>
                                </Dialog.Content>
                            </Dialog.Positioner>
                        </Dialog.Root>
                    </Box>
                </Portal>
            )}
        </>
    );
}

MacroField.displayName = "MacroField";
