import { promises as fs } from 'fs';
import path from 'path';
import { 
    GamepadConfig,
    GameProfileList,
    GameProfile,
    LedsEffectStyle,
    GameSocdMode,
    Platform
} from '@/types/gamepad-config';

const dataFilePath = path.join(process.cwd(), 'app/api/data/data.json');

/**
 * 读取配置
 */
async function readConfig(): Promise<GamepadConfig> {
    const data = await fs.readFile(dataFilePath, 'utf8');
    return JSON.parse(data);
}

/**
 * 写入配置
 */
async function writeConfig(config: GamepadConfig): Promise<void> {
    await fs.writeFile(dataFilePath, JSON.stringify(config, null, 2), 'utf8');
}

/**
 * 获取profile列表
 */
export async function getProfileList(): Promise<GameProfileList> {
    const config = await readConfig();
    return {
        defaultId: config.defaultProfileId ?? "",
        maxNumProfiles: config.numProfilesMax ?? 0,
        items: config.profiles?.map(p => ({
            id: p.id ?? "",
            name: p.name ?? "",
        })) ?? [],
    };
}

/**
 * 获取指定profile的详情
 */
export async function getProfileDetails(profileId: string): Promise<GameProfile | null> {
    const config = await readConfig();
    return config.profiles?.find(p => p.id === profileId) ?? null;
}

/**
 * 更新配置
 */
export async function updateConfig(newConfig: Partial<GamepadConfig>): Promise<void> {
    const config = await readConfig();
    await writeConfig({ ...config, ...newConfig });
}

/**
 * 获取完整配置
 */
export async function getConfig(): Promise<GamepadConfig> {
    return await readConfig();
}

/**
 * 获取初始化配置的profile详情
 */
export async function getInitialProfileDetails(id: string, name: string): Promise<GameProfile> {
    return {
        id,
        name,
        keysConfig: {
            invertXAxis: false,
            invertYAxis: false,
            fourWayMode: false,
            socdMode: GameSocdMode.SOCD_MODE_NEUTRAL,
            inputMode: Platform.XINPUT,
            keyMapping: {},
        },
        ledsConfigs: {
            ledEnabled: false,
            ledsEffectStyle: LedsEffectStyle.STATIC,
            ledColors: [
                "#000000",
                "#000000",
                "#000000",
            ],  
            ledBrightness: 100
        },
        triggerConfigs: {
            isAllBtnsConfiguring: true,
            triggerConfigs: [],
        }
    };
}

