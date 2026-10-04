'use client';

import type { ReactNode } from 'react';
import { Button, Dialog, Portal } from '@chakra-ui/react';

export function AdminConfirmDialog({ open, title, description, cancelLabel, confirmLabel, confirmColorPalette, onClose, onConfirm }: {
  open: boolean;
  title: string;
  description: ReactNode;
  cancelLabel: string;
  confirmLabel: string;
  confirmColorPalette: 'green' | 'orange' | 'red';
  onClose: () => void;
  onConfirm: () => void;
}) {
  return <Portal><Dialog.Root open={open} role="alertdialog" onOpenChange={details => { if (!details.open) onClose(); }}>
    <Dialog.Backdrop backdropFilter="blur(4px)" />
    <Dialog.Positioner alignItems="flex-start" pt={{ base: '12', md: '20' }} px="4">
      <Dialog.Content width="min(92vw, 520px)">
        <Dialog.Header><Dialog.Title>{title}</Dialog.Title></Dialog.Header>
        <Dialog.Body>{description}</Dialog.Body>
        <Dialog.Footer>
          <Button variant="surface" onClick={onClose}>{cancelLabel}</Button>
          <Button colorPalette={confirmColorPalette} onClick={onConfirm}>{confirmLabel}</Button>
        </Dialog.Footer>
      </Dialog.Content>
    </Dialog.Positioner>
  </Dialog.Root></Portal>;
}
