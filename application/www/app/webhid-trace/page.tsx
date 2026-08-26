'use client';

import { useEffect, useMemo, useRef, useState } from 'react';
import {
  Badge,
  Box,
  Button,
  chakra,
  Flex,
  Grid,
  Heading,
  HStack,
  Input,
  Switch,
  Table,
  Tabs,
  Text,
} from '@chakra-ui/react';
import {
  LuActivity,
  LuDownload,
  LuExternalLink,
  LuPause,
  LuPlay,
  LuTrash2,
} from 'react-icons/lu';
import {
  openWebHidTraceViewer,
  relatedWebHidTraceFrames,
  upsertWebHidTraceRecord,
  type WebHidNetworkTraceMode,
  type WebHidTraceRecord,
  type WebHidTraceSource,
  type WebHidTraceStatus,
  type WebHidTraceViewerSession,
} from '@/lib/device-transport/webhid-network-trace';

const MAX_RECORDS = 2_000;
const SOURCE_ACTIVE_MS = 10_000;
const MONO_FONT = 'ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace';

type DirectionFilter = 'all' | 'tx' | 'rx';

interface StatusPresentation {
  value: WebHidTraceStatus;
  label: string;
  colorPalette: 'gray' | 'green' | 'red' | 'yellow' | 'purple';
}

