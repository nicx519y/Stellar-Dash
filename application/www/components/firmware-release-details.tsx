'use client';
import { Box, Flex, Heading, HStack, Stack, Text, Table } from '@chakra-ui/react';
import type { FirmwareReleaseManifest } from '@/lib/admin/firmware-types';

export function FirmwareReleaseDetails({ manifest, zh }: { manifest: FirmwareReleaseManifest; zh: boolean }) {
  return <Stack gap={6} minW="0" fontSize="sm" lineHeight="1.7" color="fg">
    <Flex wrap="wrap" align="center" gap={3}>
      <HStack gap={2}><Text color="fg.muted">{zh ? '硬件版本' : 'Hardware'}</Text><Text fontWeight="medium">{manifest.hardwareVersion}</Text></HStack>
      <Text fontFamily="mono" fontSize="xs" color="fg.muted" bg="bg.subtle" px={3} py={1} borderRadius="md" overflowWrap="anywhere">{manifest.bootSecurityMode}</Text>
    </Flex>
    <Stack gap={4} minW="0">
      <Stack gap={2}>
        <Heading as="h2" fontSize="lg" fontWeight="semibold" lineHeight="1.4">{zh ? '固件组件' : 'Firmware components'}</Heading>
        <Text color="fg.muted">{manifest.schemaVersion === 2 && manifest.install
          ? (zh ? '可安装发布包 · 连接设备后检查兼容性' : 'Installable package · device compatibility check required')
          : (zh ? '仅供浏览 · 需要重新打包、签名和验收' : 'Catalog only · requires repackaging, signing and acceptance')}</Text>
      </Stack>
      {manifest.install && <Flex wrap="wrap" gap={{ base: 3, md: 6 }} fontSize="xs">
        {[
          [zh ? '升级协议' : 'Installer protocol', manifest.install.protocol],
          [zh ? '配置读取范围' : 'Readable configuration', `${manifest.install.configRead.min}–${manifest.install.configRead.max}`],
          [zh ? '安装后配置版本' : 'Installed configuration', manifest.install.configWrite],
        ].map(([label, value]) => <HStack key={label} gap={2}><Text color="fg.muted">{label}</Text><Text fontWeight="medium">{value}</Text></HStack>)}
      </Flex>}
      <Box overflowX="auto" borderWidth="1px" borderColor="app.border" borderRadius="lg">
        <Table.Root size="sm" minW="720px" tableLayout="fixed">
          <Table.Header><Table.Row bg="bg.subtle">
            {[zh ? '组件' : 'Component', zh ? '版本 / 构建' : 'Version / Build', zh ? '大小' : 'Size', 'SHA-256'].map((t, index) => (
              <Table.ColumnHeader key={t} width={['14%', '37%', '13%', '36%'][index]} px={4} py={3} fontSize="xs" fontWeight="medium" color="fg.muted">{t}</Table.ColumnHeader>
            ))}
          </Table.Row></Table.Header>
          <Table.Body>{manifest.artifacts.map(a => <Table.Row key={`${a.component}-${a.slot || ''}`}>
            <Table.Cell px={4} py={4} fontSize="sm" fontWeight="medium">{a.component === 'stm32' ? `${zh ? '主控' : 'Main controller'} ${a.slot}` : a.component === 'tx' ? (zh ? 'TX · USB 与无线' : 'TX · USB & radio') : (zh ? 'RX · 接收器' : 'RX · Receiver')}</Table.Cell>
            <Table.Cell px={4} py={4}><Stack gap={1.5}><Text fontSize="sm" fontWeight="medium">{a.version}</Text><Text fontSize="xs" fontFamily="mono" lineHeight="1.6" color="fg.muted" overflowWrap="anywhere">{a.buildId}</Text></Stack></Table.Cell>
            <Table.Cell px={4} py={4} fontSize="sm" whiteSpace="nowrap">{(a.size / 1024).toFixed(1)} KiB</Table.Cell>
            <Table.Cell px={4} py={4} fontFamily="mono" fontSize="xs" lineHeight="1.7" color="fg.muted" overflowWrap="anywhere">{a.sha256}</Table.Cell>
          </Table.Row>)}</Table.Body>
        </Table.Root>
      </Box>
    </Stack>
    <Stack gap={4} bg="bg.subtle" p={{ base: 4, md: 5 }} borderWidth="1px" borderColor="app.border" borderRadius="lg">
      <Heading as="h2" fontSize="md" fontWeight="semibold" lineHeight="1.4">{zh ? '发布包兼容声明' : 'Package compatibility declarations'}</Heading>
      <Stack gap={3}>
        <Box><Text fontSize="xs" color="fg.muted" mb={1}>{zh ? '主控' : 'Main controller'} ↔ TX</Text><Text whiteSpace="pre-wrap" overflowWrap="anywhere">{manifest.compatibility.stm32Tx}</Text></Box>
        <Box><Text fontSize="xs" color="fg.muted" mb={1}>TX ↔ RX</Text><Text whiteSpace="pre-wrap" overflowWrap="anywhere">{manifest.compatibility.txRx}</Text></Box>
      </Stack>
      <Text fontSize="xs" color="fg.muted" lineHeight="1.7">{zh ? '仅展示版本信息，不代表已验证当前设备的升级路径。' : 'Version information only; this does not verify an update path for your device.'}</Text>
    </Stack>
  </Stack>;
}
