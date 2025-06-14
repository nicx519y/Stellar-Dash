// 共享的升级会话存储管理
import fs from 'fs';
import path from 'path';

export interface FirmwareManifest {
    version: string;
    slot: string;
    build_date: string;
    components: FirmwareComponent[];
}

export interface FirmwareComponent {
    name: string;
    file: string;
    address: string;
    size: number;
    sha256: string;
}

export interface UpgradeSession {
    sessionId: string;
    status: 'active' | 'completed' | 'aborted';
    manifest: FirmwareManifest;
    createdAt: number;
    components: Map<string, ComponentData>;
}

export interface ComponentData {
    totalChunks: number;
    receivedChunks: number;
    totalSize: number;
    receivedSize: number;
    chunks: Map<number, ChunkData>;
}

export interface ChunkData {
    data: Buffer;
    checksum: string;
    targetAddress: string;
    chunkSize: number;
    chunkOffset: number;
}

// 持久化存储的数据结构（用于JSON序列化）
interface SerializableSession {
    sessionId: string;
    status: 'active' | 'completed' | 'aborted';
    manifest: FirmwareManifest;
    createdAt: number;
    components: { [key: string]: SerializableComponentData };
}

interface SerializableComponentData {
    totalChunks: number;
    receivedChunks: number;
    totalSize: number;
    receivedSize: number;
    chunks: { [key: number]: SerializableChunkData };
}

interface SerializableChunkData {
    data: string; // Base64编码的数据
    checksum: string;
    targetAddress: string;
    chunkSize: number;
    chunkOffset: number;
}

// 全局会话存储
const upgradeSessions = new Map<string, UpgradeSession>();

// 持久化存储配置
const STORAGE_CONFIG = {
    storageDir: path.join(process.cwd(), '.firmware-sessions'),
    sessionFile: 'sessions.json',
    autoSaveInterval: 5000, // 5秒自动保存一次
    maxFileSize: 50 * 1024 * 1024, // 50MB最大文件大小
};

export class SessionStore {
    private static initialized = false;
    private static autoSaveTimer: NodeJS.Timeout | null = null;
    private static hasUnsavedChanges = false; // 标记是否有未保存的变化

    // 初始化存储系统
    private static async initialize(): Promise<void> {
        if (this.initialized) return;

        try {
            // 确保存储目录存在
            if (!fs.existsSync(STORAGE_CONFIG.storageDir)) {
                fs.mkdirSync(STORAGE_CONFIG.storageDir, { recursive: true });
            }

            // 加载已存在的会话
            await this.loadSessions();

            // 启动自动保存定时器
            this.startAutoSave();

            this.initialized = true;
            console.log('SessionStore initialized with persistent storage');
        } catch (error) {
            console.error('Failed to initialize SessionStore:', error);
            // 即使初始化失败，也要标记为已初始化，避免重复尝试
            this.initialized = true;
        }
    }

    // 从文件加载会话
    private static async loadSessions(): Promise<void> {
        const sessionFilePath = path.join(STORAGE_CONFIG.storageDir, STORAGE_CONFIG.sessionFile);
        
        try {
            if (!fs.existsSync(sessionFilePath)) {
                console.log('No existing session file found, starting with empty sessions');
                return;
            }

            const fileContent = fs.readFileSync(sessionFilePath, 'utf8');
            const sessionsData: { [key: string]: SerializableSession } = JSON.parse(fileContent);

            // 转换为内存格式
            for (const [sessionId, sessionData] of Object.entries(sessionsData)) {
                const session = this.deserializeSession(sessionData);
                upgradeSessions.set(sessionId, session);
            }

            console.log(`Loaded ${upgradeSessions.size} sessions from persistent storage`);
        } catch (error) {
            console.error('Failed to load sessions from file:', error);
            // 如果加载失败，创建备份并继续
            try {
                const backupPath = sessionFilePath + '.backup.' + Date.now();
                fs.copyFileSync(sessionFilePath, backupPath);
                console.log(`Created backup of corrupted session file: ${backupPath}`);
            } catch (backupError) {
                console.error('Failed to create backup:', backupError);
            }
        }
    }

