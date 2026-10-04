import type { FirmwareRelease, FirmwareRuntime, ReleaseQuery } from './firmware-types';

export async function loadAllFirmwareReleases(
  list: FirmwareRuntime['list'],
  filters: Pick<ReleaseQuery, 'query' | 'status'>,
  isCurrent: () => boolean = () => true,
): Promise<FirmwareRelease[] | null> {
  const items: FirmwareRelease[] = [];
  let offset = 0;
  while (isCurrent()) {
    const page = await list({ ...filters, offset, limit: 100 });
    if (!isCurrent()) return null;
    items.push(...page.items);
    offset += page.items.length;
    if (offset >= page.total) return items;
    if (page.items.length === 0) throw new Error('Firmware release list changed; refresh to try again.');
  }
  return null;
}
