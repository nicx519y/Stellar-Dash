// ADC 按钮错误码
export enum ADCBtnsError {
    SUCCESS = 0,
    INVALID_PARAMS = -1,
    MEMORY_ERROR = -2,
    NOT_INITIALIZED = -3,
    ALREADY_INITIALIZED = -4,
    GAMEPAD_PROFILE_NOT_FOUND = -5,
    ADC1_CALIB_FAILED = -10,
    ADC2_CALIB_FAILED = -11,
    DMA1_START_FAILED = -12,
    DMA2_START_FAILED = -13,
    DMA_ALREADY_STARTED = -14,
    DMA_NOT_STARTED = -15,
    MAPPING_NOT_FOUND = -20,
    MAPPING_ALREADY_EXISTS = -21,
    MAPPING_CREATE_FAILED = -22,
    MAPPING_UPDATE_FAILED = -23,
    MAPPING_DELETE_FAILED = -24,
    MAPPING_INVALID_RANGE = -25,
    MAPPING_STORAGE_FULL = -26,
    MAPPING_STORAGE_EMPTY = -27,
    CALIBRATION_IN_PROGRESS = -30,
    CALIBRATION_NOT_STARTED = -31,
    CALIBRATION_INVALID_DATA = -32,
    CALIBRATION_FAILED = -33,
    ALREADY_MARKING = -40,
    ALREADY_SAMPLING = -41,
    NOT_MARKING = -42,
    MARKING_VALUE_FULL = -43,
    MARKING_VALUE_OVERFLOW = -44
}

// 步进信息结构体
export interface StepInfo {
    id: string;
    mapping_name: string;
    step: number;
    length: number;
    index: number;
    values: number[];
    is_marking: boolean;
    is_sampling: boolean;
    is_completed: boolean;
    sampling_noise: number;
    sampling_frequency: number;
}

// ADC值映射结构体
export interface ADCValuesMapping {
    id: string;
    name: string;
    length: number;
    step: number;
    samplingFrequency: number;
    samplingNoise: number;
    originalValues: number[];
    calibratedValues: number[];
} 