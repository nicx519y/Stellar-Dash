'use client';

import type { ReactNode } from 'react';
import { Box, Flex, HStack, Text, Stack, Button, Image, Separator } from '@chakra-ui/react';
import Link from 'next/link';
import { usePathname } from 'next/navigation';
import { LuArrowLeft, LuImages, LuPackage, LuUsers } from 'react-icons/lu';
import { LanguageSwitcher } from '@/components/language-switcher';
import { UserAuthControl } from '@/components/user-auth-control';
import { useLanguage } from '@/contexts/language-context';

const contentWidth = { maxW: '1600px', mx: 'auto', px: { base: 4, lg: 8 } } as const;

export function AdminShell({ children }: { children: ReactNode }) {
  const pathname = usePathname();
  const { currentLanguage } = useLanguage();
  const zh = currentLanguage === 'zh';
  const links = [
    { href: '/admin/users/', label: zh ? '账户管理' : 'Accounts', icon: LuUsers },
    { href: '/admin/firmware/', label: zh ? '固件管理' : 'Firmware', icon: LuPackage },
    { href: '/admin/images/', label: zh ? '官方图库' : 'Official gallery', icon: LuImages },
  ];

  return <Box minH="100vh" bg="bg" color="fg">
    <Box as="header" bg="bg.panel" borderBottomWidth="1px" borderColor="border">
      <Flex {...contentWidth} h={{ base: '104px', md: '72px' }} py={{ base: 2, md: 4 }}
        direction={{ base: 'column', md: 'row' }} justify="space-between" align={{ base: 'stretch', md: 'center' }} gap="2">
      <HStack gap="3">
        <Image src="/images/xora-mono-slate.svg" alt="XORA 星溯" width="110px" height="30px" objectFit="contain" flexShrink={0} />
        <Separator orientation="vertical" height="5" borderColor="border" flexShrink={0} aria-hidden="true" />
        <Text fontSize="xs" color="fg.muted" whiteSpace="nowrap">{zh ? '管理后台' : 'Administration'}</Text>
      </HStack>
      <HStack gap="2" h="40px" flexShrink={0} justify="flex-end"><UserAuthControl /><LanguageSwitcher /></HStack>
      </Flex>
    </Box>
    <Box bg="bg">
      <Flex {...contentWidth} h={{ base: '104px', md: '64px' }}
        direction={{ base: 'column-reverse', md: 'row' }} justify="space-between" align={{ base: 'stretch', md: 'flex-end' }} gap="2">
          <Box as="nav" aria-label={zh ? '后台导航' : 'Administration navigation'} flex={{ md: '1' }} minW="0" borderBottomWidth="1px" borderColor="border">
            <Flex gap={{ base: 0, md: 1 }}>
              {links.map(({ href, label, icon: Icon }) => {
                const active = pathname.replace(/\/$/, '') === href.replace(/\/$/, '');
                return <Button key={href} asChild variant="plain"
                  borderWidth="1px" borderColor={active ? 'border' : 'transparent'} borderBottomColor={active ? 'bg' : 'transparent'}
                  color={active ? 'fg' : 'fg.muted'} bg={active ? 'bg' : 'transparent'}
                  h="48px" mb="-1px" position="relative" zIndex={active ? 1 : undefined} flexShrink={0}
                  _hover={{ color: 'fg', bg: active ? 'bg' : 'bg.muted' }}
                  justifyContent="center" borderTopRadius="md" borderBottomRadius="0" flex={{ base: '1', md: 'initial' }}
                  gap="2" minW="0" fontSize={{ base: 'xs', md: 'sm' }} px={{ base: 2, md: 6 }}>
                  <a href={href} aria-current={active ? 'page' : undefined} onClick={event => {
                    if (event.button !== 0 || event.metaKey || event.ctrlKey || event.shiftKey || event.altKey) return;
                    event.preventDefault();
                    if (!active) window.history.pushState(null, '', href);
                  }}><Icon />{label}</a>
                </Button>;
              })}
            </Flex>
          </Box>
          <Button asChild variant="ghost" size="sm" h="32px" mt={{ base: 2, md: 0 }} mb={{ md: 2 }} flexShrink={0} alignSelf={{ base: 'flex-end', md: 'flex-end' }} color="fg.muted"><Link href="/"><LuArrowLeft />{zh ? '返回 WebConfig' : 'Back to WebConfig'}</Link></Button>
      </Flex>
    </Box>
      <Box as="main" id="admin-content" {...contentWidth} minW="0" pt={{ base: 6, lg: 8 }} pb="12">
        <Stack gap="6">{children}</Stack>
      </Box>
  </Box>;
}
