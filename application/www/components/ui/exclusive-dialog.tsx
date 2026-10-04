'use client';

import { Box, Dialog } from '@chakra-ui/react';
import { createContext, useContext, useId, useLayoutEffect, useState, useSyncExternalStore, type ComponentProps, type ReactNode } from 'react';
import { createOverlayCoordinator, OVERLAY_PRIORITY } from '@/lib/overlay-coordinator';

const OverlayContext = createContext<ReturnType<typeof createOverlayCoordinator> | null>(null);
const emptySubscribe = () => () => {};
const emptySnapshot = () => null;

export function WebConfigOverlayProvider({ children }: { children: ReactNode }) {
  const [coordinator] = useState(createOverlayCoordinator);
  return <OverlayContext.Provider value={coordinator}>{children}</OverlayContext.Provider>;
}

export function useOverlayVisibility(open: boolean, priority: number) {
  const coordinator = useContext(OverlayContext);
  const id = useId();
  const active = useSyncExternalStore(coordinator?.subscribe ?? emptySubscribe,
    coordinator?.getSnapshot ?? emptySnapshot, emptySnapshot);
  useLayoutEffect(() => {
    coordinator?.request(id, open ? priority : null);
  }, [coordinator, id, open, priority]);
  useLayoutEffect(() => () => coordinator?.request(id, null), [coordinator, id]);
  // Account-only/admin routes outside WebConfig retain their existing dialogs.
  return open && (!coordinator || active === id);
}

/** Hide the backdrop as well as the content without discarding editor inputs. */
export function ExclusiveDialog({ open = false, priority = OVERLAY_PRIORITY.editor, children, onOpenChange, ...props }:
  ComponentProps<typeof Dialog.Root> & { priority?: number }) {
  const visible = useOverlayVisibility(open, priority);
  return <Dialog.Root {...props} open={visible} onOpenChange={details => {
    if (visible) onOpenChange?.(details);
  }}>
    <Box display={visible ? 'contents' : 'none'}>{children}</Box>
  </Dialog.Root>;
}