    // 保存会话到文件
    private static async saveSessions(): Promise<void> {
        // 如果没有未保存的变化，跳过保存
        if (!this.hasUnsavedChanges) {
            return;
        }

        const sessionFilePath = path.join(STORAGE_CONFIG.storageDir, STORAGE_CONFIG.sessionFile);
        
        try {
            // 转换为可序列化格式
            const sessionsData: { [key: string]: SerializableSession } = {};
            for (const [sessionId, session] of upgradeSessions) {
                sessionsData[sessionId] = this.serializeSession(session);
            }

            const jsonData = JSON.stringify(sessionsData, null, 2);
            
            // 检查文件大小
            if (jsonData.length > STORAGE_CONFIG.maxFileSize) {
                console.warn(`Session data size (${jsonData.length}) exceeds maximum (${STORAGE_CONFIG.maxFileSize}), cleaning up old sessions`);
                this.cleanupExpiredSessions(1800000); // 清理30分钟以上的会话
                return this.saveSessions(); // 递归调用，重新保存
            }

            // 原子写入：先写入临时文件，然后重命名
            const tempFilePath = sessionFilePath + '.tmp';
            fs.writeFileSync(tempFilePath, jsonData, 'utf8');
            fs.renameSync(tempFilePath, sessionFilePath);

            // 重置未保存变化标记
            this.hasUnsavedChanges = false;
            
            console.log(`Saved ${upgradeSessions.size} sessions to persistent storage`);
        } catch (error) {
            console.error('Failed to save sessions to file:', error);
        }
    }

    // 序列化会话对象
    private static serializeSession(session: UpgradeSession): SerializableSession {
        const components: { [key: string]: SerializableComponentData } = {};
        
        for (const [componentName, componentData] of session.components) {
            const chunks: { [key: number]: SerializableChunkData } = {};
            
            for (const [chunkIndex, chunkData] of componentData.chunks) {
                chunks[chunkIndex] = {
                    data: chunkData.data.toString('base64'),
                    checksum: chunkData.checksum,
                    targetAddress: chunkData.targetAddress,
                    chunkSize: chunkData.chunkSize,
                    chunkOffset: chunkData.chunkOffset
                };
            }
            
            components[componentName] = {
                totalChunks: componentData.totalChunks,
                receivedChunks: componentData.receivedChunks,
                totalSize: componentData.totalSize,
                receivedSize: componentData.receivedSize,
                chunks
            };
        }

        return {
            sessionId: session.sessionId,
            status: session.status,
            manifest: session.manifest,
            createdAt: session.createdAt,
            components
        };
    }

    // 反序列化会话对象
    private static deserializeSession(sessionData: SerializableSession): UpgradeSession {
        const components = new Map<string, ComponentData>();
        
        for (const [componentName, componentData] of Object.entries(sessionData.components)) {
            const chunks = new Map<number, ChunkData>();
            
            for (const [chunkIndexStr, chunkData] of Object.entries(componentData.chunks)) {
                const chunkIndex = parseInt(chunkIndexStr, 10);
                chunks.set(chunkIndex, {
                    data: Buffer.from(chunkData.data, 'base64'),
                    checksum: chunkData.checksum,
                    targetAddress: chunkData.targetAddress,
                    chunkSize: chunkData.chunkSize,
                    chunkOffset: chunkData.chunkOffset
                });
            }
            
            components.set(componentName, {
                totalChunks: componentData.totalChunks,
                receivedChunks: componentData.receivedChunks,
                totalSize: componentData.totalSize,
                receivedSize: componentData.receivedSize,
                chunks
            });
        }

        return {
            sessionId: sessionData.sessionId,
            status: sessionData.status,
            manifest: sessionData.manifest,
            createdAt: sessionData.createdAt,
            components
        };
    }

    // 启动自动保存
    private static startAutoSave(): void {
        if (this.autoSaveTimer) {
            clearInterval(this.autoSaveTimer);
        }

        this.autoSaveTimer = setInterval(() => {
            this.saveSessions();
        }, STORAGE_CONFIG.autoSaveInterval);
    }

    // 停止自动保存
    private static stopAutoSave(): void {
        if (this.autoSaveTimer) {
            clearInterval(this.autoSaveTimer);
            this.autoSaveTimer = null;
        }
    }

    static async createSession(sessionId: string, manifest: FirmwareManifest): Promise<UpgradeSession> {
        await this.initialize();

        const session: UpgradeSession = {
            sessionId,
            status: 'active',
            manifest,
            createdAt: Date.now(),
            components: new Map()
        };

        // 初始化组件状态
        for (const component of manifest.components) {
            session.components.set(component.name, {
                totalChunks: 0,
                receivedChunks: 0,
                totalSize: component.size,
                receivedSize: 0,
                chunks: new Map()
            });
        }

        upgradeSessions.set(sessionId, session);
        
        // 标记有未保存的变化
        this.hasUnsavedChanges = true;
        
        // 立即保存新会话
        await this.saveSessions();
        
        return session;
    }

