import type { ReactNode } from 'react';
import { Button, Flex, Heading, type ButtonProps, type FlexProps } from '@chakra-ui/react';

export function SidebarList({ title, titleAction, children, ...props }: FlexProps & {
  title: string;
  titleAction?: ReactNode;
}) {
  return <Flex direction="column" gap={3} minW={0} minH={0} {...props}>
    <Flex align="center" justify="space-between" gap={2} flexShrink={0}>
      <Heading fontSize="md" fontWeight="semibold">{title}</Heading>
      {titleAction}
    </Flex>
    {children}
  </Flex>;
}

export function SidebarListItems(props: FlexProps) {
  return <Flex data-testid="sidebar-list-items" direction="column" gap={1} minW={0} minH={0} {...props} />;
}

export function SidebarListItem({ selected, action, rowProps, children, ...props }: ButtonProps & {
  selected: boolean;
  action?: ReactNode;
  rowProps?: FlexProps;
}) {
  return <Flex data-testid="sidebar-list-item" h="38px" minW={0} flexShrink={0} align="center" borderWidth="1px" borderRadius="md"
    borderColor={selected ? 'green.muted' : 'transparent'}
    bg={selected ? 'green.subtle' : 'transparent'} color={selected ? 'green.fg' : 'fg.muted'}
    _hover={{ bg: 'bg.muted' }} _focusWithin={{ bg: 'bg.muted' }} {...rowProps}>
    <Button variant="ghost" colorPalette="green" h="full" minH={0} minW={0} flex={1}
      px={3} gap={2.5} justifyContent="flex-start" borderWidth="0" color="inherit"
      bg="transparent" _hover={{ bg: 'transparent' }} {...props}>{children}</Button>
    {action}
  </Flex>;
}
