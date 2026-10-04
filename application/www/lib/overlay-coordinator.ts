export const OVERLAY_PRIORITY = {
  editor: 10,
  confirmation: 20,
  recovery: 30,
  connection: 40,
  operation: 50,
  account: 60,
} as const;

/** One visible blocking overlay; suspended requests keep their original order. */
export function createOverlayCoordinator() {
  const requests = new Map<string, { priority: number; order: number }>();
  const listeners = new Set<() => void>();
  let sequence = 0;
  let active: string | null = null;
  const publish = () => {
    let winner: string | null = null;
    let rank = -Infinity;
    let order = -Infinity;
    for (const [id, request] of requests) {
      if (request.priority > rank || (request.priority === rank && request.order > order)) {
        winner = id; rank = request.priority; order = request.order;
      }
    }
    if (winner === active) return;
    active = winner;
    for (const listener of listeners) listener();
  };
  return {
    getSnapshot: () => active,
    subscribe(listener: () => void) {
      listeners.add(listener);
      return () => { listeners.delete(listener); };
    },
    request(id: string, priority: number | null) {
      if (priority === null) requests.delete(id);
      else requests.set(id, { priority, order: requests.get(id)?.order ?? ++sequence });
      publish();
    },
  };
}
