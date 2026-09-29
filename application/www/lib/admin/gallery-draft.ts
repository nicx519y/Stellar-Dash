export type GalleryDraftEntry = { id: string; title: string; kind: 'existing' | 'new' };

export function moveGalleryItemToIndex<T extends { id: string }>(items: readonly T[], sourceId: string, targetIndex: number): T[] {
  const next = [...items];
  const sourceIndex = next.findIndex(item => item.id === sourceId);
  if (sourceIndex < 0) return next;
  const [moved] = next.splice(sourceIndex, 1);
  next.splice(Math.max(0, Math.min(targetIndex, next.length)), 0, moved);
  return next;
}

export function nearestGallerySlot(center: { x: number; y: number }, slots: readonly { x: number; y: number }[]): number {
  let nearest = 0;
  let distance = Infinity;
  slots.forEach((slot, index) => {
    const nextDistance = (center.x - slot.x) ** 2 + (center.y - slot.y) ** 2;
    if (nextDistance < distance) { distance = nextDistance; nearest = index; }
  });
  return nearest;
}

export function moveGalleryItem<T extends { id: string }>(
  items: readonly T[], sourceId: string, targetId: string, position: 'before' | 'after',
): T[] {
  if (sourceId === targetId || !items.some(item => item.id === sourceId) || !items.some(item => item.id === targetId)) return [...items];
  const next = [...items];
  const sourceIndex = next.findIndex(item => item.id === sourceId);
  const [moved] = next.splice(sourceIndex, 1);
  const targetIndex = next.findIndex(item => item.id === targetId);
  next.splice(targetIndex + (position === 'after' ? 1 : 0), 0, moved);
  return next;
}

export function planGalleryPublish<T extends GalleryDraftEntry>(
  baseline: readonly { id: string; title: string }[],
  draft: readonly T[],
): { additions: T[]; renames: T[]; deletions: string[]; orderChanged: boolean } {
  const original = new Map(baseline.map(item => [item.id, item.title]));
  const retained = new Set(draft.filter(item => item.kind === 'existing').map(item => item.id));
  const previousOrder = baseline.filter(item => retained.has(item.id)).map(item => item.id);
  const currentOrder = draft.filter(item => item.kind === 'existing').map(item => item.id);
  return {
    additions: draft.filter(item => item.kind === 'new'),
    renames: draft.filter(item => item.kind === 'existing' && original.has(item.id) && original.get(item.id) !== item.title),
    deletions: baseline.filter(item => !retained.has(item.id)).map(item => item.id),
    orderChanged: previousOrder.some((id, index) => id !== currentOrder[index]),
  };
}

export function galleryDraftChanged(plan: ReturnType<typeof planGalleryPublish>): boolean {
  return plan.additions.length > 0 || plan.renames.length > 0 || plan.deletions.length > 0 || plan.orderChanged;
}
