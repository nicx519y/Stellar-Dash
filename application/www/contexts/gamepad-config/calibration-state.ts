import {
    ADCValuesMapping,
    StepInfo
} from '@/types/adc';
import {
    CalibrationStatus
} from '@/types/types';
import { useRef, useState } from 'react';
import { makeEmptyCalibrationStatus, makeEmptyMarkingStatus } from './contract';

export function useCalibrationState() {
    const [defaultMappingId, setDefaultMappingId] = useState<string>("");
    const [mappingList, setMappingList] = useState<{ id: string, name: string }[]>([]);
    const [mappingStorageMode, setMappingStorageMode] = useState<string | null>(null);
    const [mappingSource, setMappingSource] = useState<'server-installed' | 'factory-fallback' | null>(null);
    const [markingStatus, setMarkingStatus] = useState<StepInfo>(makeEmptyMarkingStatus);
    const [activeMapping, setActiveMapping] = useState<ADCValuesMapping | null>(null);
    const [calibrationStatus, setCalibrationStatus] = useState<CalibrationStatus>(makeEmptyCalibrationStatus);
    const calibrationCompletionRequestRef = useRef<{
        generation: number;
        promise: Promise<boolean>;
    } | null>(null);

    return {
        defaultMappingId, setDefaultMappingId, mappingList, setMappingList, mappingStorageMode,
        setMappingStorageMode, mappingSource, setMappingSource, markingStatus, setMarkingStatus,
        activeMapping, setActiveMapping, calibrationStatus, setCalibrationStatus,
        calibrationCompletionRequestRef,
    };
}
