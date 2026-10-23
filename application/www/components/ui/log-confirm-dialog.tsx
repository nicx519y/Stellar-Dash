import { Button, Dialog, Portal, Text } from '@chakra-ui/react';
import { ExclusiveDialog } from './exclusive-dialog';

export function LogConfirmDialog({ open, title, description, confirm, cancel, danger, onConfirm, onCancel }: {
  open: boolean; title: string; description: string; confirm: string; cancel: string;
  danger?: boolean; onConfirm: () => void; onCancel: () => void;
}) {
  return <ExclusiveDialog open={open} role="alertdialog" placement="center" size="sm"
    onOpenChange={details => { if (!details.open) onCancel(); }}>
    <Portal><Dialog.Backdrop backdropFilter="blur(4px)" /><Dialog.Positioner px={4}>
      <Dialog.Content borderWidth="1px" borderColor="border" bg="bg.panel" borderRadius="xl">
        <Dialog.Header><Dialog.Title fontSize="md">{title}</Dialog.Title></Dialog.Header>
        <Dialog.Body><Text color="fg.muted" fontSize="sm" lineHeight="1.8">{description}</Text></Dialog.Body>
        <Dialog.Footer gap={2}>
          <Button variant="surface" size="sm" onClick={onCancel}>{cancel}</Button>
          <Button variant="subtle" colorPalette={danger ? 'red' : 'green'} size="sm" onClick={onConfirm}>{confirm}</Button>
        </Dialog.Footer>
      </Dialog.Content>
    </Dialog.Positioner></Portal>
  </ExclusiveDialog>;
}
