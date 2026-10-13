import { type FirmwareInventory } from '@/lib/device-transport/release-install-client';
import {
    DEFAULT_FIRMWARE_PACKAGE_CHUNK_SIZE,
    DEFAULT_FIRMWARE_UPGRADE_TIMEOUT
} from '@/types/gamepad-config';
import {
    DeviceFirmwareInfo,
    FirmwareUpdateCheckResponse,
    FirmwareUpgradeConfig,
    FirmwareUpgradeSession
} from '@/types/types';
import { useRef, useState } from 'react';
import { FIRMWARE_SERVER_CONFIG } from './contract';

export function useFirmwareState() {
    const [firmwareInfo, setFirmwareInfo] = useState<DeviceFirmwareInfo | null>(null);
    const [firmwareUpdateInfo, setFirmwareUpdateInfo] = useState<FirmwareUpdateCheckResponse | null>(null);
    const [firmwareServerHost, _setFirmwareServerHostState] = useState<string>(FIRMWARE_SERVER_CONFIG.defaultHost);
    const [upgradeSession, setUpgradeSession] = useState<FirmwareUpgradeSession | null>(null);
    const [upgradeConfig, setUpgradeConfigState] = useState<FirmwareUpgradeConfig>({
        chunkSize: DEFAULT_FIRMWARE_PACKAGE_CHUNK_SIZE, // 4KB默认分片大小
        timeout: DEFAULT_FIRMWARE_UPGRADE_TIMEOUT // 30秒超时
    });
    const [firmwareUpdating, setFirmwareUpdating] = useState(false);
    const releaseActivityRef = useRef(false);
    const releaseInventoryRequestRef = useRef<Promise<FirmwareInventory> | null>(null);

    return {
        firmwareInfo, setFirmwareInfo, firmwareUpdateInfo, setFirmwareUpdateInfo, firmwareServerHost,
        _setFirmwareServerHostState, upgradeSession, setUpgradeSession, upgradeConfig,
        setUpgradeConfigState, firmwareUpdating, setFirmwareUpdating, releaseActivityRef,
        releaseInventoryRequestRef,
    };
}
