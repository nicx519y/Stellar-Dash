import {
    DeviceCommandClient
} from '@/lib/device-transport';
import {
    ADCValuesMapping,
    StepInfo,
    SwitchMappingCatalogDetail,
    SwitchMappingCatalogItem,
    SwitchMappingPayload,
    switchMappingSha256,
} from '@/types/adc';
import React, { useCallback } from 'react';
import { DeviceRequestSender } from './contract';

export interface MappingActionsDependencies {
    setIsLoading: React.Dispatch<React.SetStateAction<boolean>>;
    sendDeviceRequest: DeviceRequestSender;
    setMappingList: React.Dispatch<React.SetStateAction<{ id: string; name: string; }[]>>;
    setDefaultMappingId: React.Dispatch<React.SetStateAction<string>>;
    setMappingStorageMode: React.Dispatch<React.SetStateAction<string | null>>;
    setMappingSource: React.Dispatch<React.SetStateAction<"server-installed" | "factory-fallback" | null>>;
    setError: React.Dispatch<React.SetStateAction<string | null>>;
    activeMapping: ADCValuesMapping | null;
    setActiveMapping: React.Dispatch<React.SetStateAction<ADCValuesMapping | null>>;
    longDeviceActivityRef: React.MutableRefObject<boolean>;
    updateSyncPause: () => void;
    setMarkingStatus: React.Dispatch<React.SetStateAction<StepInfo>>;
    markingStatus: StepInfo;
    deviceClient: DeviceCommandClient | null;
}

