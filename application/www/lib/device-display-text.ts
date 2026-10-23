/** Convert chip names in human-readable diagnostics without changing source data. */
export function deviceDisplayText(text: string, language: 'zh' | 'en'): string {
  return text
    .replace(/\bSTM32[A-Z0-9]*\b/gi, language === 'zh' ? '主控' : 'Main controller')
    .replace(/\bCH58[0-9X][A-Z0-9]*(?:\s+(TX|RX))?\b/gi, (_match, role: string | undefined) => role?.toUpperCase() || 'TX');
}
