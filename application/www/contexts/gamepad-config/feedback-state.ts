import {
    SharedButtonMonitorLease
} from '@/lib/button-monitor-lifecycle';
import { LedPreviewCoordinator } from '@/lib/led-preview-coordinator';
import {
    LEDsConfig
} from '@/types/types';
import { useRef, useState } from 'react';

export function useFeedbackState() {
    const [buttonMonitoringActive, setButtonMonitoringActive] = useState<boolean>(false);
    const buttonMonitorLeaseRef = useRef<SharedButtonMonitorLease | null>(null);
    const ledPreviewRef = useRef(new LedPreviewCoordinator<LEDsConfig>());
    const screenPreviewRef = useRef<number | null>(null);
    const feedbackSuspendedRef = useRef(false);
    const longDeviceActivityRef = useRef(false);
    if (!buttonMonitorLeaseRef.current) {
        buttonMonitorLeaseRef.current = new SharedButtonMonitorLease();
    }
    return {
        buttonMonitoringActive, setButtonMonitoringActive, buttonMonitorLeaseRef, ledPreviewRef,
        screenPreviewRef, feedbackSuspendedRef, longDeviceActivityRef,
    };
}
