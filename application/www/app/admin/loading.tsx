'use client';

import { Skeleton, Stack } from '@chakra-ui/react';
import { useLanguage } from '@/contexts/language-context';

// This boundary sits inside AdminLayout: only the page body can be replaced
// while route data loads; the shared header, navigation and account stay mounted.
export default function AdminLoading() {
  const { currentLanguage } = useLanguage();
  return <Stack gap="6" role="status" aria-label={currentLanguage === 'zh' ? '正在加载页面' : 'Loading page'}>
    <Skeleton h="8" maxW="240px" />
    <Skeleton h="48" borderRadius="xl" />
  </Stack>;
}