export default function WebHidTracePage() {
  const [mode, setMode] = useState<WebHidNetworkTraceMode>('control');
  const [records, setRecords] = useState<WebHidTraceRecord[]>([]);
  const [sources, setSources] = useState<Record<string, WebHidTraceSource>>({});
  const [supported, setSupported] = useState(true);
  const [paused, setPaused] = useState(false);
  const [directionFilter, setDirectionFilter] = useState<DirectionFilter>('all');
  const [hideClockSync, setHideClockSync] = useState(true);
  const [search, setSearch] = useState('');
  const [selectedId, setSelectedId] = useState<string | null>(null);
  const [now, setNow] = useState(() => Date.now());
  const sessionRef = useRef<WebHidTraceViewerSession | null>(null);
  const pausedRef = useRef(false);

  useEffect(() => {
    document.title = 'HBox WebHID Trace';
    const session = openWebHidTraceViewer('control', {
      onRecord(record) {
        if (pausedRef.current) return;
        setRecords((current) => upsertWebHidTraceRecord(current, record, MAX_RECORDS));
      },
      onSource(source) {
        setSources((current) => ({ ...current, [source.sourceId]: source }));
      },
    });
    sessionRef.current = session;
    setSupported(session.supported);
    const clock = window.setInterval(() => setNow(Date.now()), 1_000);
    return () => {
      window.clearInterval(clock);
      session.close();
      sessionRef.current = null;
    };
  }, []);

  useEffect(() => {
    sessionRef.current?.setMode(mode);
  }, [mode]);

  const filteredRecords = useMemo(() => {
    const needle = search.trim().toLowerCase();
    return records.filter((record) => {
      if (record.traceKind !== 'logical') return false;
      if (hideClockSync && record.command === 'performance.clock-sync') return false;
      if (directionFilter !== 'all' && record.direction !== directionFilter) return false;
      if (!needle) return true;
      return [
        record.command,
        record.typeName,
        record.name,
        record.transactionId,
        record.sequence,
        record.status,
        record.errorCode,
        record.errorMessage,
      ].some((value) => String(value ?? '').toLowerCase().includes(needle));
    });
  }, [directionFilter, hideClockSync, records, search]);

  const selected = records.find((record) => record.recordId === selectedId) ?? null;
  const selectedFrames = useMemo(
    () => relatedWebHidTraceFrames(records, selected),
    [records, selected],
  );
  const requestCount = useMemo(
    () => records.filter((record) => record.traceKind === 'logical').length,
    [records],
  );
  const activeSources = Object.values(sources).filter((source) => {
    const reportedAt = Date.parse(source.reportedAt);
    return Number.isFinite(reportedAt) && now - reportedAt < SOURCE_ACTIVE_MS;
  });

  function togglePaused() {
    setPaused((current) => {
      pausedRef.current = !current;
      return !current;
    });
  }

  function clearRecords() {
    setRecords([]);
    setSelectedId(null);
  }

  function openWebConfig() {
    window.open(new URL('/global/', window.location.origin), 'hbox-webconfig');
  }

  function exportRecords() {
    const blob = new Blob([JSON.stringify(records, null, 2)], {
      type: 'application/json',
    });
    const url = URL.createObjectURL(blob);
    const anchor = document.createElement('a');
    anchor.href = url;
    anchor.download = `hbox-webhid-trace-${new Date().toISOString().replaceAll(':', '-')}.json`;
    anchor.click();
    URL.revokeObjectURL(url);
  }

  return (
    <Box
      as="main"
      h="100dvh"
      maxH="100dvh"
      overflow="hidden"
      boxSizing="border-box"
      display="flex"
      flexDirection="column"
      gap="7px"
      p="10px"
      color="#d8ffe6"
      bg="#010302"
      backgroundImage="radial-gradient(circle at 8% 0%, rgba(38, 255, 118, 0.11), transparent 30%), radial-gradient(circle at 92% 10%, rgba(0, 255, 102, 0.06), transparent 28%)"
      fontFamily={MONO_FONT}
    >
      <Flex flex="0 0 auto" align="center" justify="space-between" gap="14px" minW={0}>
        <HStack gap="9px" minW={0}>
          <Flex
            w="28px"
            h="28px"
            flex="0 0 auto"
            align="center"
            justify="center"
            border="1px solid"
            borderColor="rgba(38, 255, 118, 0.48)"
            borderRadius="7px"
            color="#26ff76"
            bg="rgba(8, 35, 17, 0.82)"
            boxShadow="0 0 16px rgba(38, 255, 118, 0.12)"
          >
            <LuActivity size={15} />
          </Flex>
          <Box minW={0}>
            <Text color="#26ff76" fontSize="8px" fontWeight="700" letterSpacing="0.16em" lineHeight="1.1">
              LOCAL DIAGNOSTICS
            </Text>
            <Heading as="h1" mt="2px" color="#effff4" fontSize="17px" fontWeight="700" lineHeight="1.15">
              HBox WebHID Trace
            </Heading>
            <Text mt="2px" color="#6f917a" fontFamily="system-ui, sans-serif" fontSize="10px" lineHeight="1.25" truncate>
              Inspect raw WebHID frames and decoded logical packets from a same-origin WebConfig page in real time.
            </Text>
          </Box>
        </HStack>
        <HStack gap="6px" flex="0 0 auto">
          <Button size="xs" colorPalette="green" onClick={openWebConfig}>
            <LuExternalLink />
            Open WebConfig
          </Button>
          <Button size="xs" variant="outline" onClick={exportRecords} disabled={records.length === 0}>
            <LuDownload />
            Export JSON
          </Button>
        </HStack>
      </Flex>

      <Flex
        flex="0 0 auto"
        minH="30px"
        align="center"
        gap="8px"
        px="10px"
        border="1px solid"
        borderColor="rgba(38, 255, 118, 0.18)"
        borderRadius="7px"
        bg="rgba(3, 10, 6, 0.94)"
        aria-live="polite"
      >
        <Box
          w="7px"
          h="7px"
          flex="0 0 auto"
          borderRadius="full"
          bg={activeSources.length > 0 ? '#26ff76' : '#35483b'}
          boxShadow={activeSources.length > 0 ? '0 0 12px rgba(38, 255, 118, .78)' : 'none'}
        />
        <Text fontSize="10px" fontWeight="700" lineHeight="1">
          {!supported
            ? 'Tracing is not available at this address'
            : activeSources.length > 0
              ? `${activeSources.length} WebConfig page${activeSources.length === 1 ? '' : 's'} connected`
              : 'Waiting for a same-origin WebConfig page'}
        </Text>
        <Text ml="auto" color="#506b59" fontFamily="system-ui, sans-serif" fontSize="9px" lineHeight="1" truncate>
          Both pages must use the same hostname and port. Do not mix localhost with 127.0.0.1.
        </Text>
      </Flex>

      <Flex
        flex="0 0 auto"
        align="end"
        gap="7px"
        minW={0}
        p="7px"
        border="1px solid"
        borderColor="rgba(38, 255, 118, 0.18)"
        borderRadius="7px"
        bg="rgba(3, 10, 6, 0.94)"
      >
        <TraceSelect
          label="CAPTURE SCOPE"
          value={mode}
          onChange={(value) => setMode(value as WebHidNetworkTraceMode)}
          options={[
            ['control', 'Control Traffic (Recommended)'],
            ['all', 'All Traffic (Includes Performance)'],
          ]}
        />
        <TraceSelect
          label="DIRECTION"
          value={directionFilter}
          onChange={(value) => setDirectionFilter(value as DirectionFilter)}
          options={[
            ['all', 'TX + RX'],
            ['tx', 'TX'],
            ['rx', 'RX'],
          ]}
        />
        <Box as="label" flex="1 1 220px" minW="150px">
          <Text mb="3px" color="gray.500" fontSize="8px" fontWeight="700" letterSpacing="0.08em" lineHeight="1">
            SEARCH
          </Text>
          <Input
            h="28px"
            px="9px"
            borderColor="rgba(38, 255, 118, 0.26)"
            borderRadius="6px"
            color="#d8ffe6"
            bg="#050b07"
            fontFamily={MONO_FONT}
            fontSize="10px"
            value={search}
            onChange={(event) => setSearch(event.target.value)}
            placeholder="command / type / TID / status"
            _placeholder={{ color: '#405849' }}
            _focus={{ borderColor: '#26ff76', boxShadow: '0 0 0 1px #26ff76' }}
          />
        </Box>
        <Switch.Root
          flex="0 0 auto"
          colorPalette="green"
          size="sm"
          checked={hideClockSync}
          onCheckedChange={(details) => {
            setHideClockSync(details.checked);
            if (
              details.checked
              && records.find((record) => record.recordId === selectedId)?.command === 'performance.clock-sync'
            ) {
              setSelectedId(null);
            }
          }}
        >
          <Switch.HiddenInput />
          <Switch.Control>
            <Switch.Thumb />
          </Switch.Control>
          <Switch.Label color="#7fa98b" fontSize="9px" fontWeight="700" whiteSpace="nowrap">
            Hide Clock Sync
          </Switch.Label>
        </Switch.Root>
        <Button h="28px" minH="28px" size="xs" variant="outline" onClick={togglePaused}>
          {paused ? <LuPlay /> : <LuPause />}
          {paused ? 'Resume' : 'Pause'}
        </Button>
        <Button h="28px" minH="28px" size="xs" variant="outline" onClick={clearRecords} disabled={records.length === 0}>
          <LuTrash2 />
          Clear
        </Button>
      </Flex>

      {(!supported || mode === 'all') && (
        <Box
          flex="0 0 auto"
          px="10px"
          py="5px"
          border="1px solid"
          borderColor="yellow.800"
          borderRadius="6px"
          color="yellow.300"
          bg="rgba(62, 48, 13, 0.35)"
          fontFamily="system-ui, sans-serif"
          fontSize="9px"
          lineHeight="1.25"
        >
          {!supported
            ? 'The Trace page is only available on localhost, 127.0.0.1, or ::1.'
            : 'All Traffic includes high-frequency performance telemetry and may affect timing. Use Control Traffic for routine configuration debugging.'}
        </Box>
      )}

      <Grid
        flex="1 1 0"
        minH={0}
        minW={0}
        gridTemplateColumns="minmax(0, 1.45fr) minmax(320px, 1fr)"
        overflow="hidden"
        border="1px solid"
        borderColor="rgba(38, 255, 118, 0.18)"
        borderRadius="8px"
        bg="rgba(2, 8, 4, 0.96)"
      >
        <Flex minW={0} minH={0} flexDirection="column" borderRight="1px solid" borderColor="rgba(38, 255, 118, 0.18)">
          <PanelTitle title="REQUESTS" meta={`${filteredRecords.length} / ${requestCount}`} />
          <Box flex="1 1 0" minH={0} overflow="auto" css={{ scrollbarGutter: 'stable' }}>
            <Table.Root size="sm" minW="760px" fontFamily={MONO_FONT} fontSize="10px" interactive>
              <Table.Header>
                <Table.Row bg="#061009">
                  {['#', 'TIME', 'STATUS', 'DIRECTION', 'TYPE', 'COMMAND', 'TID', 'LENGTH'].map((label) => (
                    <Table.ColumnHeader
                      key={label}
                      position="sticky"
                      top={0}
                      zIndex={1}
                      h="30px"
                      px="8px"
                      borderColor="rgba(38, 255, 118, 0.16)"
                      color="#63816c"
                      bg="#061009"
                      fontSize="9px"
                      whiteSpace="nowrap"
                    >
                      {label}
                    </Table.ColumnHeader>
                  ))}
                </Table.Row>
              </Table.Header>
              <Table.Body>
                {filteredRecords.map((record) => {
                  const status = traceStatus(record);
                  const selectedRow = record.recordId === selectedId;
                  return (
                    <Table.Row
                      key={record.recordId}
                      cursor="pointer"
                      bg={selectedRow ? 'rgba(38, 255, 118, 0.11)' : 'transparent'}
                      _hover={{ bg: selectedRow ? 'rgba(38, 255, 118, 0.15)' : 'rgba(38, 255, 118, 0.055)' }}
                      onClick={() => setSelectedId(record.recordId)}
                    >
                      <TraceCell>{record.captureSequence}</TraceCell>
                      <TraceCell>{formatTraceTime(record.capturedAt)}</TraceCell>
                      <TraceCell>
                        <Badge size="sm" variant="subtle" colorPalette={status.colorPalette} fontSize="8px">
                          {status.label}
                        </Badge>
                      </TraceCell>
                      <TraceCell>
                        <Badge
                          size="sm"
                          variant="subtle"
                          colorPalette="green"
                          fontSize="8px"
                        >
                          {record.direction.toUpperCase()}
                        </Badge>
                      </TraceCell>
                      <TraceCell>{record.typeName}</TraceCell>
                      <TraceCell maxW="220px" color="#d8ffe6" fontWeight="700">
                        {String(record.command ?? record.typeName)}
                      </TraceCell>
                      <TraceCell>{String(record.transactionId ?? '—')}</TraceCell>
                      <TraceCell>{String(record.plaintextLength ?? record.payloadLength ?? '—')}</TraceCell>
                    </Table.Row>
                  );
                })}
                {filteredRecords.length === 0 && (
                  <Table.Row>
                    <Table.Cell colSpan={8} h="160px" color="gray.600" fontFamily="system-ui, sans-serif" fontSize="11px" textAlign="center">
                      {activeSources.length > 0
                        ? 'Connected. Use the device from WebConfig to generate WebHID traffic.'
                        : 'Open WebConfig, connect the device, and keep both pages open.'}
                    </Table.Cell>
                  </Table.Row>
                )}
              </Table.Body>
            </Table.Root>
          </Box>
        </Flex>

        <Flex minW={0} minH={0} flexDirection="column" bg="#030805">
          <PanelTitle title="DATA VIEW" meta={selected ? String(selected.command ?? selected.typeName) : 'NO SELECTION'} />
          <Tabs.Root
            defaultValue="json"
            flex="1 1 0"
            minH={0}
            display="flex"
            flexDirection="column"
            colorPalette="green"
            variant="line"
          >
            <Tabs.List flex="0 0 32px" minH="32px" px="10px" borderBottomColor="rgba(38, 255, 118, 0.16)">
              <Tabs.Trigger value="json" h="31px" px="9px" color="#63816c" fontSize="9px" fontWeight="700">
                JSON
              </Tabs.Trigger>
              <Tabs.Trigger value="raw" h="31px" px="9px" color="#63816c" fontSize="9px" fontWeight="700">
                HID RAW
                {selected && (
                  <Badge ml="3px" size="sm" colorPalette="green" variant="subtle" fontSize="7px">
                    {selectedFrames.length}
                  </Badge>
                )}
              </Tabs.Trigger>
            </Tabs.List>
            <Tabs.Content
              value="json"
              flex="1 1 0"
              minH={0}
              overflow="auto"
              p="12px"
              css={{ scrollbarGutter: 'stable' }}
            >
              {selected ? (
                selected.decoded !== undefined ? (
                  <Box
                    as="pre"
                    m={0}
                    color={traceStatus(selected).value === 'failed' ? 'red.200' : '#bdfbce'}
                    fontFamily={MONO_FONT}
                    fontSize="10px"
                    lineHeight="1.55"
                    overflowWrap="anywhere"
                    whiteSpace="pre-wrap"
                  >
                    {formatJson(decodedDetail(selected))}
                  </Box>
                ) : (
                  <Text color="gray.600" fontFamily="system-ui, sans-serif" fontSize="11px">
                    No decoded JSON is available for this request.
                  </Text>
                )
              ) : (
                <Text color="gray.600" fontFamily="system-ui, sans-serif" fontSize="11px">
                  Select a request on the left to inspect its decoded JSON.
                </Text>
              )}
            </Tabs.Content>
            <Tabs.Content
              value="raw"
              flex="1 1 0"
              minH={0}
              overflow="auto"
              p="12px"
              css={{ scrollbarGutter: 'stable' }}
            >
              {selected ? (
                <RawFramesDetail frames={selectedFrames} />
              ) : (
                <Text color="gray.600" fontFamily="system-ui, sans-serif" fontSize="11px">
                  Select a request on the left to inspect its HID frames.
                </Text>
              )}
            </Tabs.Content>
          </Tabs.Root>
        </Flex>
      </Grid>

      <Text flex="0 0 auto" color="#35483b" fontFamily="system-ui, sans-serif" fontSize="8px" lineHeight="1" truncate>
        Traces may contain device authorization material and plaintext configuration. Do not share exported JSON or HAR files. Filter DevTools Network by __hbox_webhid_trace__.
      </Text>
    </Box>
  );
}

