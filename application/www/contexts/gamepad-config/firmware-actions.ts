import {
    DeviceCommandClient,
    DeviceTransportState,
    WEBHID_FIRMWARE_CHUNK_DATA_SIZE
} from '@/lib/device-transport';
import {
    abortFirmwareSessionIfSafe,
    FirmwareFinalizationUncertainError,
    sendFirmwareChunkWithoutAmbiguousRetry,
} from '@/lib/device-transport/firmware-upgrade-reliability';
import { saveReleaseConfigBackup } from '@/lib/device-transport/release-config-backup';
import {
    downloadRelease, installRelease, type FirmwareInventory, type PreparedRelease, type ReleaseProgress,
} from '@/lib/device-transport/release-install-client';
import { calculateSHA256, extractFirmwarePackage } from '@/lib/firmware-utils';
import {
    DeviceFirmwareInfo,
    FirmwarePackage,
    FirmwarePackageDownloadProgress,
    FirmwareUpdateCheckRequest,
    FirmwareUpdateCheckResponse,
    FirmwareUpgradeConfig,
    FirmwareUpgradeSession
} from '@/types/types';
import React, { useCallback } from 'react';
import { DeviceRequestSender, FIRMWARE_SERVER_CONFIG } from './contract';

export interface FirmwareActionsDependencies {
    deviceClient: DeviceCommandClient | null;
    releaseInventoryRequestRef: React.MutableRefObject<Promise<FirmwareInventory> | null>;
    releaseActivityRef: React.MutableRefObject<boolean>;
    boundaryCountRef: React.MutableRefObject<number>;
    setFirmwareUpdating: React.Dispatch<React.SetStateAction<boolean>>;
    longDeviceActivityRef: React.MutableRefObject<boolean>;
    updateSyncPause: () => void;
    queueConfigTransaction: (afterFlush?: () => void | Promise<void>, resumeMonitoringAfterSuccess?: boolean, beforeFlush?: () => void | Promise<void>, all?: boolean) => Promise<void>;
    terminateWebConfigActivities: () => Promise<void>;
    sendDeviceRequest: DeviceRequestSender;
    setFirmwareInfo: React.Dispatch<React.SetStateAction<DeviceFirmwareInfo | null>>;
    deviceState: DeviceTransportState;
    firmwareServerHost: string;
    setFirmwareUpdateInfo: React.Dispatch<React.SetStateAction<FirmwareUpdateCheckResponse | null>>;
    setError: React.Dispatch<React.SetStateAction<string | null>>;
    setUpgradeSession: React.Dispatch<React.SetStateAction<FirmwareUpgradeSession | null>>;
    upgradeConfig: FirmwareUpgradeConfig;
    setUpgradeConfigState: React.Dispatch<React.SetStateAction<FirmwareUpgradeConfig>>;
}

