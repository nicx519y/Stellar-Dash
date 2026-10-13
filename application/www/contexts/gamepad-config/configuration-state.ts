import {
    DeferredConfigCoordinator,
    type ConfigSyncState,
} from '@/lib/deferred-config-coordinator';
import { type ConfigSyncProgress } from '@/lib/device-transport/config-sync';
import { SessionConfigStore, type ConfigDraftBackup } from '@/lib/session-config-store';
import {
    DEFAULT_SCREEN_CONTROL_CONFIG,
    GameProfile,
    GameProfileList, GlobalConfig,
    Hotkey,
    Platform,
    ScreenControlConfig
} from '@/types/gamepad-config';
import { useRef, useState } from 'react';

export function useConfigurationState() {
    const [globalConfig, setGlobalConfig] = useState<GlobalConfig>({ inputMode: Platform.XINPUT });
    const globalConfigRef = useRef<GlobalConfig>({ inputMode: Platform.XINPUT });
    const [screenControl, setScreenControl] = useState<ScreenControlConfig>(DEFAULT_SCREEN_CONTROL_CONFIG);
    const screenControlRef = useRef<ScreenControlConfig>(DEFAULT_SCREEN_CONTROL_CONFIG);
    const [profileList, setProfileList] = useState<GameProfileList>({ defaultId: "", maxNumProfiles: 0, items: [] });
    const [defaultProfile, setDefaultProfile] = useState<GameProfile>({ id: "", name: "" });
    const [hotkeysConfig, setHotkeysConfig] = useState<Hotkey[]>([]);
    const [dataIsReady, setDataIsReady] = useState(false);
    const [finishConfigDisabled, setFinishConfigDisabled] = useState(false);
    const [deferredConfigDirty, setDeferredConfigDirty] = useState(false);
    const [deferredConfigSaving, setDeferredConfigSaving] = useState(false);
    const configStoreRef = useRef(new SessionConfigStore());
    const detachedDraftsRef = useRef(new Map<string, ConfigDraftBackup>());
    const configReadyRef = useRef(false);
    const [configRecovery, setConfigRecovery] = useState<string[]>([]);
    const recoveryRef = useRef<ConfigDraftBackup | null>(null);
    const [configReadProgress, setConfigReadProgress] = useState<ConfigSyncProgress>({ completed: 0, total: 0 });
    const [configSyncState, setConfigSyncState] = useState<ConfigSyncState>({ pendingCount: 0, saving: false, paused: true, error: null });
    const [configBoundaryBusy, setConfigBoundaryBusy] = useState(false);
    const boundaryCountRef = useRef(0);
    const autoFlushRef = useRef<() => Promise<void>>(async () => { });
    const deferredConfigRef = useRef<DeferredConfigCoordinator | null>(null);
    const deferredFlushTailRef = useRef<Promise<void>>(Promise.resolve());
    if (!deferredConfigRef.current) {
        deferredConfigRef.current = new DeferredConfigCoordinator(setDeferredConfigDirty, {
            autoFlush: () => autoFlushRef.current(), onState: setConfigSyncState,
        });
    }
    return {
        globalConfig, setGlobalConfig, globalConfigRef, screenControl, setScreenControl,
        screenControlRef, profileList, setProfileList, defaultProfile, setDefaultProfile, hotkeysConfig,
        setHotkeysConfig, dataIsReady, setDataIsReady, finishConfigDisabled, setFinishConfigDisabled,
        deferredConfigDirty, setDeferredConfigDirty, deferredConfigSaving, setDeferredConfigSaving,
        configStoreRef, detachedDraftsRef, configReadyRef, configRecovery, setConfigRecovery,
        recoveryRef, configReadProgress, setConfigReadProgress, configSyncState, setConfigSyncState,
        configBoundaryBusy, setConfigBoundaryBusy, boundaryCountRef, autoFlushRef, deferredConfigRef,
        deferredFlushTailRef,
    };
}
