import { verifyResource, type ResourceInventory, type ResourceSender } from '@/lib/resources';
import { SessionConfigStore } from '@/lib/session-config-store';
import { switchMappingSha256, type SwitchMappingPayload } from '@/types/adc';
import type { GameProfile } from '@/types/gamepad-config';
import type { MutableRefObject } from 'react';
import type { DeviceRequestSender } from './contract';

interface ResourceDependencies {
    sendDeviceRequest: DeviceRequestSender;
    runConfigExclusive: <T>(operation: () => Promise<T>, refresh?: boolean) => Promise<T>;
    configStoreRef: MutableRefObject<SessionConfigStore>;
    publishConfig: () => void;
    installCanonicalMapping: (mapping: SwitchMappingPayload, sha256: string) => Promise<SwitchMappingPayload>;
}

/** Resource writes use the same configuration boundary as calibration and upgrades. */
export function createResourceActions(dependencies: ResourceDependencies) {
    const resourceCommand: ResourceSender = (command, params = {}) => {
        if (!["resources_list", "resources_status", "resources_get"].includes(command)) {
            return Promise.reject(new Error("Use resourceTransaction for writes"));
        }
        return dependencies.sendDeviceRequest(command, params, true);
    };
    const resourceTransaction = <T>(operation: (send: ResourceSender) => Promise<T>): Promise<T> =>
        dependencies.runConfigExclusive(() => operation((command, params = {}) =>
            dependencies.sendDeviceRequest(command, params, true)), true);
    const syncLightingResourceReferences = (profiles: ResourceInventory['profiles']) => {
        const store = dependencies.configStoreRef.current;
        let changed = false;
        for (const { profileId, keys, ambient } of profiles) {
            const key = `profile:${profileId}`;
            const confirmed = store.confirmed[key] as GameProfile | undefined;
            if (!confirmed || JSON.stringify(confirmed.lightingResources) === JSON.stringify({ keys, ambient })) continue;
            store.refresh(key, { ...confirmed, lightingResources: { keys, ambient } });
            changed = true;
        }
        if (changed) dependencies.publishConfig();
    };
    const installMappingResource = (bytes: Uint8Array) => dependencies.runConfigExclusive(async () => {
        const source = await verifyResource(bytes);
        if (source.type !== 'switch-mapping') throw new Error('Expected a switch mapping resource');
        const mapping = { ...source.payload, id: source.resourceId } as SwitchMappingPayload;
        return dependencies.installCanonicalMapping(mapping, await switchMappingSha256(mapping));
    }, true);
    return { resourceCommand, resourceTransaction, syncLightingResourceReferences, installMappingResource };
}
