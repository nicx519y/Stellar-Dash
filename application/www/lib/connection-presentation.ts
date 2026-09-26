import { DeviceConnectionPhase, type DeviceConnectionError } from './device-transport/device-command-types';
import type { DeviceTransportErrorCode } from './device-transport/types';

const en = {
  title: 'Getting your XORA ready',
  description: 'Your device settings will appear here once syncing is complete.',
  connect: 'Connect device',
  sync: 'Sync configuration',
  ready: 'Ready to configure',
  discovering: 'Finding your XORA',
  opening: 'Connecting to your device',
  securing: 'Establishing a secure connection',
  reading: 'Reading device configuration',
  checking: 'Checking configuration versions',
  finishing: 'Preparing your workspace',
  waiting: 'Waiting for device configuration',
  progress: 'Configuration sync',
  keepConnected: 'Keep your device connected via USB.',
  readCount: '{completed} of {total} items read',
};

export const CONNECTION_TEXT: Record<'en' | 'zh', typeof en> = {
  en,
  zh: {
    title: '正在准备你的 XORA',
    description: '配置同步完成后，即可开始调整设备设置。',
    connect: '连接设备',
    sync: '同步配置',
    ready: '准备就绪',
    discovering: '正在查找 XORA',
    opening: '正在连接设备',
    securing: '正在建立安全连接',
    reading: '正在读取设备配置',
    checking: '正在核对配置版本',
    finishing: '正在准备配置页面',
    waiting: '等待读取设备配置',
    progress: '配置同步',
    keepConnected: '请保持设备的 USB 连接。',
    readCount: '已读取 {completed} / {total} 项',
  },
};

const CONNECTION_ERRORS: Record<'en' | 'zh', Record<DeviceTransportErrorCode, string>> = {
  en: {
    'unsupported': 'This browser does not support WebHID. Open this page in Chrome or Edge on a computer.',
    'permission-required': 'No XORA has been authorized yet. Click Reconnect Device, then select and authorize your device in the browser chooser.',
    'permission-denied': 'Device access was not granted. Click Reconnect Device and select your XORA in the browser chooser.',
    'device-busy': 'Your XORA is in use. Close other pages or apps connected to it, then reconnect.',
    'not-connected': 'Your XORA is not connected. Check the USB connection and WebConfig mode, then reconnect.',
    'disconnected': 'The device was disconnected. Check the USB connection, then reconnect.',
    'authentication-required': 'A secure device connection is required. Reconnect your XORA to continue.',
    'authentication-failed': 'The secure device connection failed. Reconnect your XORA to try again.',
    'protocol': 'The device returned an unexpected response. Reconnect your XORA to try again.',
    'timeout': 'The device did not respond in time. Check the USB connection, then reconnect.',
    'server': 'The connection could not be completed. Please try reconnecting.',
  },
  zh: {
    'unsupported': '当前浏览器不支持 WebHID，请在电脑上使用 Chrome 或 Edge 打开此页面。',
    'permission-required': '尚未授权 XORA 设备。请点击“重新连接设备”，然后在浏览器选择器中选择并授权设备。',
    'permission-denied': '尚未获得设备访问权限。请点击“重新连接设备”，并在浏览器选择器中选择你的 XORA。',
    'device-busy': 'XORA 正被占用。请关闭其他连接此设备的网页或应用，然后重新连接。',
    'not-connected': 'XORA 尚未连接。请检查 USB 连接及 WebConfig 模式，然后重新连接。',
    'disconnected': '设备连接已断开。请检查 USB 连接，然后重新连接。',
    'authentication-required': '需要建立设备安全连接，请重新连接 XORA 后继续。',
    'authentication-failed': '设备安全连接失败，请重新连接 XORA 后重试。',
    'protocol': '设备返回了异常响应，请重新连接 XORA 后重试。',
    'timeout': '设备响应超时。请检查 USB 连接，然后重新连接。',
    'server': '暂时无法完成连接，请尝试重新连接。',
  },
};

// Localize stable error codes at the UI boundary; retain raw diagnostics in the transport.
export function connectionErrorMessage(
  error: Pick<DeviceConnectionError, 'transportCode' | 'type'> | null | undefined,
  language: 'en' | 'zh',
): string | undefined {
  if (!error) return undefined;
  const code = error.transportCode ?? (error.type === 'connection' ? 'not-connected' : error.type);
  return CONNECTION_ERRORS[language][code] ?? CONNECTION_ERRORS[language].server;
}

export function connectionPresentation(
  phase: DeviceConnectionPhase,
  progress: { completed: number; total: number; phase?: 'checking' | 'reading' | 'complete' },
) {
  const syncing = phase === DeviceConnectionPhase.INITIALIZING || phase === DeviceConnectionPhase.READY;
  const total = syncing && Number.isFinite(progress.total) ? Math.max(0, progress.total) : 0;
  const completed = total && Number.isFinite(progress.completed) ? Math.min(total, Math.max(0, progress.completed)) : 0;
  const percent = total ? Math.floor(completed / total * 100) : 0;
  const stage = !syncing ? 0 : progress.phase === 'complete' || (!progress.phase && total > 0 && completed === total) ? 2 : 1;
  const detail: keyof typeof en = syncing && progress.phase === 'checking' ? 'checking' : stage === 2 ? 'finishing' : syncing ? 'reading'
    : phase === DeviceConnectionPhase.DISCOVERING ? 'discovering'
    : phase === DeviceConnectionPhase.ATTESTING || phase === DeviceConnectionPhase.AUTHORIZING ? 'securing' : 'opening';
  return { total, completed, percent, stage, detail };
}
