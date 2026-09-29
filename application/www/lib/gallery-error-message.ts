import { ImageTransferError } from './device-transport/image-transfer-error';
import { DeviceTransportError } from './device-transport/types';
import { GalleryApiError } from './image-gallery';

type Language = 'en' | 'zh';

const deviceErrors: Partial<Record<DeviceTransportError['code'], [string, string]>> = {
  'device-busy': ['设备正忙，请稍后重试。', 'The device is busy. Please try again shortly.'],
  'disconnected': ['设备已断开，请重新连接。', 'The device disconnected. Please reconnect.'],
  'not-connected': ['设备尚未连接，请重新连接。', 'The device is not connected. Please reconnect.'],
  'permission-required': ['需要授权访问设备，请重新连接。', 'Device permission is required. Please reconnect.'],
  'permission-denied': ['设备访问被拒绝，请检查授权。', 'Device access was denied. Check its permissions.'],
  'timeout': ['设备响应超时，请重试。', 'The device did not respond in time. Please try again.'],
  'protocol': ['设备返回的数据无效，请重新连接后重试。', 'The device returned invalid data. Reconnect and try again.'],
  'unsupported': ['当前设备不支持此图片操作，请检查固件版本。', 'This device does not support the image operation. Check its firmware version.'],
};

const knownErrors: Record<string, [string, string]> = {
  'Gallery image verification failed': ['图库图片校验失败，请重新加载后重试。', 'Gallery image verification failed. Reload and try again.'],
  'Gallery source image is invalid': ['图库原图无效，请重新上传。', 'The gallery source image is invalid. Upload it again.'],
  'Gallery upload failed': ['图库上传失败，请检查网络后重试。', 'Gallery upload failed. Check your connection and try again.'],
  'Device did not confirm the installed image': ['设备未确认图片安装，请重新连接后检查。', 'The device did not confirm the image installation. Reconnect and check it.'],
  'Invalid device image catalog': ['设备图片信息无效，请重新连接后重试。', 'The device image information is invalid. Reconnect and try again.'],
};

const galleryApiErrors: Record<string, [string, string]> = {
  AUTH_REQUIRED: ['请先登录账户。', 'Sign in to your account first.'],
  GALLERY_LIMIT_REACHED: ['个人图库已达到图片数量上限。', 'Your personal gallery has reached its image limit.'],
  GALLERY_FILE_REQUIRED: ['缺少图片文件，请重新选择。', 'An image file is missing. Select it again.'],
  GALLERY_FILE_INVALID: ['图片文件格式或大小无效。', 'The image file has an invalid type or size.'],
  GALLERY_UIMG_INVALID: ['设备图片数据无效，请重新处理后上传。', 'The device image data is invalid. Process and upload it again.'],
  GALLERY_MANIFEST_INVALID: ['图片信息与文件不匹配，请重新上传。', 'The image information does not match the file. Upload it again.'],
  GALLERY_TITLE_INVALID: ['图片名称无效。', 'The image title is invalid.'],
  GALLERY_IMAGE_NOT_FOUND: ['图片不存在或已被删除，请刷新图库。', 'The image was not found. Refresh the gallery.'],
  GALLERY_DELETE_INVALID: ['要删除的图片列表无效，请重新选择。', 'The selected images are invalid. Select them again.'],
  GALLERY_FINGERPRINT_INVALID: ['设备图片信息无效，请重新连接后重试。', 'The device image information is invalid. Reconnect and try again.'],
  GALLERY_CURSOR_INVALID: ['图库列表已变化，请重新加载。', 'The gallery list changed. Reload it.'],
};

export function galleryErrorMessage(error: unknown, language: Language): string {
  const zh = language === 'zh';
  if (error instanceof GalleryApiError) {
    const message = galleryApiErrors[error.code];
    if (message) return message[zh ? 0 : 1];
    return zh
      ? `图库请求失败（HTTP ${error.status}），请重试。`
      : `The gallery request failed (HTTP ${error.status}). Please try again.`;
  }
  if (error instanceof ImageTransferError) {
    switch (error.reason) {
      case 'catalog-request-failed':
        return zh
          ? '无法读取设备图片信息。请重新连接设备后重试；若仍失败，请检查 WebHID 连接日志。'
          : 'Could not read image information from the device. Reconnect and try again; if it persists, check the WebHID connection log.';
      case 'fast-transfer-required':
        return zh
          ? '设备固件不支持快速图片传输，请先升级设备固件。'
          : 'The device firmware does not support fast image transfer. Update the device firmware first.';
      case 'frame-limit':
        return zh
          ? `设备固件最多支持 ${error.maxFrames} 帧图片，请先升级设备固件。`
          : `The device firmware supports at most ${error.maxFrames} image frames. Update the device firmware first.`;
      case 'animation-rate':
        return zh
          ? '设备固件尚不支持 6 FPS 动图，请先升级设备固件。当前图片未被修改。'
          : 'The device firmware does not support 6 FPS animations yet. Update it first. The current image was not changed.';
    }
  }
  if (error instanceof DeviceTransportError) {
    const message = deviceErrors[error.code];
    if (message) return message[zh ? 0 : 1];
  }
  const raw = error instanceof Error ? error.message : String(error);
  const known = knownErrors[raw];
  if (known) return known[zh ? 0 : 1];
  if (/^UIMG\b/.test(raw)) {
    return zh ? '图片文件格式或校验无效，请重新上传。' : 'The image file is invalid or failed verification. Upload it again.';
  }
  const http = /^HTTP (\d{3})$/.exec(raw);
  if (http) {
    return zh ? `请求失败（HTTP ${http[1]}），请重试。` : `The request failed (HTTP ${http[1]}). Please try again.`;
  }
  return zh ? '操作失败，请重试。' : 'The operation failed. Please try again.';
}
