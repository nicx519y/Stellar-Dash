import { Box, HStack } from "@chakra-ui/react";
import * as React from "react";

import type { SerialLogLine, SerialPortInfo } from "../../../shared/monitor-types";
import { VirtualTable, type VirtualColumn } from "./VirtualTable";
import { appendSerialLogLines, loadSerialLogLines } from "./serialLogStore";

const LOG_SLOT_COUNT = 2;
const MAX_LOG_ROWS = 500;

function fmtTime(ms: number) {
  const d = new Date(ms);
  return d.toLocaleTimeString() + "." + String(d.getMilliseconds()).padStart(3, "0");
}

function isSerialLogLine(value: unknown): value is SerialLogLine {
  if (!value || typeof value !== "object") return false;
  const line = value as Partial<SerialLogLine>;
  return (
    typeof line.id === "string" &&
    typeof line.timestampMs === "number" &&
    typeof line.portPath === "string" &&
    typeof line.text === "string"
  );
}

function normalizeSelections(selections: string[]): string[] {
  return Array.from({ length: LOG_SLOT_COUNT }, (_unused, slot) => selections[slot] ?? "");
}

function appendRows(current: SerialLogLine[] | undefined, lines: SerialLogLine[]) {
  return [...(current ?? []), ...lines].slice(-MAX_LOG_ROWS);
}

function buildLogFilter(pattern: string): { regex: RegExp | null; invalid: boolean } {
  const trimmed = pattern.trim();
  if (!trimmed) return { regex: null, invalid: false };

  try {
    if (trimmed.startsWith("/") && trimmed.lastIndexOf("/") > 0) {
      const lastSlash = trimmed.lastIndexOf("/");
      return {
        regex: new RegExp(trimmed.slice(1, lastSlash), trimmed.slice(lastSlash + 1)),
        invalid: false,
      };
    }
    return { regex: new RegExp(trimmed), invalid: false };
  } catch {
    return { regex: null, invalid: true };
  }
}

function matchesLogFilter(line: SerialLogLine, regex: RegExp | null) {
  if (!regex) return true;
  regex.lastIndex = 0;
  return regex.test(line.text);
}

function RegexFilterInput({
  slot,
  value,
  invalid,
  onChange,
}: {
  slot: number;
  value: string;
  invalid: boolean;
  onChange: (value: string) => void;
}) {
  return (
    <input
      value={value}
      title={invalid ? "Invalid regex" : "Regex filter"}
      aria-label={`Log ${slot + 1} regex filter`}
      placeholder="Regex"
      onChange={(event) => onChange(event.currentTarget.value)}
      style={{
        width: "150px",
        height: "28px",
        padding: "0 8px",
        borderWidth: "1px",
        borderStyle: "solid",
        borderRadius: "6px",
        borderColor: invalid ? "rgba(255,96,96,0.72)" : "rgba(92,255,138,0.34)",
        background: "rgba(8,18,16,0.86)",
        color: "rgba(245,255,248,0.92)",
        fontSize: "12px",
        outline: "none",
      }}
    />
  );
}

function PortSelector({
  ports,
  selectedPort,
  onPortChange,
}: {
  ports: SerialPortInfo[];
  selectedPort: string;
  onPortChange: (portPath: string) => void;
}) {
  const hasSelectedPort = selectedPort && !ports.some((port) => port.path === selectedPort);
  const options = hasSelectedPort
    ? [...ports, { path: selectedPort, displayName: selectedPort }]
    : ports;

  return (
    <Box
      w={{ base: "150px", md: "190px" }}
      maxW="100%"
    >
      <select
        value={selectedPort}
        title="Select COM port"
        aria-label="Select COM port"
        onChange={(event) => onPortChange(event.currentTarget.value)}
        style={{
          width: "100%",
          height: "28px",
          padding: "0 8px",
          borderWidth: "1px",
          borderStyle: "solid",
          borderRadius: "6px",
          borderColor: "rgba(92,255,138,0.34)",
          background: "rgba(8,18,16,0.86)",
          color: "rgba(245,255,248,0.92)",
          fontSize: "12px",
          outline: "none",
          cursor: "pointer",
        }}
      >
        <option value="">{ports.length === 0 ? "No COM ports" : "Select COM"}</option>
        {options.map((port) => (
          <option key={port.path} value={port.path}>
            {port.displayName}
          </option>
        ))}
      </select>
    </Box>
  );
}

