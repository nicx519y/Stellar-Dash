'use client';

import type { ReactNode } from 'react';
import { Box, Flex, Heading, Stack, Text, type BoxProps } from '@chakra-ui/react';

export function AdminCard(props: BoxProps) {
  return <Box bg="app.panel" borderWidth="1px" borderColor="app.border" borderRadius="xl"
    p={{ base: 4, md: 6 }} minW="0" shadow="xs" {...props} />;
}

export function AdminPageHeader({ title, description, actions }: { title: string; description: string; actions?: ReactNode }) {
  return <Flex pl={{ base: '17px', md: '25px' }} justify="space-between" align={{ base: 'flex-start', md: 'center' }} gap="4" direction={{ base: 'column', md: 'row' }}>
    <Stack gap="2" minW="0"><Heading as="h1" size="2xl">{title}</Heading><Text color="fg.muted" fontSize="sm">{description}</Text></Stack>
    {actions}
  </Flex>;
}
