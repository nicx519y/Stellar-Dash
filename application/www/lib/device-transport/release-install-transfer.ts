import type { TxIapTransferMode } from './release-install-client';

export function normalizeTxIapMode(mode: unknown): TxIapTransferMode {
  return mode === 'dma' || mode === 'small-packet' ? mode : 'unknown';
}

export function txIapModeLabel(mode: unknown, zh: boolean): string {
  switch (normalizeTxIapMode(mode)) {
    case 'dma': return zh ? '大包 DMA · 1000 字节/包' : 'DMA · 1000 bytes/packet';
    case 'small-packet': return zh ? '小包协议 · 40 字节/包' : 'Small packets · 40 bytes/packet';
    default: return zh ? '未知' : 'Unknown';
  }
}
