'use client';
import {
    Box,
    Flex,
    HStack,
    IconButton,
    Image,
    Spinner,
    Text
} from "@chakra-ui/react";
import {
    LuDownload,
    LuPencil,
    LuPlus,
    LuTrash2
} from "react-icons/lu";
import { Tooltip as UiTooltip } from "../ui/tooltip";
import { CARD_HEIGHT, CARD_WIDTH, COVER_HEIGHT, downloadPulse } from './shared';
import type { useSwitchMarkingController } from './use-switch-marking-controller';

type Props = Pick<ReturnType<typeof useSwitchMarkingController>, 'initialized' | 'axisItems' | 'selectedMappingId' | 'busyId' | 'catalogImages' | 'isAdmin' | 'hoveredMappingId' | 'recordingBusy' | 'markingStatus' | 'setHoveredMappingId' | 'selectAxis' | 't' | 'openEditor' | 'deleteAxis'>;
export function SwitchMappingCatalog(props: Props) {
    const {
        initialized, axisItems, selectedMappingId, busyId, catalogImages, isAdmin, hoveredMappingId,
        recordingBusy, markingStatus, setHoveredMappingId, selectAxis, t, openEditor, deleteAxis,
    } = props;
    return (<Flex
        data-testid="switch-mapping-catalog"
        aria-busy={!initialized}
        width="100%"
        minWidth={0}
        height={`calc(${CARD_HEIGHT} + 24px)`}
        flexShrink={0}
        gap={3}
        overflowX="auto"
        overflowY="hidden"
        paddingBottom={2}
        alignItems="stretch"
    >
        {!initialized ? (
            <Flex width="100%" height={CARD_HEIGHT} alignItems="center" justifyContent="center">
                <Spinner />
            </Flex>
        ) : (<>
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
        </>)}
    </Flex>);
}
