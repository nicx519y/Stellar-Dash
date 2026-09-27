'use client';
import { Box, Stack, Text, Table } from '@chakra-ui/react';
import type { FirmwareReleaseManifest } from '@/lib/admin/firmware-types';

export function FirmwareReleaseDetails({ manifest, zh }: { manifest: FirmwareReleaseManifest; zh: boolean }) {
  return <Stack gap="4">
    <Text color="fg.muted">{zh ? '硬件版本' : 'Hardware'} {manifest.hardwareVersion} · {manifest.bootSecurityMode}</Text>
    <Box overflowX="auto"><Table.Root size="sm"><Table.Header><Table.Row>
      {[zh ? '组件' : 'Component', zh ? '版本 / 构建' : 'Version / Build', zh ? '大小' : 'Size', 'SHA-256'].map(t => <Table.ColumnHeader key={t}>{t}</Table.ColumnHeader>)}
    </Table.Row></Table.Header><Table.Body>{manifest.artifacts.map(a => <Table.Row key={`${a.component}-${a.slot || ''}`}>
      <Table.Cell whiteSpace="nowrap">{a.component === 'stm32' ? `STM32 ${a.slot}` : a.component === 'tx' ? (zh ? 'TX · USB 与无线' : 'TX · USB & radio') : (zh ? 'RX · 接收器' : 'RX · Receiver')}</Table.Cell>
      <Table.Cell>{a.version}<Text fontSize="xs" color="fg.muted">{a.buildId}</Text></Table.Cell>
      <Table.Cell whiteSpace="nowrap">{(a.size / 1024).toFixed(1)} KiB</Table.Cell>
      <Table.Cell fontFamily="mono" fontSize="xs" minW="180px" maxW="280px" overflowWrap="anywhere">{a.sha256}</Table.Cell>
    </Table.Row>)}</Table.Body></Table.Root></Box>
    <Box bg="bg.muted" p="3" borderRadius="md"><Text fontWeight="semibold">{zh ? '发布包兼容声明' : 'Package compatibility declarations'}</Text>
      <Text whiteSpace="pre-wrap" overflowWrap="anywhere">STM32 ↔ TX: {manifest.compatibility.stm32Tx}</Text>
      <Text whiteSpace="pre-wrap" overflowWrap="anywhere">TX ↔ RX: {manifest.compatibility.txRx}</Text>
      <Text mt="2" fontSize="sm" color="fg.muted">{zh ? '仅展示版本信息，不代表已验证当前设备的升级路径。' : 'Version information only; this does not verify an update path for your device.'}</Text>
    </Box>
  </Stack>;
}
