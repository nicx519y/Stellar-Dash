import { SECURE_HID_REPORT_SIZE, SECURE_HID_REPORT_VERSION } from './secure-hid-frame';
import { DeviceTransportError } from './types';

export interface WebHidCapability {
  reportBytes: number;
  window: number;
  spiHz: number;
  connectionEpoch: number;
}

export function parseWebHidCapability(view: DataView): WebHidCapability {
  if (view.byteLength !== 32 || view.getUint32(0, true) !== 0x32485758 ||
      view.getUint16(4, true) !== SECURE_HID_REPORT_VERSION ||
      view.getUint16(6, true) !== SECURE_HID_REPORT_SIZE) {
    throw new DeviceTransportError('protocol', 'XORA 高速协议版本不匹配，请配套更新网页、STM32 和 TX 固件');
  }
  if (view.getUint8(8) !== 2) {
    throw new DeviceTransportError('unsupported', 'XORA 配置需要 USB High-Speed 连接，请检查 USB 线缆和集线器');
  }
  if (view.getUint8(9) !== 1 || view.getUint8(10) !== 0 || view.getUint8(11) < 4 ||
      view.getUint8(11) > 8 || view.getUint32(12, true) === 0 || view.getUint32(16, true) === 0) {
    const fault = view.getUint8(10);
    const reason = fault >= 0x10 && fault <= 0x1f
      ? `SPI DMA/端口故障，状态码 ${fault & 0x0f}`
      : ['参数无效', 'SPI 协商未完成', '高速探测尚未提交', 'SPI 数据完整性或协议错误',
        'SPI 块 CRC 校验失败', 'SPI 块长度或版本错误', 'SPI 块序号、代次或窗口错误'][fault] ?? '未知错误';
    throw new DeviceTransportError('protocol', `XORA 高速桥接未就绪：${reason}（${view.getUint8(10)}）`);
  }
  for (let i = 20; i < 32; ++i) {
    if (view.getUint8(i) !== 0) throw new DeviceTransportError('protocol', 'Invalid capability reserved bytes');
  }
  return { reportBytes: view.getUint16(6, true), window: view.getUint8(11),
    spiHz: view.getUint32(12, true), connectionEpoch: view.getUint32(16, true) };
}
