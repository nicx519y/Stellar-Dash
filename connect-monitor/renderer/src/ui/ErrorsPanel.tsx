import { Badge, Card, HStack, Table, Text } from "@chakra-ui/react";

import type { ErrorEvent } from "../../../shared/monitor-types";

function fmtTime(ms: number) {
  const d = new Date(ms);
  return d.toLocaleTimeString() + "." + String(d.getMilliseconds()).padStart(3, "0");
}

function levelColor(level: string) {
  if (level === "FATAL") return "red";
  if (level === "ERROR") return "red";
  if (level === "WARN") return "yellow";
  return "gray";
}

export function ErrorsPanel({ items }: { items: Array<ErrorEvent & { id?: string }> }) {
  const rows = items.slice(-100).reverse();

  return (
    <Card.Root variant="outline" bg="rgba(255,255,255,0.02)" borderColor="rgba(255,255,255,0.08)">
      <Card.Header>
        <HStack justify="space-between">
          <Text fontSize="sm" color="gray.400">
            最近 {rows.length} / 100
          </Text>
        </HStack>
      </Card.Header>
      <Card.Body p={0}>
        <Table.ScrollArea maxH="560px">
          <Table.Root size="sm" variant="line" stickyHeader striped>
            <Table.Header>
              <Table.Row>
                <Table.ColumnHeader color="gray.300">时间</Table.ColumnHeader>
                <Table.ColumnHeader color="gray.300">级别</Table.ColumnHeader>
                <Table.ColumnHeader color="gray.300">来源</Table.ColumnHeader>
                <Table.ColumnHeader color="gray.300">代码</Table.ColumnHeader>
                <Table.ColumnHeader color="gray.300">消息</Table.ColumnHeader>
              </Table.Row>
            </Table.Header>
            <Table.Body>
              {rows.map((e, idx) => (
                <Table.Row key={(e as any).id ?? `${e.timestampMs}-${idx}`}>
                  <Table.Cell color="gray.200">{fmtTime(e.timestampMs)}</Table.Cell>
                  <Table.Cell>
                    <Badge colorPalette={levelColor(e.level)}>{e.level}</Badge>
                  </Table.Cell>
                  <Table.Cell color="gray.200">{e.source}</Table.Cell>
                  <Table.Cell color="gray.200">{e.code}</Table.Cell>
                  <Table.Cell color="gray.200">{e.message}</Table.Cell>
                </Table.Row>
              ))}
            </Table.Body>
          </Table.Root>
        </Table.ScrollArea>
      </Card.Body>
    </Card.Root>
  );
}
