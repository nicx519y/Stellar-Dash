import { Badge, Card, HStack, Table, Text } from "@chakra-ui/react";

import type { PacketEvent } from "../../../shared/monitor-types";

function fmtTime(ms: number) {
  const d = new Date(ms);
  return d.toLocaleTimeString() + "." + String(d.getMilliseconds()).padStart(3, "0");
}

function dirColor(dir: string) {
  return dir === "TX" ? "blue" : "purple";
}

export function PacketsPanel({ items }: { items: Array<PacketEvent & { id?: string }> }) {
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
                <Table.ColumnHeader color="gray.300">通道</Table.ColumnHeader>
                <Table.ColumnHeader color="gray.300">方向</Table.ColumnHeader>
                <Table.ColumnHeader color="gray.300">类型</Table.ColumnHeader>
                <Table.ColumnHeader color="gray.300" textAlign="end">
                  长度
                </Table.ColumnHeader>
                <Table.ColumnHeader color="gray.300" textAlign="end">
                  Seq
                </Table.ColumnHeader>
              </Table.Row>
            </Table.Header>
            <Table.Body>
              {rows.map((p, idx) => (
                <Table.Row key={(p as any).id ?? `${p.timestampMs}-${idx}`}>
                  <Table.Cell color="gray.200">{fmtTime(p.timestampMs)}</Table.Cell>
                  <Table.Cell>
                    <Badge colorPalette={p.channel === "USB" ? "green" : "orange"}>{p.channel}</Badge>
                  </Table.Cell>
                  <Table.Cell>
                    <Badge colorPalette={dirColor(p.direction)}>{p.direction}</Badge>
                  </Table.Cell>
                  <Table.Cell color="gray.200">{p.messageType}</Table.Cell>
                  <Table.Cell color="gray.200" textAlign="end">
                    {p.payloadLen}
                  </Table.Cell>
                  <Table.Cell color="gray.200" textAlign="end">
                    {typeof p.seq === "number" ? p.seq : "-"}
                  </Table.Cell>
                </Table.Row>
              ))}
            </Table.Body>
          </Table.Root>
        </Table.ScrollArea>
      </Card.Body>
    </Card.Root>
  );
}
