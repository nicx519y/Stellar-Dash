export type DiagnosticsPolicy = {
  version: 2;
  loggingEnabled: boolean;
  monitoringEnabled: boolean;
  controller: 'applied';
  tx: 'applied' | 'pending' | 'unsupported';
  rx: 'applied' | 'pending' | 'unsupported';
};

export function parseDiagnosticsPolicy(value: unknown): DiagnosticsPolicy {
  const p = value as Partial<DiagnosticsPolicy> | null;
  if (!p || p.version !== 2 || typeof p.loggingEnabled !== 'boolean' || typeof p.monitoringEnabled !== 'boolean' || p.controller !== 'applied' ||
      !['applied', 'pending', 'unsupported'].includes(p.tx ?? '') ||
      !['applied', 'pending', 'unsupported'].includes(p.rx ?? '')) {
    throw new Error('Diagnostics policy unavailable; update firmware');
  }
  return p as DiagnosticsPolicy;
}

export type DiagnosticsSettings = Pick<DiagnosticsPolicy, "loggingEnabled" | "monitoringEnabled">;