    static async getSession(sessionId: string): Promise<UpgradeSession | undefined> {
        await this.initialize();
        return upgradeSessions.get(sessionId);
    }

    static async updateSessionStatus(sessionId: string, status: 'active' | 'completed' | 'aborted'): Promise<boolean> {
        await this.initialize();
        
        const session = upgradeSessions.get(sessionId);
        if (session) {
            session.status = status;
            
            // 标记有未保存的变化
            this.hasUnsavedChanges = true;
            
            // 如果会话完成或中止，清理持久化存储
            if (status === 'completed' || status === 'aborted') {
                console.log(`Session ${sessionId} ${status}, cleaning up persistent storage...`);
                
                // 从内存中删除会话
                upgradeSessions.delete(sessionId);
                
                // 立即保存更新后的会话列表（不包含已删除的会话）
                await this.saveSessions();
                
                console.log(`Session ${sessionId} and its chunk data have been cleaned up from persistent storage`);
                return true;
            } else {
                // 立即保存状态更新
                await this.saveSessions();
                return true;
            }
        }
        return false;
    }

    static async deleteSession(sessionId: string): Promise<boolean> {
        await this.initialize();
        
        const deleted = upgradeSessions.delete(sessionId);
        if (deleted) {
            // 标记有未保存的变化
            this.hasUnsavedChanges = true;
            
            // 立即保存删除操作
            await this.saveSessions();
        }
        return deleted;
    }

    static async getAllSessions(): Promise<Map<string, UpgradeSession>> {
        await this.initialize();
        return new Map(upgradeSessions);
    }

    static async cleanupExpiredSessions(maxAgeMs: number = 3600000): Promise<number> { // 默认1小时过期
        await this.initialize();
        
        const now = Date.now();
        let cleanedCount = 0;

        for (const [sessionId, session] of upgradeSessions) {
            if (now - session.createdAt > maxAgeMs) {
                upgradeSessions.delete(sessionId);
                cleanedCount++;
            }
        }

        if (cleanedCount > 0) {
            console.log(`Cleaned up ${cleanedCount} expired sessions`);
            // 标记有未保存的变化
            this.hasUnsavedChanges = true;
            // 保存清理结果
            await this.saveSessions();
        }

        return cleanedCount;
    }

    // 手动触发保存
    static async forceSave(): Promise<void> {
        await this.initialize();
        // 强制标记有变化
        this.hasUnsavedChanges = true;
        await this.saveSessions();
    }

    // 优雅关闭
    static async shutdown(): Promise<void> {
        this.stopAutoSave();
        await this.saveSessions();
        console.log('SessionStore shutdown completed');
    }

    // 手动清理指定会话（包括持久化存储）
    static async cleanupSession(sessionId: string): Promise<boolean> {
        await this.initialize();
        
        const session = upgradeSessions.get(sessionId);
        if (session) {
            console.log(`Manually cleaning up session ${sessionId}...`);
            
            // 从内存中删除会话
            upgradeSessions.delete(sessionId);
            
            // 标记有未保存的变化
            this.hasUnsavedChanges = true;
            
            // 立即保存更新后的会话列表
            await this.saveSessions();
            
            console.log(`Session ${sessionId} and its chunk data have been cleaned up`);
            return true;
        }
        return false;
    }

    // 清理所有已完成或中止的会话
    static async cleanupCompletedSessions(): Promise<number> {
        await this.initialize();
        
        let cleanedCount = 0;
        const sessionsToCleanup: string[] = [];
        
        // 找出所有需要清理的会话
        for (const [sessionId, session] of upgradeSessions) {
            if (session.status === 'completed' || session.status === 'aborted') {
                sessionsToCleanup.push(sessionId);
            }
        }
        
        // 删除这些会话
        for (const sessionId of sessionsToCleanup) {
            upgradeSessions.delete(sessionId);
            cleanedCount++;
        }
        
        if (cleanedCount > 0) {
            console.log(`Cleaned up ${cleanedCount} completed/aborted sessions`);
            // 标记有未保存的变化
            this.hasUnsavedChanges = true;
            // 保存清理结果
            await this.saveSessions();
        }
        
        return cleanedCount;
    }

    // 清理所有会话和持久化存储
    static async clearAllSessions(): Promise<void> {
        await this.initialize();
        
        console.log('Clearing all sessions and persistent storage...');
        
        // 清空内存中的会话
        upgradeSessions.clear();
        
        // 标记有未保存的变化
        this.hasUnsavedChanges = true;
        
        // 保存空的会话列表到文件
        await this.saveSessions();
        
        console.log('All sessions and persistent storage have been cleared');
    }
} 