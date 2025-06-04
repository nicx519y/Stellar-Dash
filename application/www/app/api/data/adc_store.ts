import fs from 'fs';
import path from 'path';
import { v4 as uuidv4 } from 'uuid';
import { ADCBtnsError, StepInfo, ADCValuesMapping } from '@/types/adc';

const DATA_FILE = path.join(process.cwd(), 'app/api/data/adc_mapping.json');

const NUM_ADC_VALUES_MAPPING = 8;

interface ADCMappingData {
    default_mapping_id: string;
    mappings: ADCValuesMapping[];
    marking_status: StepInfo;
}

// 读取数据
function readData(): ADCMappingData {
    try {
        const data = fs.readFileSync(DATA_FILE, 'utf8');
        return JSON.parse(data) as ADCMappingData;
    } catch {
        return {
            default_mapping_id: "",
            mappings: [],
            marking_status: {
                id: "",
                mapping_name: "",
                step: 0,
                length: 0,
                index: 0,
                values: [],
                is_marking: false,
                is_sampling: false,
                is_completed: false
            }
        };
    }
}

// 写入数据
function writeData(data: ADCMappingData) {
    fs.writeFileSync(DATA_FILE, JSON.stringify(data, null, 4));
}

// 创建映射
export function createMapping(name: string, length: number, step: number): ADCBtnsError {
    const data = readData();

    // 检查是否已存在
    if (data.mappings.some(m => m.name === name)) {
        return ADCBtnsError.MAPPING_ALREADY_EXISTS;
    }

    if(data.mappings.length >= NUM_ADC_VALUES_MAPPING) {
        return ADCBtnsError.MAPPING_STORAGE_FULL;
    }

    // 创建新映射
    const newMapping: ADCValuesMapping = {
        id: uuidv4(),
        name,
        length,
        step,
        originalValues: new Array(length).fill(0),
        calibratedValues: new Array(length).fill(0)
    };

    data.mappings.push(newMapping);
    writeData(data);
    return ADCBtnsError.SUCCESS;
}

// 删除映射
export function deleteMapping(id: string): ADCBtnsError {
    const data = readData();

    if(data.mappings.length <= 1) {
        return ADCBtnsError.MAPPING_STORAGE_EMPTY;
    }

    const index = data.mappings.findIndex(m => m.id === id);
    
    if (index === -1) {
        return ADCBtnsError.MAPPING_NOT_FOUND;
    }

    const mapping = data.mappings[index];
    data.mappings.splice(index, 1);

    if (data.default_mapping_id === mapping.id) {
        data.default_mapping_id = "";
    }
    writeData(data);
    return ADCBtnsError.SUCCESS;
}

// 获取映射列表
export function getMappingList(): { id: string, name: string }[] {
    const data = readData();
    if(data && data.mappings && data.mappings.length > 0) {
        return data.mappings.map(m => ({ id: m.id, name: m.name }));
    }
    return [];
}

// 获取默认映射
export function getDefaultMapping(): string | null {
    const data = readData();
    if(data && data.default_mapping_id && data.default_mapping_id !== "") {
        return data.default_mapping_id;
    } else if(data && data.mappings && data.mappings.length > 0) {
        return data.mappings[0].id;
    }
    return null;
}

// 获取映射
export function getMapping(id: string): ADCValuesMapping | null {
    const data = readData();
    const mapping = data.mappings.find(m => m.id === id);
    return mapping ? mapping : null;
}

// 设置默认映射
export function setDefaultMapping(id: string): ADCBtnsError {
    const data = readData();
    const mapping = data.mappings.find(m => m.id === id);
    if (!mapping) {
        return ADCBtnsError.MAPPING_NOT_FOUND;
    }

    data.default_mapping_id = mapping.id;
    writeData(data);
    return ADCBtnsError.SUCCESS;
}

const stepInfo: StepInfo = {
    id: "",
    mapping_name: "",
    step: 0,
    length: 0,
    index: 0,
    values: [],
    is_marking: false,
    is_sampling: false,
    is_completed: false
};

// 开始标记
export function startMarking(id: string): ADCBtnsError {
    if(stepInfo.is_marking) {
        return ADCBtnsError.ALREADY_MARKING;
    }

    const data = readData();
    const mapping = data.mappings.find(m => m.id === id);
    if (!mapping) {
        return ADCBtnsError.MAPPING_NOT_FOUND;
    }
    // 初始化标记状态
    Object.assign(stepInfo, {
        id: mapping.id,
        mapping_name: mapping.name,
        step: mapping.step,
        length: mapping.length,
        index: 0,
        values: [], 
        is_marking: true,
        is_sampling: false,
        is_completed: false
    });

    console.log("startMarking - stepInfo: ", stepInfo);

    return ADCBtnsError.SUCCESS;
}

// 停止标记
export function stopMarking(): ADCBtnsError {
    if (!stepInfo.is_marking) {
        return ADCBtnsError.NOT_MARKING;
    }

    Object.assign(stepInfo, {
        is_marking: false,
        is_sampling: false,
        is_completed: true
    });

    console.log("stopMarking - stepInfo: ", stepInfo);

    return ADCBtnsError.SUCCESS;
}

// 标记步进
export function stepMarking(): ADCBtnsError {
    // 如果未开始标记，则不能步进
    if(!stepInfo.is_marking) {
        return ADCBtnsError.NOT_MARKING;
    }

    // 如果正在采样，则不能步进
    if (stepInfo.is_sampling) {
        return ADCBtnsError.ALREADY_SAMPLING;
    }
    
    // 如果标记完成，则保存数据
    if (stepInfo.index >= stepInfo.length) {
        stepInfo.is_completed = true;
        stepInfo.is_marking = false;
        stepInfo.is_sampling = false;
        
        const data = readData();
        const mapping = data.mappings.find(m => m.name === stepInfo.mapping_name);
        if (!mapping) {
            return ADCBtnsError.MAPPING_NOT_FOUND;
        }

        const len = stepInfo.values.length;
        for (let i = 0; i < len; i++) {
            mapping.originalValues[i] = stepInfo.values[i];
            mapping.calibratedValues[i] = stepInfo.values[i];
        }

        writeData(data);
        console.log("stepMarking - finished: ", stepInfo);
        return ADCBtnsError.SUCCESS;
    }

    // 如果未采样，则采样
    if(!stepInfo.is_sampling) {
        setTimeout(() => {
            // 模拟ADC采样值
            const sampleValue = Math.floor(Math.random() * 0xFFFF);
            stepInfo.values.push(sampleValue);
            stepInfo.index++;
            stepInfo.is_sampling = false;
        }, 1000);

        console.log("stepMarking - sampling: ", stepInfo);

        stepInfo.is_sampling = true;
    }

    return ADCBtnsError.SUCCESS;
}

// 获取标记状态
export function getMarkingStatus(): StepInfo {
    return stepInfo;
} 


// 重命名映射
export function renameMapping(id: string, name: string): ADCBtnsError {
    const data = readData();

    if(name === "") {
        return ADCBtnsError.INVALID_PARAMS;
    }

    if(data.mappings.some(m => m.name === name)) {
        return ADCBtnsError.MAPPING_ALREADY_EXISTS;
    }

    if(name.length > 16) {
        return ADCBtnsError.INVALID_PARAMS;
    }

    const mapping = data.mappings.find(m => m.id === id);
    if (!mapping) {
        return ADCBtnsError.MAPPING_NOT_FOUND;
    }
    mapping.name = name;
    writeData(data);
    return ADCBtnsError.SUCCESS;
}