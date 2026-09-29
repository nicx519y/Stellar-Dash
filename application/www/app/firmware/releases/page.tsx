'use client';
import { Button, Container, HStack, Stack } from '@chakra-ui/react';
import { FirmwareReleaseCatalog } from '@/components/firmware-release-catalog';
import { LanguageSwitcher } from '@/components/language-switcher';
import { UserAuthControl } from '@/components/user-auth-control';
import { useLanguage } from '@/contexts/language-context';

export default function FirmwareReleasesPage() {
  const { currentLanguage } = useLanguage();
  return <Container maxW="6xl" py="7"><Stack gap="6"><HStack justify="space-between" wrap="wrap"><Button variant="surface" onClick={() => { window.location.href = '/firmware/'; }}>{currentLanguage === 'zh' ? '返回 WebConfig' : 'Back to WebConfig'}</Button><HStack><UserAuthControl /><LanguageSwitcher /></HStack></HStack><FirmwareReleaseCatalog /></Stack></Container>;
}
