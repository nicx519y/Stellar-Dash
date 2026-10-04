'use client';
import { Box, HStack, Stack, Text } from '@chakra-ui/react';
import type { PublicFirmwareRelease } from '@/lib/admin/firmware-types';

export const firmwareNotesStyle = {
  fontFamily: 'system-ui, -apple-system, "Segoe UI", "Microsoft YaHei", sans-serif',
  fontSize: '13px',
  fontWeight: 'normal',
  lineHeight: '1.65',
  letterSpacing: 'normal',
} as const;

export function displayFirmwareNotes(notes: string, zh: boolean): string {
  return notes.split(/\r\n?|\n/).filter(line => line.trim().length > 0).join('\n') ||
    (zh ? '此版本暂无更新说明。' : 'No release notes for this version.');
}

export function FirmwareReleaseInfo({ release, zh }: { release: PublicFirmwareRelease; zh: boolean }) {
  const manifest = release.manifest;
  const components = [
    { key: 'stm32', label: zh ? '主控' : 'Main Controller' },
    { key: 'tx', label: 'TX' },
  ].map(component => ({
    ...component,
    artifacts: manifest.artifacts.filter(artifact => artifact.component === component.key),
  })).filter(component => component.artifacts.length > 0);
  return <Stack gap="5" minW={0}>
    <Box>
      <Text fontSize="12px" fontWeight="semibold" color="fg.muted" mb="2">{zh ? '更新说明' : 'Release notes'}</Text>
      <Text {...firmwareNotesStyle} color="fg.muted" whiteSpace="pre-wrap" overflowWrap="anywhere">
        {displayFirmwareNotes(release.notes, zh)}
      </Text>
    </Box>
    <Stack gap="3" pt="4" borderTopWidth="1px">
      <Text fontSize="16px" fontWeight="semibold">{zh ? '包含组件' : 'Included components'}</Text>
      {components.map(component => <HStack key={component.key} justify="space-between" align="start" gap="4" fontSize="14px">
        <Text>{component.label}</Text>
        <Text textAlign="end">{[...new Set(component.artifacts.map(artifact => artifact.version))].join(' / ')}
          <Text as="span" color="fg.muted"> · {(component.artifacts.reduce((size, artifact) => size + artifact.size, 0) / 1024).toFixed(1)} KiB</Text>
        </Text>
      </HStack>)}
      {release.publishedAt && <Text fontSize="12px" color="fg.muted">{zh ? '发布时间' : 'Published'}: {new Date(release.publishedAt).toLocaleString(zh ? 'zh-CN' : 'en-US')}</Text>}
    </Stack>
    <Box as="details" borderTopWidth="1px" pt="4" fontSize="12px" color="fg.muted">
      <Box as="summary" cursor="pointer" fontWeight="semibold">{zh ? '构建与校验信息' : 'Build and checksums'}</Box>
      <Stack mt="3" gap="3">
        {components.map(component => <Box key={component.key} overflowWrap="anywhere">
          <Text fontWeight="semibold">{component.label} · {[...new Set(component.artifacts.map(artifact => artifact.buildId))].join(' / ')}</Text>
          {component.artifacts.map((artifact, index) => <Text key={`${artifact.sha256}-${index}`} fontFamily="mono">SHA-256: {artifact.sha256}</Text>)}
        </Box>)}
        {manifest.install && <Text>{zh ? '安装协议' : 'Installer protocol'} {manifest.install.protocol} · {zh ? '配置版本' : 'Configuration version'} {manifest.install.configRead.min}–{manifest.install.configRead.max} → {manifest.install.configWrite}</Text>}
      </Stack>
    </Box>
  </Stack>;
}
