// Same viewport + six-row overscan approach as connect-monitor/VirtualTable.
// Prefix offsets also allow the existing expandable diagnostic rows.
export function logVirtualWindow(heights: number[], scrollTop: number, viewport: number, overscan = 6) {
  const offsets = [0];
  for (const height of heights) offsets.push(offsets[offsets.length - 1] + height);
  let first = 0;
  while (first < heights.length && offsets[first + 1] <= scrollTop) first++;
  let last = first;
  while (last < heights.length && offsets[last] < scrollTop + viewport) last++;
  return { offsets, total: offsets[heights.length], start: Math.max(0, first - overscan),
    end: Math.min(heights.length, last + overscan) };
}