function RawFramesDetail({ frames }: { frames: WebHidTraceRecord[] }) {
  if (frames.length === 0) {
    return (
      <Text color="gray.600" fontFamily="system-ui, sans-serif" fontSize="11px">
        No linked HID frames were captured for this request.
      </Text>
    );
  }

  return (
    <Flex flexDirection="column" gap="18px">
      {frames.map((frame, index) => (
        <Box key={frame.recordId}>
          <Flex align="center" justify="space-between" gap="8px" mb="7px">
            <Text color="#26ff76" fontSize="9px" fontWeight="700" letterSpacing="0.08em">
              {frame.direction === 'tx' ? 'REQUEST' : 'RESPONSE'} FRAME {index + 1}
            </Text>
            <Text color="#496251" fontSize="8px">
              SEQ {String(frame.sequence ?? '—')}
            </Text>
          </Flex>
          <RawFrameDetail record={frame} />
        </Box>
      ))}
    </Flex>
  );
}

function RawFrameDetail({ record }: { record: WebHidTraceRecord }) {
  const wireBytes = parseHexBytes(record.wireHex);
  const plaintextBytes = parseHexBytes(record.plaintextHex);
  const readablePayload = typeof record.plaintextUtf8 === 'string'
    ? record.plaintextUtf8
    : null;

  return (
    <Flex flexDirection="column" gap="12px">
      <Grid gridTemplateColumns="repeat(2, minmax(0, 1fr))" gap="6px">
        <FrameField label="DIRECTION" value={String(record.direction).toUpperCase()} />
        <FrameField label="REPORT ID" value={formatHexValue(record.reportId)} />
        <FrameField label="FRAME TYPE" value={`${String(record.typeName)} (${formatHexValue(record.type)})`} />
        <FrameField label="SEQUENCE" value={String(record.sequence ?? '—')} />
        <FrameField label="VERSION" value={formatHexValue(record.version)} />
        <FrameField label="FLAGS" value={formatFrameFlags(record)} />
        <FrameField label="SECURE" value={record.secure === true ? 'YES' : 'NO'} />
        <FrameField label="PAYLOAD LENGTH" value={`${String(record.payloadLength ?? plaintextBytes.length)} bytes`} />
      </Grid>

      <HexSection
        title="WIRE REPORT"
        description="Raw bytes transferred through the HID report, before decryption."
        bytes={wireBytes}
      />
      <HexSection
        title="PLAINTEXT PAYLOAD"
        description="Frame payload after transport decryption."
        bytes={plaintextBytes}
      />

      {readablePayload !== null && (
        <Box>
          <Text color="#63816c" fontSize="8px" fontWeight="700" letterSpacing="0.09em">
            UTF-8 PAYLOAD
          </Text>
          <Box
            as="pre"
            mt="4px"
            mb={0}
            p="9px"
            overflowWrap="anywhere"
            border="1px solid rgba(38, 255, 118, 0.14)"
            borderRadius="6px"
            color="#bdfbce"
            bg="#020503"
            fontFamily={MONO_FONT}
            fontSize="10px"
            lineHeight="1.5"
            whiteSpace="pre-wrap"
          >
            {readablePayload || '(empty)'}
          </Box>
        </Box>
      )}
    </Flex>
  );
}