export function useMappingActions(dependencies: MappingActionsDependencies) {
    const fetchMappingList = async (immediate: boolean = true): Promise<void> => {
        try {
            dependencies.setIsLoading(true);
            const data = await dependencies.sendDeviceRequest('ms_get_list', {}, immediate);
            if (data && 'mappingList' in data && 'defaultMappingId' in data) {
                dependencies.setMappingList(data.mappingList as { id: string, name: string }[]);
                dependencies.setDefaultMappingId(data.defaultMappingId as string);
                dependencies.setMappingStorageMode(
                    typeof data.storageMode === 'string' ? data.storageMode : null,
                );
                dependencies.setMappingSource(
                    data.source === 'server-installed' || data.source === 'factory-fallback'
                        ? data.source
                        : null,
                );
            }
            dependencies.setError(null);
            return Promise.resolve();
        } catch (err) {
            dependencies.setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to fetch mapping list"));
        } finally {
            dependencies.setIsLoading(false);
        }
    };

    const fetchDefaultMapping = async (immediate: boolean = true): Promise<void> => {
        try {
            dependencies.setIsLoading(true);
            const data = await dependencies.sendDeviceRequest('ms_get_default', {}, immediate);
            if (data && 'id' in data) {
                dependencies.setDefaultMappingId(data.id as string ?? "");
            }
            return Promise.resolve();
        } catch (err) {
            dependencies.setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to fetch default mapping"));
        } finally {
            dependencies.setIsLoading(false);
        }
    };

    const createMapping = async (name: string, length: number, step: number, immediate: boolean = true): Promise<string> => {
        try {
            dependencies.setIsLoading(true);
            const data = await dependencies.sendDeviceRequest('ms_create_mapping', { name, length, step }, immediate);
            if (data && 'mappingList' in data && 'defaultMappingId' in data) {
                dependencies.setMappingList(data.mappingList as { id: string, name: string }[]);
                dependencies.setDefaultMappingId(data.defaultMappingId as string);
                dependencies.setMappingStorageMode(typeof data.storageMode === 'string' ? data.storageMode : null);
                dependencies.setMappingSource(
                    data.source === 'server-installed' || data.source === 'factory-fallback'
                        ? data.source
                        : null,
                );
            }
            const createdMappingId = typeof data?.createdMappingId === 'string'
                ? data.createdMappingId
                : '';
            if (!createdMappingId) {
                throw new Error('Device did not return the created mapping id');
            }
            dependencies.setError(null);
            return createdMappingId;
        } catch (err) {
            dependencies.setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to create mapping"));
        } finally {
            dependencies.setIsLoading(false);
        }
    };

    const deleteMapping = async (id: string, immediate: boolean = true): Promise<void> => {
        try {
            dependencies.setIsLoading(true);
            const data = await dependencies.sendDeviceRequest('ms_delete_mapping', { id }, immediate);
            if (data && 'mappingList' in data && 'defaultMappingId' in data) {
                dependencies.setMappingList(data.mappingList as { id: string, name: string }[]);
                dependencies.setDefaultMappingId(data.defaultMappingId as string);
            }
            if (dependencies.activeMapping?.id === id) {
                dependencies.setActiveMapping(null);
            }
            dependencies.setError(null);
            return Promise.resolve();
        } catch (err) {
            dependencies.setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to delete mapping"));
        } finally {
            dependencies.setIsLoading(false);
        }
    };

    const updateDefaultMapping = async (id: string, immediate: boolean = true): Promise<void> => {
        try {
            dependencies.setIsLoading(true);
            const data = await dependencies.sendDeviceRequest('ms_set_default', { id }, immediate);
            dependencies.setDefaultMappingId(data.id);
            dependencies.setError(null);
            return Promise.resolve();
        } catch (err) {
            dependencies.setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to set default mapping"));
        } finally {
            dependencies.setIsLoading(false);
        }
    };

    const startMarking = async (id: string, immediate: boolean = true): Promise<void> => {
        dependencies.longDeviceActivityRef.current = true; dependencies.updateSyncPause();
        try {
            dependencies.setIsLoading(true);
            const data = await dependencies.sendDeviceRequest('ms_mark_mapping_start', { id }, immediate);
            if (data.status) {
                dependencies.setMarkingStatus(data.status);
            }
            dependencies.setError(null);
            return Promise.resolve();
        } catch (err) {
            dependencies.longDeviceActivityRef.current = false; dependencies.updateSyncPause();
            dependencies.setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to start marking"));
        } finally {
            dependencies.setIsLoading(false);
        }
    };

    const stopMarking = async (immediate: boolean = true): Promise<void> => {
        try {
            dependencies.setIsLoading(true);
            const stoppedMappingId = dependencies.markingStatus.id;
            const data = await dependencies.sendDeviceRequest('ms_mark_mapping_stop', {}, immediate);
            dependencies.longDeviceActivityRef.current = false; dependencies.updateSyncPause();
            if (data.status) {
                dependencies.setMarkingStatus(data.status);
            }
            if (stoppedMappingId && stoppedMappingId !== 'draft') {
                await fetchActiveMapping(stoppedMappingId, immediate);
            }
            dependencies.setError(null);
            return Promise.resolve();
        } catch (err) {
            dependencies.setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to stop marking"));
        } finally {
            dependencies.setIsLoading(false);
        }
    };

    const stepMarking = async (immediate: boolean = true): Promise<void> => {
        try {
            dependencies.setIsLoading(true);
            const data = await dependencies.sendDeviceRequest('ms_mark_mapping_step', {}, immediate);
            if (data.status) {
                const status = data.status as StepInfo;
                dependencies.setMarkingStatus(status);
                if (status.is_completed && status.id && status.id !== 'draft') {
                    await fetchActiveMapping(status.id, immediate);
                }
            }
            dependencies.setError(null);
            return Promise.resolve();
        } catch (err) {
            dependencies.setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to step marking"));
        } finally {
            dependencies.setIsLoading(false);
        }
    };

    const fetchMarkingStatus = async (immediate: boolean = true): Promise<void> => {
        try {
            const data = await dependencies.sendDeviceRequest('ms_get_mark_status', {}, immediate);
            if (data?.status) {
                dependencies.setMarkingStatus(data.status as StepInfo);
            }
            dependencies.setError(null);
        } catch (err) {
            dependencies.setError(err instanceof Error ? err.message : 'An error occurred');
            throw new Error("Failed to fetch marking status");
        }
    };

    const syncMarkingProgress = async (): Promise<void> => {
        const data = await dependencies.sendDeviceRequest('ms_mark_mapping_sync', {}, true);
        if (data?.status) {
            dependencies.setMarkingStatus(data.status as StepInfo);
        }
    };

    const fetchActiveMapping = async (id: string, immediate: boolean = true): Promise<void> => {
        try {
            dependencies.setIsLoading(true);
            const data = await dependencies.sendDeviceRequest('ms_get_mapping', { id }, immediate);
            dependencies.setActiveMapping(data.mapping);
            dependencies.setError(null);
            return Promise.resolve();
        } catch (err) {
            dependencies.setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to fetch mapping"));
        } finally {
            dependencies.setIsLoading(false);
        }
    };

    const renameMapping = async (id: string, name: string, immediate: boolean = true): Promise<void> => {
        try {
            dependencies.setIsLoading(true);
            const data = await dependencies.sendDeviceRequest('ms_rename_mapping', { id, name }, immediate);
            dependencies.setMappingList(data.mappingList);
            dependencies.setDefaultMappingId(data.defaultMappingId);
            dependencies.setActiveMapping({ ...(dependencies.activeMapping as ADCValuesMapping), name: name });
            dependencies.setError(null);
            return Promise.resolve();
        } catch (err) {
            dependencies.setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to rename mapping"));
        } finally {
            dependencies.setIsLoading(false);
        }
    };

    const readSwitchMappingEnvelope = async <T,>(response: Response): Promise<T> => {
        const envelope = await response.json() as {
            success?: boolean;
            data?: T;
            message?: string;
            error?: string;
        };
        if (!response.ok || envelope.success !== true || envelope.data === undefined) {
            throw new Error(envelope.message || envelope.error || `HTTP ${response.status}`);
        }
        return envelope.data;
    };

    const fetchSwitchMappingResponse = async (
        input: RequestInfo | URL,
        init: RequestInit = {},
    ): Promise<Response> => {
        if (!dependencies.deviceClient) throw new Error('设备命令客户端未初始化');
        const controller = new AbortController();
        const timeout = setTimeout(() => controller.abort(), 10_000);
        try {
            return await dependencies.deviceClient.authorizedFetch(
                input,
                { ...init, signal: controller.signal },
                ['config.read'],
            );
        } catch (error) {
            if (controller.signal.aborted) {
                throw new Error('服务器映射请求超时');
            }
            throw error;
        } finally {
            clearTimeout(timeout);
        }
    };

    const fetchDeviceAuthorizedResource = useCallback(async (
        input: RequestInfo | URL,
        init: RequestInit = {},
    ): Promise<Response> => {
        if (!dependencies.deviceClient) throw new Error('设备命令客户端未初始化');
        return dependencies.deviceClient.authorizedFetch(input, init, ['config.read']);
    }, [dependencies.deviceClient]);

    const fetchSwitchMappingCatalog = async (
        includeDrafts: boolean = false,
    ): Promise<SwitchMappingCatalogItem[]> => {
        const response = await fetchSwitchMappingResponse(
            includeDrafts
                ? '/api/admin/switch-mappings-compatible'
                : '/api/switch-mappings',
            { cache: 'no-store' },
        );
        const data = await readSwitchMappingEnvelope<{ items: SwitchMappingCatalogItem[] }>(response);
        return Array.isArray(data.items) ? data.items : [];
    };

    const fetchSwitchMappingDetail = async (
        catalogId: string,
        admin: boolean = false,
    ): Promise<SwitchMappingCatalogDetail> => {
        const response = await fetchSwitchMappingResponse(
            `${admin ? '/api/admin' : '/api'}/switch-mappings/${encodeURIComponent(catalogId)}`,
            { cache: 'no-store' },
        );
        const detail = await readSwitchMappingEnvelope<SwitchMappingCatalogDetail>(response);
        if (!detail.revision?.mapping || detail.revision.mapping.id !== detail.revision.revisionId ||
            await switchMappingSha256(detail.revision.mapping) !== detail.revision.sha256?.toLowerCase()) {
            throw new Error('服务器返回的映射结构或 SHA-256 无效');
        }
        return detail;
    };

    const fetchSwitchMappingImage = async (
        catalogId: string,
        admin: boolean = false,
    ): Promise<Blob | null> => {
        const response = await fetchSwitchMappingResponse(
            `${admin ? '/api/admin' : '/api'}/switch-mappings/` +
            `${encodeURIComponent(catalogId)}/image`,
            { cache: 'no-store' },
        );
        if (response.status === 404) return null;
        if (!response.ok) {
            throw new Error(`轴体图片下载失败: HTTP ${response.status}`);
        }
        const image = await response.blob();
        return image.type.startsWith('image/') ? image : null;
    };

    const uploadSwitchMappingImage = async (
        catalogId: string,
        image: File,
    ): Promise<void> => {
        if (!['image/jpeg', 'image/png', 'image/webp'].includes(image.type) ||
            image.size < 12 || image.size > 2 * 1024 * 1024) {
            throw new Error('轴体图片必须是 2 MiB 以内的 JPEG、PNG 或 WebP');
        }
        const response = await fetchSwitchMappingResponse(
            `/api/admin/switch-mappings/${encodeURIComponent(catalogId)}/image`,
            {
                method: 'PUT',
                headers: { 'Content-Type': image.type },
                body: image,
            },
        );
        await readSwitchMappingEnvelope<SwitchMappingCatalogDetail>(response);
    };

    const updateSwitchMappingMetadata = async (
        catalogId: string,
        displayName: string,
        description: string,
    ): Promise<void> => {
        const response = await fetchSwitchMappingResponse(
            `/api/admin/switch-mappings/${encodeURIComponent(catalogId)}`,
            {
                method: 'PATCH',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ displayName, description }),
            },
        );
        await readSwitchMappingEnvelope<SwitchMappingCatalogDetail>(response);
    };

    const updateSwitchMappingCurve = async (
        catalogId: string,
        mapping: SwitchMappingPayload,
    ): Promise<SwitchMappingCatalogDetail> => {
        const response = await fetchSwitchMappingResponse(
            `/api/admin/switch-mappings/${encodeURIComponent(catalogId)}/mapping`,
            {
                method: 'PATCH',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ mapping }),
            },
        );
        return readSwitchMappingEnvelope<SwitchMappingCatalogDetail>(response);
    };

    const deleteSwitchMapping = async (catalogId: string): Promise<void> => {
        const response = await fetchSwitchMappingResponse(
            `/api/admin/switch-mappings/${encodeURIComponent(catalogId)}`,
            { method: 'DELETE' },
        );
        await readSwitchMappingEnvelope<{
            catalogId: string;
            deletedRevisionIds: string[];
            deleted: boolean;
        }>(response);
    };

    const installCanonicalMapping = async (
        mapping: SwitchMappingPayload,
        sha256: string,
    ): Promise<SwitchMappingPayload> => {
        const list = await dependencies.sendDeviceRequest('ms_get_list', {}, true);
        if (list?.storageMode !== 'shared-singleton' || list?.installSchemaVersion !== 1) {
            dependencies.setMappingStorageMode(typeof list?.storageMode === 'string' ? list.storageMode : null);
            throw new Error('设备固件不支持共享单映射安装，请先升级设备固件');
        }
        dependencies.setMappingStorageMode('shared-singleton');
        const computed = await switchMappingSha256(mapping);
        if (!/^[0-9a-f]{64}$/i.test(sha256) || computed !== sha256.toLowerCase()) {
            throw new Error('服务器映射 SHA-256 校验失败');
        }
        const installation = await dependencies.sendDeviceRequest('ms_install_mapping', { mapping, sha256 }, true);
        if (installation?.runtimeReloaded !== true) throw new Error('映射已写入，但输入运行时未确认加载，请重新连接设备');
        const confirmed = await dependencies.sendDeviceRequest('ms_get_mapping', { id: mapping.id }, true);
        const installed = confirmed?.mapping as SwitchMappingPayload | undefined;
        if (!installed || installed.id !== mapping.id ||
            await switchMappingSha256(installed) !== computed) {
            throw new Error('设备映射回读校验失败');
        }
        dependencies.setMappingList([{ id: installed.id, name: installed.name }]);
        dependencies.setDefaultMappingId(installed.id);
        dependencies.setMappingSource('server-installed');
        dependencies.setActiveMapping(installed as ADCValuesMapping);
        return installed;
    };

    const installSwitchMapping = async (
        catalogId: string,
        prefetchedDetail?: SwitchMappingCatalogDetail,
    ): Promise<SwitchMappingPayload> => {
        const detail = prefetchedDetail || await fetchSwitchMappingDetail(catalogId);
        if (!detail.revision?.mapping || detail.revision.revisionId !== detail.revision.mapping.id) {
            throw new Error('服务器返回的映射结构无效');
        }
        return installCanonicalMapping(detail.revision.mapping, detail.revision.sha256);
    };

    const clearInstalledSwitchMapping = async (
        mappingId: string,
    ): Promise<ADCValuesMapping> => {
        const data = await dependencies.sendDeviceRequest(
            'ms_clear_installed_mapping',
            { id: mappingId },
            true,
        );
        const fallback = data?.mapping as ADCValuesMapping | undefined;
        if (!fallback?.id || data?.source !== 'factory-fallback') {
            throw new Error('设备未能恢复固件兜底映射');
        }
        dependencies.setMappingList(Array.isArray(data.mappingList)
            ? data.mappingList as { id: string; name: string }[]
            : [{ id: fallback.id, name: fallback.name }]);
        dependencies.setDefaultMappingId(typeof data.defaultMappingId === 'string'
            ? data.defaultMappingId
            : fallback.id);
        dependencies.setMappingStorageMode('shared-singleton');
        dependencies.setMappingSource('factory-fallback');
        dependencies.setActiveMapping(fallback);
        return fallback;
    };

    const createSwitchMappingFromCurrent = async (input: {
        displayName: string;
        description: string;
        length: number;
        step: number;
        image?: File | null;
    }): Promise<SwitchMappingCatalogDetail> => {
        if (!dependencies.deviceClient) throw new Error('设备命令客户端未初始化');
        const response = await dependencies.deviceClient.authorizedFetch(
            '/api/admin/switch-mappings/blank',
            {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({
                    displayName: input.displayName,
                    description: input.description,
                    length: input.length,
                    step: input.step,
                }),
            },
            ['config.read'],
        );
        const created = await readSwitchMappingEnvelope<SwitchMappingCatalogDetail>(response);
        const revision = created.revision;
        if (!revision?.mapping || revision.mapping.id !== revision.revisionId) {
            throw new Error('服务器未返回标定草稿');
        }
        if (input.image) {
            await uploadSwitchMappingImage(created.catalogId, input.image);
        }
        return created;
    };

    const beginMappingDraft = async (name: string, length: number, step: number): Promise<void> => {
        const data = await dependencies.sendDeviceRequest(
            'ms_mapping_draft_begin',
            { name, length, step },
            true,
        );
        if (!data?.status) throw new Error('设备未返回标定草稿状态');
        dependencies.setMarkingStatus(data.status as StepInfo);
    };

    const publishMappingDraft = async (input: {
        catalogId?: string;
        displayName: string;
        description: string;
    }): Promise<SwitchMappingCatalogDetail> => {
        if (!dependencies.deviceClient) throw new Error('设备命令客户端未初始化');
        const draft = await dependencies.sendDeviceRequest('ms_mapping_draft_get', {}, true);
        const mapping = draft?.mapping as Omit<SwitchMappingPayload, 'id'> | undefined;
        if (!mapping) throw new Error('标定草稿尚未完成');
        const response = await dependencies.deviceClient.authorizedFetch(
            '/api/admin/switch-mappings/drafts',
            {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ ...input, catalogId: input.catalogId || undefined, mapping }),
            },
            ['config.read'],
        );
        const hiddenDraft = await readSwitchMappingEnvelope<SwitchMappingCatalogDetail>(response);
        const revision = hiddenDraft.revision;
        if (!revision?.mapping || revision.mapping.id !== revision.revisionId) {
            throw new Error('服务器未返回规范化草稿版本');
        }

        // Publication is an explicit operation in the resource administration page.
        return hiddenDraft;
    };

    const updateMarkingStatus = (status: StepInfo) => {
        dependencies.setMarkingStatus(status);
    };
    return {
        fetchMappingList, fetchDefaultMapping, createMapping, deleteMapping, updateDefaultMapping,
        startMarking, stopMarking, stepMarking, fetchMarkingStatus, syncMarkingProgress,
        fetchActiveMapping, renameMapping, readSwitchMappingEnvelope, fetchSwitchMappingResponse,
        fetchDeviceAuthorizedResource, fetchSwitchMappingCatalog, fetchSwitchMappingDetail,
        fetchSwitchMappingImage, uploadSwitchMappingImage, updateSwitchMappingMetadata,
        updateSwitchMappingCurve, deleteSwitchMapping, installCanonicalMapping, installSwitchMapping,
        clearInstalledSwitchMapping, createSwitchMappingFromCurrent, beginMappingDraft,
        publishMappingDraft, updateMarkingStatus,
    };
}