function SerialLogCard({
  slot,
  ports,
  selectedPort,
  items,
  filterPattern,
  onFilterChange,
  onPortChange,
}: {
  slot: number;
  ports: SerialPortInfo[];
  selectedPort: string;
  items: SerialLogLine[];
  filterPattern: string;
  onFilterChange: (slot: number, pattern: string) => void;
  onPortChange: (slot: number, portPath: string) => void;
}) {
  const filter = React.useMemo(() => buildLogFilter(filterPattern), [filterPattern]);
  const recentRows = items.slice(-MAX_LOG_ROWS).reverse();
  const rows = filter.invalid
    ? []
    : recentRows.filter((row) => matchesLogFilter(row, filter.regex));
  const hasFilter = filterPattern.trim().length > 0;
  const countText = !selectedPort
    ? "Select COM"
    : filter.invalid
      ? "Invalid regex"
      : hasFilter
        ? `Matched ${rows.length} / ${recentRows.length}`
        : `Recent ${rows.length} / ${MAX_LOG_ROWS}`;
  const columns = React.useMemo<Array<VirtualColumn<SerialLogLine>>>(
    () => [
      { key: "time", header: "Time", width: "110px", render: (row) => fmtTime(row.timestampMs) },
      { key: "log", header: "Log", width: "calc(100% - 110px)", render: (row) => row.text },
    ],
    [],
  );

  return (
    <VirtualTable
      title={`Log ${slot + 1}`}
      countText={countText}
      action={
        <HStack gap={2}>
          <RegexFilterInput
            slot={slot}
            value={filterPattern}
            invalid={filter.invalid}
            onChange={(pattern) => onFilterChange(slot, pattern)}
          />
          <PortSelector
            ports={ports}
            selectedPort={selectedPort}
            onPortChange={(portPath) => onPortChange(slot, portPath)}
          />
        </HStack>
      }
      items={rows}
      columns={columns}
      rowKey={(row) => row.id}
      maxHeight="100%"
    />
  );
}

export function SerialLogPanel({ clearVersion = 0 }: { clearVersion?: number }) {
  const [ports, setPorts] = React.useState<SerialPortInfo[]>([]);
  const [selections, setSelections] = React.useState<string[]>(() => normalizeSelections([]));
  const [filterPatterns, setFilterPatterns] = React.useState<string[]>(() => normalizeSelections([]));
  const [logsByPort, setLogsByPort] = React.useState<Map<string, SerialLogLine[]>>(() => new Map());

  const refreshPorts = React.useCallback(() => {
    if (!window.connectMonitorApi) {
      setPorts([]);
      return;
    }
    void window.connectMonitorApi.listSerialPorts()
      .then((nextPorts) => setPorts(nextPorts))
      .catch(() => setPorts([]));
  }, []);

  React.useEffect(() => {
    refreshPorts();
    if (window.connectMonitorApi) {
      void window.connectMonitorApi.getSerialLogSelections()
        .then((nextSelections) => setSelections(normalizeSelections(nextSelections)))
        .catch(() => {});
    }
    const timer = window.setInterval(refreshPorts, 3000);
    return () => window.clearInterval(timer);
  }, [refreshPorts]);

  React.useEffect(() => {
    if (!window.connectMonitorApi) return undefined;
    const unsubscribe = window.connectMonitorApi.onSerialLogs((rawLines) => {
      const lines = rawLines.filter(isSerialLogLine);
      if (lines.length === 0) return;

      void appendSerialLogLines(lines).catch(() => {});
      setLogsByPort((current) => {
        const next = new Map(current);
        const grouped = new Map<string, SerialLogLine[]>();
        for (const line of lines) {
          const group = grouped.get(line.portPath) ?? [];
          group.push(line);
          grouped.set(line.portPath, group);
        }
        for (const [portPath, portLines] of grouped) {
          next.set(portPath, appendRows(next.get(portPath), portLines));
        }
        return next;
      });
    });
    return unsubscribe;
  }, []);

  React.useEffect(() => {
    setLogsByPort(new Map());
  }, [clearVersion]);

  React.useEffect(() => {
    let canceled = false;
    const selectedPorts = Array.from(new Set(selections.filter(Boolean)));

    for (const portPath of selectedPorts) {
      void loadSerialLogLines(portPath, MAX_LOG_ROWS)
        .then((rows) => {
          if (canceled) return;
          setLogsByPort((current) => {
            const next = new Map(current);
            next.set(portPath, rows);
            return next;
          });
        })
        .catch(() => {});
    }

    return () => {
      canceled = true;
    };
  }, [selections, clearVersion]);

  const handlePortChange = React.useCallback((slot: number, portPath: string) => {
    setSelections((current) => {
      const next = normalizeSelections(current);
      next[slot] = portPath;
      if (window.connectMonitorApi) {
        void window.connectMonitorApi.setSerialLogSelections(next)
          .then((savedSelections) => setSelections(normalizeSelections(savedSelections)))
          .catch(() => {});
      }
      return next;
    });
  }, []);

  const handleFilterChange = React.useCallback((slot: number, pattern: string) => {
    setFilterPatterns((current) => {
      const next = normalizeSelections(current);
      next[slot] = pattern;
      return next;
    });
  }, []);

  return (
    <Box
      flex="1"
      minH={0}
      display="grid"
      gridTemplateColumns={{ base: "1fr", xl: "repeat(2, minmax(0, 1fr))" }}
      gap="10px"
      alignItems="stretch"
    >
      {selections.map((selectedPort, slot) => (
        <SerialLogCard
          key={slot}
          slot={slot}
          ports={ports}
          selectedPort={selectedPort}
          items={selectedPort ? logsByPort.get(selectedPort) ?? [] : []}
          filterPattern={filterPatterns[slot] ?? ""}
          onFilterChange={handleFilterChange}
          onPortChange={handlePortChange}
        />
      ))}
    </Box>
  );
}