function FrameField({ label, value }: { label: string; value: string }) {
  return (
    <Box
      minW={0}
      px="8px"
      py="6px"
      border="1px solid rgba(38, 255, 118, 0.12)"
      borderRadius="5px"
      bg="#020503"
    >
      <Text color="#4f6b58" fontSize="7px" fontWeight="700" letterSpacing="0.08em">
        {label}
      </Text>
      <Text mt="2px" color="#bdfbce" fontSize="9px" fontWeight="700" truncate>
        {value}
      </Text>
    </Box>
  );
}

function HexSection({
  title,
  description,
  bytes,
}: {
  title: string;
  description: string;
  bytes: number[];
}) {
  return (
    <Box>
      <Flex align="baseline" justify="space-between" gap="8px">
        <Text color="#63816c" fontSize="8px" fontWeight="700" letterSpacing="0.09em">
          {title}
        </Text>
        <Text color="#405849" fontSize="8px">
          {bytes.length} bytes
        </Text>
      </Flex>
      <Text mt="2px" color="#405849" fontFamily="system-ui, sans-serif" fontSize="8px" lineHeight="1.25">
        {description}
      </Text>
      <Box
        as="pre"
        mt="5px"
        mb={0}
        minH="36px"
        p="9px"
        overflowX="auto"
        border="1px solid rgba(38, 255, 118, 0.14)"
        borderRadius="6px"
        color="#9fffb9"
        bg="#020503"
        fontFamily={MONO_FONT}
        fontSize="9px"
        lineHeight="1.55"
        whiteSpace="pre"
      >
        {bytes.length > 0 ? formatHexDump(bytes) : '(empty)'}
      </Box>
    </Box>
  );
}

