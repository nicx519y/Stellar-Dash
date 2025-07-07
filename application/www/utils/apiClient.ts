import DeviceAuthManager from './deviceAuth';

/**
 * API客户端工具类
 * 演示如何使用设备认证管理器进行API调用
 */
class ApiClient {
  private authManager: DeviceAuthManager;
  private baseUrl: string;

  constructor(baseUrl: string = 'http://localhost:3000') {
    this.authManager = DeviceAuthManager.getInstance();
    this.baseUrl = baseUrl;
  }

  /**
   * 检查固件更新
   * 使用POST方式，认证信息在body中
   */
  async checkFirmwareUpdate(currentVersion: string) {
    try {
      const response = await this.authManager.authenticatedPost(
        `${this.baseUrl}/api/firmware-check-update`,
        { currentVersion }
      );

      if (!response.ok) {
        throw new Error(`请求失败: ${response.status} ${response.statusText}`);
      }

      return await response.json();
    } catch (error) {
      console.error('检查固件更新失败:', error);
      throw error;
    }
  }

  /**
   * 获取设备列表
   * 使用GET方式，认证信息在header中
   */
  async getDeviceList() {
    try {
      const response = await this.authManager.authenticatedGet(
        `${this.baseUrl}/api/devices`
      );

      if (!response.ok) {
        throw new Error(`请求失败: ${response.status} ${response.statusText}`);
      }

      return await response.json();
    } catch (error) {
      console.error('获取设备列表失败:', error);
      throw error;
    }
  }

  /**
   * 获取固件列表（无需认证的示例）
   */
  async getFirmwareList() {
    try {
      const response = await fetch(`${this.baseUrl}/api/firmwares`);

      if (!response.ok) {
        throw new Error(`请求失败: ${response.status} ${response.statusText}`);
      }

      return await response.json();
    } catch (error) {
      console.error('获取固件列表失败:', error);
      throw error;
    }
  }

  /**
   * 获取认证状态（用于调试）
   */
  getAuthStatus() {
    return this.authManager.getAuthStatus();
  }

  /**
   * 强制刷新认证信息
   */
  async refreshAuth() {
    return await this.authManager.refreshAuth();
  }
}

// 导出单例实例
export const apiClient = new ApiClient();
export default ApiClient; 