'use client';

import { Button, type ButtonProps } from '@chakra-ui/react';
import { forwardRef } from 'react';

/** Compact actions for page toolbars and cards. */
export const PanelActionButton = forwardRef<HTMLButtonElement, ButtonProps & { accent?: boolean }>(
  function PanelActionButton({ accent = false, children, ...props }, ref) {
    return <Button ref={ref} size="sm" variant="outline" colorPalette="green"
      h="32px" px={3} gap={2} borderRadius="md" fontSize="sm" fontWeight="medium"
      bg={accent ? 'green.subtle' : 'bg.panel'} color={accent ? 'green.fg' : 'fg.muted'}
      borderColor={accent ? 'green.muted' : 'border'} boxShadow="0 1px 2px rgba(0, 0, 0, 0.08)"
      transition="background-color 150ms ease, border-color 150ms ease, color 150ms ease"
      _hover={{ bg: 'green.subtle', color: 'green.fg', borderColor: 'green.muted' }}
      _active={{ bg: 'green.muted' }}
      _focusVisible={{ outline: '2px solid', outlineColor: 'green.focusRing', outlineOffset: '2px' }}
      _disabled={{ opacity: 0.4, cursor: 'not-allowed', boxShadow: 'none' }}
      css={{ '& svg': { width: '14px', height: '14px', flexShrink: 0 } }}
      {...props}>{children}</Button>;
  },
);