function TraceSelect({
  label,
  value,
  options,
  onChange,
}: {
  label: string;
  value: string;
  options: ReadonlyArray<readonly [string, string]>;
  onChange: (value: string) => void;
}) {
  return (
    <Box as="label" flex="0 0 auto">
      <Text mb="3px" color="#63816c" fontSize="8px" fontWeight="700" letterSpacing="0.08em" lineHeight="1">
        {label}
      </Text>
      <chakra.select
        h="28px"
        minW="132px"
        px="8px"
        border="1px solid"
        borderColor="rgba(38, 255, 118, 0.26)"
        borderRadius="6px"
        color="#d8ffe6"
        bg="#050b07"
        fontFamily={MONO_FONT}
        fontSize="10px"
        outline="none"
        value={value}
        onChange={(event) => onChange(event.currentTarget.value)}
        _focus={{ borderColor: '#26ff76' }}
      >
        {options.map(([optionValue, optionLabel]) => (
          <option key={optionValue} value={optionValue}>{optionLabel}</option>
        ))}
      </chakra.select>
    </Box>
  );
}

function PanelTitle({ title, meta }: { title: string; meta: string }) {
  return (
    <Flex flex="0 0 31px" minW={0} align="center" justify="space-between" gap="8px" px="10px" borderBottom="1px solid" borderColor="rgba(38, 255, 118, 0.16)">
      <Text color="#7fa98b" fontSize="9px" fontWeight="700" letterSpacing="0.08em" lineHeight="1">
        {title}
      </Text>
      <Text minW={0} color="#496251" fontSize="8px" lineHeight="1" truncate>
        {meta}
      </Text>
    </Flex>
  );
}