export function useFirmwareActions(dependencies: FirmwareActionsDependencies) {
    const getReleaseInventory = useCallback(async (): Promise<FirmwareInventory> => {
        if (!dependencies.deviceClient) throw new Error('Device is not connected');
        const request = dependencies.releaseInventoryRequestRef.current ?? dependencies.deviceClient.request('get_firmware_inventory') as unknown as Promise<FirmwareInventory>;
        dependencies.releaseInventoryRequestRef.current = request;
        let result: FirmwareInventory;
        try { result = await request; } finally { if (dependencies.releaseInventoryRequestRef.current === request) dependencies.releaseInventoryRequestRef.current = null; }
        if (!result || typeof result.protocol !== 'number' || typeof result.phase !== 'string') throw new Error('Device needs the whole-device installation baseline');
        const pending = !['idle', 'completed', 'aborted', 'restored'].includes(result.phase) ||
            (dependencies.releaseActivityRef.current && dependencies.boundaryCountRef.current > 0);
        dependencies.setFirmwareUpdating(pending);
        if (pending || dependencies.releaseActivityRef.current) dependencies.longDeviceActivityRef.current = pending;
        dependencies.releaseActivityRef.current = pending;
        dependencies.updateSyncPause();
        return result;
    }, [dependencies.deviceClient]);

    const downloadSelectedRelease = async (id: string, inventory: FirmwareInventory, progress?: (p: ReleaseProgress) => void) => {
        if (!dependencies.deviceClient) throw new Error('Device is not connected');
        return downloadRelease(dependencies.deviceClient, id, inventory, progress);
    };

    const installSelectedRelease = async (release: PreparedRelease, progress: (p: ReleaseProgress) => void) => {
        if (!dependencies.deviceClient) throw new Error('Device is not connected');
        const client = dependencies.deviceClient;
        try {
            await dependencies.queueConfigTransaction(async () => {
                progress({ stage: 'backup' });
                await saveReleaseConfigBackup(`${release.digest}-${Date.now()}`, await client.exportConfig());
                dependencies.releaseActivityRef.current = true;
                dependencies.longDeviceActivityRef.current = true; dependencies.updateSyncPause(); dependencies.setFirmwareUpdating(true);
                try {
                    await installRelease(client, release, progress);
                }
                catch (error) {
                    // A failed HTTP request or rejected BEGIN can precede device writes.
                    // Only device evidence may release the exclusive operation boundary.
                    try { await getReleaseInventory(); } catch { /* Leave uncertain operation blocked. */ }
                    throw error;
                }
            }, false, dependencies.terminateWebConfigActivities);
        }
        finally {
            // The queue boundary must have ended before clearing the release lock,
            // including when the confirmation view unmounts before BEGIN.
            try { await getReleaseInventory(); } catch { /* Keep an uncertain device operation blocked. */ }
        }
    };

    const releaseInstallAction = async (action: 'abort' | 'retry' | 'activate', sessionId: string) => {
        if (!dependencies.deviceClient) throw new Error('Device is not connected');
        await dependencies.deviceClient.request(`${action}_release_install`, { session_id: sessionId });
        await getReleaseInventory();
    };

    const fetchFirmwareMetadata = async (
        immediate: boolean = true,
        requester: DeviceRequestSender = dependencies.sendDeviceRequest,
    ): Promise<void> => {
        try {
            const data = await requester('get_firmware_metadata', {}, immediate);
            dependencies.setFirmwareInfo({
                firmware: data
            });
            return Promise.resolve();
        } catch (err) {
            return Promise.reject(new Error("Failed to fetch firmware metadata"));
        }
    };

    const checkFirmwareUpdate = async (currentVersion: string, customServerHost?: string): Promise<void> => {
        try {
            if (!dependencies.deviceClient || dependencies.deviceState !== DeviceTransportState.CONNECTED) {
                throw new Error('设备传输尚未连接');
            }
            const serverHost = customServerHost || dependencies.firmwareServerHost || FIRMWARE_SERVER_CONFIG.defaultHost;
            const response = await dependencies.deviceClient.authorizedFetch(
                `${serverHost}${FIRMWARE_SERVER_CONFIG.endpoints.checkUpdate}`,
                {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ currentVersion: currentVersion.trim() } satisfies FirmwareUpdateCheckRequest),
                },
                ['config.read'],
            );
            if (!response.ok) {
                throw new Error(`HTTP error: ${response.status} ${response.statusText}`);
            }
            const envelope = await response.json();
            if (envelope.errNo && envelope.errNo !== 0) {
                throw new Error(envelope.errorMessage || `Server error ${envelope.errNo}`);
            }
            dependencies.setFirmwareUpdateInfo(envelope.data);
        } catch (cause) {
            dependencies.setFirmwareUpdateInfo(null);
            const message = cause instanceof Error ? cause.message : '固件更新认证失败';
            dependencies.setError(message);
            throw cause;
        }
    };

    const generateSessionId = (): string => {
        return 'session_' + Date.now() + '_' + Math.random().toString(36).substr(2, 9);
    };

    const downloadFirmwarePackage = async (downloadUrl: string, onProgress?: (progress: FirmwarePackageDownloadProgress) => void): Promise<FirmwarePackage> => {
        try {
            dependencies.setError(null);

            // 初始化进度
            const initialProgress: FirmwarePackageDownloadProgress = {
                stage: 'downloading',
                progress: 0,
                message: 'Starting to download firmware package...'
            };
            onProgress?.(initialProgress);

            // 1. 下载固件包 (进度 0% - 30%)
            if (!dependencies.deviceClient) throw new Error('Device transport is not connected');
            const response = await dependencies.deviceClient.authorizedFetch(
                downloadUrl,
                undefined,
                ['firmware.update'],
            );
            if (!response.ok) {
                throw new Error(`Failed to download firmware package: ${response.status} ${response.statusText}`);
            }

            const contentLength = parseInt(response.headers.get('content-length') || '0', 10);
            if (contentLength === 0) {
                throw new Error('Failed to get file size');
            }

            const reader = response.body?.getReader();
            if (!reader) {
                throw new Error('Failed to read response data');
            }

            const chunks: Uint8Array[] = [];
            let receivedLength = 0;

            while (true) {
                const { done, value } = await reader.read();
                if (done) break;

                chunks.push(value);
                receivedLength += value.length;

                // 下载进度占总进度的80%
                const downloadProgress = (receivedLength / contentLength) * 80;
                onProgress?.({
                    stage: 'downloading',
                    progress: downloadProgress,
                    message: `Downloading firmware package... ${Math.round(receivedLength / 1024)}KB/${Math.round(contentLength / 1024)}KB`,
                    bytes_transferred: receivedLength,
                    total_bytes: contentLength
                });
            }

            // 合并所有数据块
            const packageData = new Uint8Array(receivedLength);
            let position = 0;
            for (const chunk of chunks) {
                packageData.set(chunk, position);
                position += chunk.length;
            }

            onProgress?.({
                stage: 'extracting',
                progress: 80,
                message: 'Extracting firmware package...'
            });

            // 2. 解压和验证包 (进度 30% - 40%)
            const { manifest, components } = await extractFirmwarePackage(packageData);

            // 3. 计算总大小
            onProgress?.({
                stage: 'extracting',
                progress: 85,
                message: 'Preparing firmware package...'
            });

            let totalSize = 0;
            for (const component of Object.values(components)) {
                if (component.data) {
                    totalSize += component.data.length;
                }
            }

            // 4. 创建固件包对象
            const firmwarePackage: FirmwarePackage = {
                manifest,
                components,
                totalSize
            };

            onProgress?.({
                stage: 'downcompleted',
                progress: 100,
                message: `Firmware package download completed! Total size: ${Math.round(totalSize / 1024)}KB`
            });

            dependencies.setError(null);
            return Promise.resolve(firmwarePackage);

        } catch (err) {
            const errorMessage = err instanceof Error ? err.message : 'Failed to download firmware package: unknown error';
            dependencies.setError(errorMessage);

            onProgress?.({
                stage: 'failed',
                progress: 0,
                message: 'Download failed',
                error: errorMessage
            });

            return Promise.reject(new Error(errorMessage));
        }
    };

    const uploadFirmwareToDevice = async (firmwarePackage: FirmwarePackage, onProgress?: (progress: FirmwarePackageDownloadProgress) => void): Promise<void> => {
        let activeSessionId: string | null = null;
        let completionIssued = false;
        let completionSettled = false;
        try {
            // 生成会话ID
            const sessionId = generateSessionId();

            // 创建升级会话
            const sessionData = await dependencies.sendDeviceRequest('create_firmware_upgrade_session', {
                session_id: sessionId,
                manifest: firmwarePackage.manifest
            }, true);

            const deviceSessionId = sessionData.session_id || sessionId;
            activeSessionId = deviceSessionId;

            // 更新升级会话状态
            dependencies.setUpgradeSession({
                sessionId: deviceSessionId,
                status: 'uploading',
                progress: 0,
                currentComponent: null,
                error: null
            });

            onProgress?.({
                stage: 'uploading',
                progress: 0,
                message: 'Starting to upload firmware package...'
            });

            const componentNames = Object.keys(firmwarePackage.components);
            const totalComponents = componentNames.length;

            // 逐个上传组件
            for (let i = 0; i < componentNames.length; i++) {
                const componentName = componentNames[i];
                const component = firmwarePackage.components[componentName];

                // 更新当前组件状态
                dependencies.setUpgradeSession(prev => prev ? {
                    ...prev,
                    currentComponent: componentName,
                    progress: Math.round((i / totalComponents) * 100)
                } : null);

                onProgress?.({
                    stage: 'uploading',
                    progress: Math.round((i / totalComponents) * 100),
                    message: `Uploading component: ${componentName}`
                });

                // 分片传输单个组件
                const componentData = component.data;
                if (!componentData) {
                    throw new Error(`Component ${componentName} data is missing`);
                }
                const transportChunkSize =
                    dependencies.deviceClient?.transport.kind === 'webhid'
                        ? WEBHID_FIRMWARE_CHUNK_DATA_SIZE
                        : dependencies.upgradeConfig.chunkSize;
                const totalChunks = Math.ceil(componentData.length / transportChunkSize);

                // 解析组件基地址（支持十六进制格式）
                let baseAddress: number;

                if (component.address.toString().startsWith('0x')) {
                    baseAddress = parseInt(component.address.toString(), 16);
                } else if (component.address.toString().startsWith('0X')) {
                    baseAddress = parseInt(component.address.toString(), 16);
                } else {
                    baseAddress = parseInt(component.address.toString(), 10);
                }

                // 分片传输
                for (let chunkIndex = 0; chunkIndex < totalChunks; chunkIndex++) {
                    const start = chunkIndex * transportChunkSize;
                    const end = Math.min(start + transportChunkSize, componentData.length);
                    const chunkData = componentData.slice(start, end);

                    // 计算当前chunk的精确写入地址和偏移
                    const chunkOffset = parseInt(start.toString(), 10);
                    const targetAddress = baseAddress + chunkOffset;

                    // 直接计算二进制数据的校验和（移除Intel HEX相关逻辑）
                    // 使用异步SHA256计算
                    const checksum = await calculateSHA256(chunkData);

                    // 添加调试输出
                    console.log(`Frontend calculated chunk ${chunkIndex} SHA256:`, checksum);
                    console.log(`Chunk size: ${chunkData.length} bytes`);
                    console.log(`First 32 bytes of chunk data:`, Array.from(chunkData.slice(0, 32)).map(b => b.toString(16).padStart(2, '0')).join(' '));
                    if (chunkData.length > 32) {
                        console.log(`Last 32 bytes of chunk data:`, Array.from(chunkData.slice(-32)).map(b => b.toString(16).padStart(2, '0')).join(' '));
                    }

                    const client = dependencies.deviceClient;
                    if (!client) throw new Error('Device command client is not available');
                    await sendFirmwareChunkWithoutAmbiguousRetry(
                        () => client.uploadFirmwareChunk({
                            sessionId: deviceSessionId,
                            componentName,
                            chunkIndex,
                            totalChunks,
                            chunkOffset,
                            targetAddress,
                            checksumSha256: checksum,
                            data: chunkData,
                        }, { timeoutMs: dependencies.upgradeConfig.timeout }),
                    );

                    // 更新进度
                    const componentProgress = ((chunkIndex + 1) / totalChunks) * 100;
                    const overallProgress = ((i + (chunkIndex + 1) / totalChunks) / totalComponents) * 100;

                    onProgress?.({
                        stage: 'uploading',
                        progress: Math.round(overallProgress),
                        message: `Uploading component ${componentName}: ${Math.round(componentProgress)}% (${chunkIndex + 1}/${totalChunks})`
                    });
                }
            }

            // 完成升级会话
            completionIssued = true;
            const completeResult = await dependencies.sendDeviceRequest('complete_firmware_upgrade_session', {
                session_id: deviceSessionId
            }, true);

            if (!completeResult || typeof completeResult.success !== 'boolean') {
                throw new Error('Firmware finalization returned an invalid response');
            }
            completionSettled = true;

            if (!completeResult.success) {
                throw new Error(`Failed to complete upgrade session: ${completeResult.error || 'Unknown error'}`);
            }

            // 更新最终状态
            dependencies.setUpgradeSession(prev => prev ? {
                ...prev,
                status: 'completed',
                progress: 100,
                currentComponent: null
            } : null);

            onProgress?.({
                stage: 'uploadcompleted',
                progress: 100,
                message: 'Firmware upload completed!'
            });

        } catch (error) {
            const finalizationUncertain = completionIssued && !completionSettled;
            const reportedError = finalizationUncertain
                ? new FirmwareFinalizationUncertainError(error)
                : error;
            try {
                await abortFirmwareSessionIfSafe({
                    sessionId: activeSessionId,
                    completionIssued,
                    completionSettled,
                    abortSession: async (sessionId) => {
                        await dependencies.sendDeviceRequest('abort_firmware_upgrade_session', {
                            session_id: sessionId
                        }, true);
                    },
                });
            } catch (abortError) {
                console.error('Failed to abort upgrade session:', abortError);
            }

            dependencies.setUpgradeSession(prev => prev ? {
                ...prev,
                status: 'failed',
                error: reportedError instanceof Error ? reportedError.message : 'Unknown error'
            } : null);

            onProgress?.({
                stage: 'failed',
                progress: 0,
                message: `Upload failed: ${reportedError instanceof Error ? reportedError.message : 'Unknown error'}`
            });

            throw reportedError;
        }
    };

    const uploadCh585Firmware = async (
        image: Uint8Array,
        onProgress?: (progress: number) => void,
    ): Promise<void> => {
        if (image.length <= 0x1000 || (image.length & 3) !== 0) {
            throw new Error('TX image must be the aligned combined IAP + application BIN');
        }
        const sessionId = `ch585-${Date.now().toString(16)}`;
        const sha256 = await calculateSHA256(image);
        await dependencies.sendDeviceRequest('ch585_update_begin', {
            session_id: sessionId,
            total_size: image.length,
            sha256,
        }, true);

        const chunkSize = 512;
        for (let offset = 0; offset < image.length; offset += chunkSize) {
            const chunk = image.slice(offset, Math.min(offset + chunkSize, image.length));
            const data = btoa(String.fromCharCode(...chunk));
            await dependencies.sendDeviceRequest('ch585_update_chunk', {
                session_id: sessionId,
                offset,
                data,
            }, true);
            onProgress?.(Math.round(((offset + chunk.length) * 100) / image.length));
        }

        await dependencies.sendDeviceRequest('ch585_update_complete', {
            session_id: sessionId,
        }, true);
        onProgress?.(100);
    };

    const setUpgradeConfig = (config: Partial<FirmwareUpgradeConfig>): void => {
        // 验证分片大小必须是4K或4K的倍数
        if (config.chunkSize !== undefined) {
            const CHUNK_SIZE_BASE = 4096; // 4KB基础单位

            if (config.chunkSize <= 0) {
                throw new Error('Chunk size must be greater than 0');
            }

            if (config.chunkSize % CHUNK_SIZE_BASE !== 0) {
                throw new Error(`Chunk size must be a multiple of 4KB (${CHUNK_SIZE_BASE} bytes), current value: ${config.chunkSize}`);
            }

            // 建议的最大分片大小为16KB，避免超过STM32的HTTP缓冲区限制
            const MAX_CHUNK_SIZE = 16384; // 16KB
            if (config.chunkSize > MAX_CHUNK_SIZE) {
                console.warn(`Chunk size ${config.chunkSize} exceeds the recommended maximum of ${MAX_CHUNK_SIZE}, which may lead to insufficient STM32 memory.`);
            }
        }

        dependencies.setUpgradeConfigState(prevConfig => ({
            ...prevConfig,
            ...config
        }));
    };

    const getUpgradeConfig = (): FirmwareUpgradeConfig => {
        return dependencies.upgradeConfig;
    };

    const getValidChunkSizes = (): number[] => {
        const CHUNK_SIZE_BASE = 4096; // 4KB基础单位
        const MAX_CHUNK_SIZE = 16384; // 16KB
        const validSizes: number[] = [];

        for (let size = CHUNK_SIZE_BASE; size <= MAX_CHUNK_SIZE; size += CHUNK_SIZE_BASE) {
            validSizes.push(size);
        }

        return validSizes;
    };
    return {
        getReleaseInventory, downloadSelectedRelease, installSelectedRelease, releaseInstallAction,
        fetchFirmwareMetadata, checkFirmwareUpdate, generateSessionId, downloadFirmwarePackage,
        uploadFirmwareToDevice, uploadCh585Firmware, setUpgradeConfig, getUpgradeConfig,
        getValidChunkSizes,
    };
}
