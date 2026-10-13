import {
    DEFAULT_DEVICE_TRANSPORT_CONFIG,
    DeviceCommandClient,
    DeviceConnectionError,
    DeviceConnectionPhase,
    DeviceSession,
    DeviceTransportState,
    PerformanceTelemetryController,
    PostReadyRequestScheduler
} from '@/lib/device-transport';
import { useRef, useState } from 'react';
import { DeviceTransportConfigType, HitboxLayoutItem } from './contract';

export function useSessionState() {
    const [operationLoading, setIsLoading] = useState(false);
    const [rfBindingBusy, setRfBindingBusy] = useState(false);
    const rfBindingBusyRef = useRef(false);
    const rfBindingConnectionEpoch = useRef(0);
    const rfBindingOwnerRef = useRef<{ client: DeviceCommandClient; epoch: number } | null>(null);
    const [error, setError] = useState<string | null>(null);
    const [jsReady, setJsReady] = useState(false);
    const [deviceConnected, setDeviceConnected] = useState(false);
    const [deviceState, setDeviceState] = useState<DeviceTransportState>(DeviceTransportState.DISCONNECTED);
    const [devicePhase, setDevicePhase] = useState<DeviceConnectionPhase>(DeviceConnectionPhase.IDLE);
    const [deviceError, setDeviceError] = useState<DeviceConnectionError | null>(null);
    const [deviceClient, setDeviceClient] = useState<DeviceCommandClient | null>(null);
    const deviceClientRef = useRef<DeviceCommandClient | null>(null);
    const [deviceSession, setDeviceSession] = useState<DeviceSession | null>(null);
    const [showReconnect, setShowReconnect] = useState(false);
    const [transportConfig, setTransportConfig] = useState<DeviceTransportConfigType>(DEFAULT_DEVICE_TRANSPORT_CONFIG);
    const [userRebooting, setUserRebooting] = useState(false);
    const connectionWatchdogPausedRef = useRef(false);
    const [hitboxLayout, setHitboxLayout] = useState<HitboxLayoutItem[]>([]);
    const initializationGenerationRef = useRef(0);
    const postReadyRequestSchedulerRef = useRef<PostReadyRequestScheduler | null>(null);
    const performanceTelemetryRef = useRef<PerformanceTelemetryController | null>(null);
    const initialAutoConnectClientRef = useRef<DeviceCommandClient | null>(null);
    const initialAutoConnectCancelRef = useRef<(() => void) | null>(null);
    const foregroundReconnectCancelRef = useRef<(() => void) | null>(null);
    const pageHiddenRef = useRef(
        typeof document !== 'undefined' && document.visibilityState === 'hidden',
    );
    if (!postReadyRequestSchedulerRef.current) {
        postReadyRequestSchedulerRef.current = new PostReadyRequestScheduler();
    }
    return {
        operationLoading, setIsLoading, rfBindingBusy, setRfBindingBusy, rfBindingBusyRef,
        rfBindingConnectionEpoch, rfBindingOwnerRef, error, setError, jsReady, setJsReady,
        deviceConnected, setDeviceConnected, deviceState, setDeviceState, devicePhase, setDevicePhase,
        deviceError, setDeviceError, deviceClient, setDeviceClient, deviceClientRef, deviceSession,
        setDeviceSession, showReconnect, setShowReconnect, transportConfig, setTransportConfig,
        userRebooting, setUserRebooting, connectionWatchdogPausedRef, hitboxLayout, setHitboxLayout,
        initializationGenerationRef, postReadyRequestSchedulerRef, performanceTelemetryRef,
        initialAutoConnectClientRef, initialAutoConnectCancelRef, foregroundReconnectCancelRef,
        pageHiddenRef,
    };
}