function TraceCell({ children, ...props }: React.ComponentProps<typeof Table.Cell>) {
  return (
    <Table.Cell
      h="31px"
      maxW="150px"
      px="8px"
      py="4px"
      overflow="hidden"
      borderColor="rgba(38, 255, 118, .11)"
      color="#7fa98b"
      textOverflow="ellipsis"
      whiteSpace="nowrap"
      {...props}
    >
      {children}
    </Table.Cell>
  );
}

function traceStatus(record: WebHidTraceRecord): StatusPresentation {
  const explicit = record.status;
  if (explicit === 'pending') return { value: explicit, label: 'PENDING', colorPalette: 'yellow' };
  if (explicit === 'failed') return { value: explicit, label: 'FAILED', colorPalette: 'red' };
  if (explicit === 'event') return { value: explicit, label: 'EVENT', colorPalette: 'purple' };
  if (explicit === 'captured') return { value: explicit, label: 'CAPTURED', colorPalette: 'gray' };
  if (explicit === 'success') return { value: explicit, label: 'SUCCESS', colorPalette: 'green' };

  if (record.traceKind === 'frame') {
    return record.typeName === 'ERROR'
      ? { value: 'failed', label: 'FAILED', colorPalette: 'red' }
      : { value: 'captured', label: 'CAPTURED', colorPalette: 'gray' };
  }
  if (record.typeName === 'ERROR') return { value: 'failed', label: 'FAILED', colorPalette: 'red' };
  if (record.typeName === 'EVENT') return { value: 'event', label: 'EVENT', colorPalette: 'purple' };
  if (record.direction === 'tx') return { value: 'pending', label: 'PENDING', colorPalette: 'yellow' };
  const decoded = record.decoded as { errNo?: unknown } | undefined;
  if (typeof decoded?.errNo === 'number' && decoded.errNo !== 0) {
    return { value: 'failed', label: 'FAILED', colorPalette: 'red' };
  }
  return { value: 'success', label: 'SUCCESS', colorPalette: 'green' };
}

