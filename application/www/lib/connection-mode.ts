import { type GlobalConfig } from '../types/gamepad-config';

export function connectionModeLabel(config: GlobalConfig, language: 'en' | 'zh'): string {
  const saved = config.connectionMode ?? '—';
  const active = config.effectiveConnectionMode ?? 'UNKNOWN';
  const runtime = config.ch585?.role === 'MAINTENANCE'
    ? (language === 'zh' ? 'USB 配置' : 'USB Config') : active;
  return language === 'zh' ? `记忆 ${saved} · 当前 ${runtime}` : `Saved ${saved} · Active ${runtime}`;
}
