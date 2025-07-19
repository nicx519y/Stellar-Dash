/**
 * 设备认证管理器
 * 负责管理设备认证信息的缓存、过期检测和自动重新获取
 */

// 设备认证信息接口
interface DeviceAuthInfo {
    deviceId: string;
    originalUniqueId: string;
    challenge: string;
    timestamp: number;
    signature: string;
    obtainedAt: number; // 获取时间戳，用于计算过期
}

// WebSocket发送函数类型定义
type WebSocketSendFunction = (command: string, params?: any) => Promise<any>;

// 认证有效期配置（1.5分钟）
const AUTH_VALIDITY_DURATION = 1.5 * 60 * 1000; // 1.5分钟，单位毫秒，server有效期是2分钟

class DeviceAuthManager {
    private static instance: DeviceAuthManager;
    private cachedAuth: DeviceAuthInfo | null = null;
    private isRefreshing = false;
    private webSocketSendFunction: WebSocketSendFunction | null = null;

    private constructor() {}

    /**
     * 获取单例实例
     */
    public static getInstance(): DeviceAuthManager {
        if (!DeviceAuthManager.instance) {
            DeviceAuthManager.instance = new DeviceAuthManager();
        }
        return DeviceAuthManager.instance;
    }

    /**
     * 设置WebSocket发送函数
     * 必须在使用认证管理器之前调用此方法
     */
    public setWebSocketSendFunction(sendFunction: WebSocketSendFunction): void {
        this.webSocketSendFunction = sendFunction;
    }

    /**
     * 检查认证信息是否过期
     */
    private isAuthExpired(auth: DeviceAuthInfo): boolean {
        const now = Date.now();
        const age = now - auth.obtainedAt;
        return age >= AUTH_VALIDITY_DURATION;
    }

    /**
     * 从STM32设备获取新的认证信息
     */
    private async fetchAuthFromDevice(): Promise<DeviceAuthInfo> {
        try {
            // 检查是否已设置WebSocket发送函数
            if (!this.webSocketSendFunction) {
                throw new Error('WebSocket发送函数未设置，请先调用setWebSocketSendFunction方法');
            }

            // 使用WebSocket获取设备认证信息
            const data = await this.webSocketSendFunction('get_device_auth');
            
            const authData = data;
            
            // 验证返回的数据结构
            if (!authData || !authData.deviceId || !authData.challenge || !authData.signature) {
                throw new Error('Invalid device auth response format');
            }

            // 创建认证信息对象
            const authInfo: DeviceAuthInfo = {
                deviceId: authData.deviceId,
                originalUniqueId: authData.originalUniqueId || '',
                challenge: authData.challenge,
                timestamp: authData.timestamp || 0,
                signature: authData.signature,
                obtainedAt: Date.now()
            };

            console.log('🔐 获取新的设备认证信息:', {
                deviceId: authInfo.deviceId,
                challenge: authInfo.challenge,
                timestamp: authInfo.timestamp,
                obtainedAt: new Date(authInfo.obtainedAt).toISOString()
            });

            return authInfo;

        } catch (error) {
            console.error('❌ 从设备获取认证信息失败:', error);
            throw error;
        }
    }

    /**
     * 获取有效的认证信息
     * 如果缓存的认证信息过期，会自动重新获取
     */
    public async getValidAuth(): Promise<DeviceAuthInfo | null> {
        try {
            // 如果正在刷新，等待完成
            if (this.isRefreshing) {
                // 简单的轮询等待，避免并发刷新
                let retries = 50; // 最多等待5秒
                while (this.isRefreshing && retries > 0) {
                    await new Promise(resolve => setTimeout(resolve, 100));
                    retries--;
                }
            }

            // 检查缓存的认证信息是否仍然有效
            if (this.cachedAuth && !this.isAuthExpired(this.cachedAuth)) {
                console.log('✅ 使用缓存的认证信息，剩余有效时间:', 
                    Math.round((AUTH_VALIDITY_DURATION - (Date.now() - this.cachedAuth.obtainedAt)) / 1000 / 60) + '分钟');
                return this.cachedAuth;
            }

            // 需要刷新认证信息
            console.log('🔄 认证信息已过期或不存在，重新获取...');
            this.isRefreshing = true;

            try {
                // 从设备获取新的认证信息
                const newAuth = await this.fetchAuthFromDevice();
                
                // 更新缓存
                this.cachedAuth = newAuth;
                
                console.log('✅ 认证信息刷新成功');
                return newAuth;

            } finally {
                this.isRefreshing = false;
            }

        } catch (error) {
            this.isRefreshing = false;
            console.error('❌ 获取有效认证信息失败:', error);
            
            // 如果获取失败，清除缓存
            this.cachedAuth = null;
            return null;
        }
    }

    /**
     * 清除缓存的认证信息
     * 用于强制下次请求重新获取
     */
    public clearCache(): void {
        console.log('🗑️ 清除认证信息缓存');
        this.cachedAuth = null;
    }

    /**
     * 检查当前缓存状态
     */
    public getCacheStatus(): { 
        hasCached: boolean; 
        isExpired: boolean; 
        remainingMinutes: number;
        isRefreshing: boolean;
    } {
        if (!this.cachedAuth) {
            return {
                hasCached: false,
                isExpired: true,
                remainingMinutes: 0,
                isRefreshing: this.isRefreshing
            };
        }

        const isExpired = this.isAuthExpired(this.cachedAuth);
        const remainingMs = AUTH_VALIDITY_DURATION - (Date.now() - this.cachedAuth.obtainedAt);
        const remainingMinutes = Math.max(0, Math.round(remainingMs / 1000 / 60));

        return {
            hasCached: true,
            isExpired,
            remainingMinutes,
            isRefreshing: this.isRefreshing
        };
    }

    /**
     * 处理服务器返回的认证失败错误
     * 当服务器返回认证相关错误时，自动清除缓存并重新获取
     */
    public async handleAuthError(error: any): Promise<DeviceAuthInfo | null> {
        console.log('🚨 处理认证错误，清除缓存并重新获取认证信息');
        
        // 清除缓存
        this.clearCache();
        
        // 重新获取认证信息
        return await this.getValidAuth();
    }
}

export default DeviceAuthManager; 