function decodedDetail(record: WebHidTraceRecord): unknown {
  const hasResponse = record.responseDecoded !== undefined;
  const hasError = typeof record.errorCode === 'string' || typeof record.errorMessage === 'string';
  if (!hasResponse && !hasError) return record.decoded;
  return {
    request: record.decoded,
    ...(hasResponse ? { response: record.responseDecoded } : {}),
    ...(hasError
      ? {
        error: {
          code: record.errorCode ?? null,
          message: record.errorMessage ?? null,
        },
      }
      : {}),
  };
}

function formatTraceTime(value: string): string {
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return value;
  return date.toLocaleTimeString(undefined, {
    hour12: false,
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
    fractionalSecondDigits: 3,
  });
}

function formatJson(value: unknown): string {
  try {
    return JSON.stringify(value, null, 2);
  } catch {
    return String(value);
  }
}

function parseHexBytes(value: unknown): number[] {
  if (typeof value !== 'string' || value.trim() === '') return [];
  return value
    .trim()
    .split(/\s+/)
    .map((part) => Number.parseInt(part, 16))
    .filter((byte) => Number.isInteger(byte) && byte >= 0 && byte <= 0xff);
}

function formatHexDump(bytes: number[]): string {
  const lines: string[] = [];
  for (let offset = 0; offset < bytes.length; offset += 16) {
    const chunk = bytes.slice(offset, offset + 16);
    const hex = chunk
      .map((byte) => byte.toString(16).padStart(2, '0'))
      .join(' ')
      .padEnd(16 * 3 - 1, ' ');
    const ascii = chunk
      .map((byte) => (byte >= 0x20 && byte <= 0x7e ? String.fromCharCode(byte) : '.'))
      .join('');
    lines.push(`${offset.toString(16).padStart(4, '0')}  ${hex}  |${ascii}|`);
  }
  return lines.join('\n');
}

function formatHexValue(value: unknown): string {
  return typeof value === 'number' && Number.isInteger(value)
    ? `0x${value.toString(16).padStart(2, '0').toUpperCase()}`
    : '—';
}

function formatFrameFlags(record: WebHidTraceRecord): string {
  const names = Array.isArray(record.flagNames)
    ? record.flagNames.filter((name): name is string => typeof name === 'string')
    : [];
  const numeric = formatHexValue(record.flags);
  return names.length > 0 ? `${numeric} · ${names.join(' | ')}` : numeric;
}
