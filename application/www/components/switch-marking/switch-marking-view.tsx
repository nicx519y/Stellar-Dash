'use client';
import {
    Badge,
    Box,
    Button,
    Flex,
    HStack,
    Portal,
    Spinner,
    Text,
    VStack
} from "@chakra-ui/react";
import { Line } from "react-chartjs-2";
import { SwitchMappingCatalog } from './switch-mapping-catalog';
import { SwitchMappingCurveEditor } from './switch-mapping-curve-editor';
import { SwitchMappingEditor } from './switch-mapping-editor';
import type { useSwitchMarkingController } from './use-switch-marking-controller';

export function SwitchMarkingView({ model }: { model: ReturnType<typeof useSwitchMarkingController> }) {
    const {
        t,
        mappingSource,
        busyId,
        initialized,
        isAdmin,
        recordingBusy,
        markingStatus,
        displayedMapping,
        serverSyncBusy,
        selectedIsInstalled,
        defaultMappingId,
        runRecordingAction,
        stopMarking,
        activeMapping,
        setSelectedMappingId,
        setSelectedMapping,
        startMarking,
        stepMarking,
        selectedAxisItem,
        openCurveEditor,
        chartData,
        chartOptions
    } = model;
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
            <Box width="100%" flexShrink={0}>
                <HStack justifyContent="space-between" mb={3} minHeight="24px">
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

                <SwitchMappingCatalog {...model} />
            </Box>

            <Box
                data-testid="switch-mapping-chart"
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
                <SwitchMappingEditor {...model} />

                <SwitchMappingCurveEditor {...model} />
            </Portal>
        </Flex>
    );
}
