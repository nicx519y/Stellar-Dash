export interface FirmwareArtifact {
  component: 'stm32' | 'tx' | 'rx';
  slot?: 'A' | 'B';
  version: string;
  buildId: string;
  hardwareVersion: string;
  file: string;
  size: number;
  sha256: string;
  bootSecurityMode: 'unlocked-development';
  requiresManualLifecycleProvisioning: false;
  imageFormat?: 'ch585-tx-combined' | 'ch585-rx-bin';
  metadataSha256?: string;
  applicationOffset?: number;
  applicationSize?: number;
  applicationSha256?: string;
}
export interface FirmwareReleaseManifest {
  schemaVersion: 1 | 2;
  buildId?: string;
  install?: ReleaseInstallContract;
  product: 'XORA';
  deviceModel: string;
  hardwareVersion: string;
  version: string;
  bootSecurityMode: 'unlocked-development';
  requiresManualLifecycleProvisioning: false;
  compatibility: { stm32Tx: string; txRx: string };
  artifacts: FirmwareArtifact[];
}
export interface FirmwareAuditEvent {
  actor: { actorType: string; actorId: string };
  action: string;
  at: string;
  before: unknown;
  after: unknown;
}
export interface PublicFirmwareRelease {
  bundleSha256?: string;
  installable?: boolean;
  id: string;
  manifest: FirmwareReleaseManifest;
  notes: string;
  publishedAt: string | null;
  status: 'published';
}

export interface ReleaseInstallContract {
  protocol: 1 | 2;
  order: 'tx-then-stm32';
  configRead: { min: number; max: number };
  configWrite: number;
  stm32Maintenance: { min: number; max: number };
  txMaintenance: { min: number; max: number };
}
export interface FirmwareRelease extends Omit<PublicFirmwareRelease, 'status'> {
  status: 'draft' | 'published' | 'withdrawn';
  revision: number;
  acceptance: string;
  createdAt: string;
  reason: string;
  checks: string[];
  audit?: FirmwareAuditEvent[];
}
export interface ReleasePage<T> { items: T[]; total: number; limit: number; offset: number }
export interface ReleaseQuery { query?: string; status?: string; hardware?: string; offset?: number; limit?: number }
export interface ReleaseImport { id: string; status: 'validating' | 'completed' | 'failed'; releaseId: string | null; error: string | null }
export interface LegacyFirmware { id: string; version: string; hardwareVersion: string; notes: string; scope: 'STM32_ONLY' }
export interface FirmwareRuntime {
  list(query?: ReleaseQuery): Promise<ReleasePage<FirmwareRelease>>;
  detail(id: string): Promise<FirmwareRelease>;
  importBundle(file: File, onProgress: (percent: number) => void): Promise<ReleaseImport>;
  edit(id: string, revision: number, notes: string, acceptance?: string): Promise<FirmwareRelease>;
  publish(id: string, revision: number): Promise<FirmwareRelease>;
  withdraw(id: string, revision: number, reason?: string): Promise<FirmwareRelease>;
  remove(id: string, revision: number): Promise<void>;
  legacy(): Promise<LegacyFirmware[]>;
  catalog(query?: ReleaseQuery): Promise<ReleasePage<PublicFirmwareRelease>>;
}
