'use client';

import type { ReactNode } from 'react';
import { Box, Flex, HStack, Text, Stack, Button, Image, Separator } from '@chakra-ui/react';
import { usePathname } from 'next/navigation';
import { LuArrowLeft, LuImages, LuKeyRound, LuPackage, LuUsers } from 'react-icons/lu';
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
    { href: '/admin/service-tokens/', label: zh ? '服务令牌' : 'Service tokens', icon: LuKeyRound },
    { href: '/admin/firmware/', label: zh ? '固件管理' : 'Firmware', icon: LuPackage },
    { href: '/admin/resources/', label: zh ? '资源管理' : 'Resources', icon: LuPackage },
    { href: '/admin/images/', label: zh ? '官方图库' : 'Official gallery', icon: LuImages },
  ];

  return <Box minH="100vh" bg="bg" color="fg">
    <Box as="header" bg="bg.panel" borderBottomWidth="1px" borderColor="border">
      <Flex {...contentWidth} minH="72px" py="3" gap={{ base: 3, lg: 5 }}
        direction={{ base: 'column', lg: 'row' }} align={{ base: 'stretch', lg: 'center' }}>
      <HStack gap="3" flexShrink={0}>
        <Image src="/images/xora-mono-slate.svg" alt="XORA 星溯" width="110px" height="30px" objectFit="contain" flexShrink={0} />
        <Separator orientation="vertical" height="5" borderColor="border" flexShrink={0} aria-hidden="true" />
        <Text fontSize="xs" color="fg.muted" whiteSpace="nowrap">{zh ? '管理后台' : 'Administration'}</Text>
      </HStack>
      <Box as="nav" aria-label={zh ? '后台导航' : 'Administration navigation'} flex={{ lg: 1 }} minW="0"
        order={{ base: 3, lg: 2 }}>
            <Flex gap="1" overflowX={{ base: 'auto', lg: 'visible' }}>
              {links.map(({ href, label, icon: Icon }) => {
                const section = href.replace(/\/$/, '');
                const current = pathname.replace(/\/$/, '');
                const active = current === section || current.startsWith(`${section}/`);
                return <Button key={href} asChild variant="plain"
                  color={active ? 'fg' : 'fg.muted'} bg={active ? 'bg.muted' : 'transparent'}
                  h="40px" flexShrink={0} borderRadius="md"
                  _hover={{ color: 'fg', bg: 'bg.muted' }}
                  justifyContent="center" flex="0 0 auto"
                  gap="2" minW="0" whiteSpace="nowrap" fontSize={{ base: 'xs', md: 'sm' }} px={{ base: 2, md: 4 }}>
                  <a href={href} aria-current={active ? 'page' : undefined} onClick={event => {
                    if (event.button !== 0 || event.metaKey || event.ctrlKey || event.shiftKey || event.altKey) return;
                    event.preventDefault();
                    if (current !== section) window.history.pushState(null, '', href);
                  }}><Icon />{label}</a>
                </Button>;
              })}
            </Flex>
      </Box>
      <HStack gap="2" minH="40px" flexShrink={0} justify="flex-end" order={{ base: 2, lg: 3 }}>
        <Button asChild variant="ghost" size="sm" flexShrink={0} color="fg.muted"><a href="/global/"><LuArrowLeft />{zh ? '返回 WebConfig' : 'Back to WebConfig'}</a></Button>
        <UserAuthControl /><LanguageSwitcher />
      </HStack>
      </Flex>
    </Box>
      <Box as="main" id="admin-content" {...contentWidth} minW="0" pt={{ base: 6, lg: 8 }} pb="12">
        <Stack gap="6">{children}</Stack>
      </Box>
  </Box>;
}
