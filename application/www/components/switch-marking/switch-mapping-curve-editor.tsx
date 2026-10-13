'use client';
import { ExclusiveDialog } from '@/components/ui/exclusive-dialog';
import { OVERLAY_PRIORITY } from '@/lib/overlay-coordinator';
import {
    SWITCH_MARKING_LENGTH_MAX,
    SWITCH_MARKING_LENGTH_MIN
} from "@/types/gamepad-config";
import {
    Box,
    Button,
    Dialog,
    HStack,
    Input,
    Table,
    Text,
    VStack
} from "@chakra-ui/react";
import type { useSwitchMarkingController } from './use-switch-marking-controller';

type Props = Pick<ReturnType<typeof useSwitchMarkingController>, 'curveEditor' | 'closeCurveEditor' | 'busyId' | 't' | 'curveEditorLength' | 'changeCurveEditorLength' | 'curveEditorColumnCount' | 'curveEditorValues' | 'changeCurveEditorValue' | 'saveCurveEditor'>;
export function SwitchMappingCurveEditor(props: Props) {
    const {
        curveEditor, closeCurveEditor, busyId, t, curveEditorLength, changeCurveEditorLength,
        curveEditorColumnCount, curveEditorValues, changeCurveEditorValue, saveCurveEditor,
    } = props;
    return (<ExclusiveDialog priority={OVERLAY_PRIORITY.editor}
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
    </ExclusiveDialog>);
}
