import { GamePadColor } from "./gamepad-color";

export const NUM_PROFILES_MAX = 8;
// LEDS animation cycle in milliseconds 
export const LEDS_ANIMATION_CYCLE = 10000;
// LEDS animation speed
export const LEDS_ANIMATION_SPEED = 3;
// LEDS animation step in milliseconds
export const LEDS_ANIMATION_STEP = 80;
// Default number of hotkeys max
export const DEFAULT_NUM_HOTKEYS_MAX = 11;
// max number of key binding per button
export const NUM_BIND_KEY_PER_BUTTON_MAX = 3;
// max length of profile name
export const PROFILE_NAME_MAX_LENGTH = 20;

// max length of switch marking name
export const SWITCH_MARKING_NAME_MAX_LENGTH = 16;

// firmware package chunk size
export const DEFAULT_FIRMWARE_PACKAGE_CHUNK_SIZE = 4096 * 2;
// firmware upgrade max retries
export const DEFAULT_FIRMWARE_UPGRADE_MAX_RETRIES = 3;
// firmware upgrade timeout
export const DEFAULT_FIRMWARE_UPGRADE_TIMEOUT = 30000;
// firmware server host
export const DEFAULT_FIRMWARE_SERVER_HOST = 'https://firmware.st-dash.com';
// 
// combination key max length
export const COMBINATION_KEY_MAX_LENGTH = 5;
// max number of button combination
export const MAX_NUM_BUTTON_COMBINATION = 5;

// keys settings interactive ids
export const KEYS_SETTINGS_INTERACTIVE_IDS = [ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19 ]; // 0-19 共20个按键可以交互，并设置为按键

// leds settings interactive ids
export const LEDS_SETTINGS_INTERACTIVE_IDS = [ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20 ]; // 0-20 共21个按键可以交互，并设置为led

// hotkeys settings interactive ids
export const HOTKEYS_SETTINGS_INTERACTIVE_IDS = [ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19 ]; // 0-19 共20个按键可以交互，并设置为hotkey

// rapid trigger settings interactive ids
export const RAPID_TRIGGER_SETTINGS_INTERACTIVE_IDS = [ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 ]; // 0-16 共17个按键可以交互，并设置为rapid trigger



export enum Platform {
    XINPUT = "XINPUT",
    PS4 = "PS4",
    PS5 = "PS5",
    XBOX = "XBOX",
    SWITCH = "SWITCH",
}

export const PlatformList = Object.values(Platform);

export const PlatformLabelMap = new Map<Platform, { label: string, description: string }>([
    [Platform.XINPUT, { 
        label: "Windows PC", 
        description: "Xbox/Windows controller" 
    }],
    [Platform.PS4, { 
        label: "PlayStation 4", 
        description: "PlayStation 4 controller" 
    }],
    [Platform.PS5, { 
        label: "PlayStation 5", 
        description: "PlayStation 5 controller" 
    }],
    [Platform.XBOX, { 
        label: "Xbox One|Series", 
        description: "Xbox controller" 
    }],
    [Platform.SWITCH, { 
        label: "Nintendo Switch", 
        description: "Nintendo Switch controller" 
    }],
]);

export interface GlobalConfig {
    inputMode?: Platform;
    autoCalibrationEnabled?: boolean;
    manualCalibrationActive?: boolean;
}

export enum GameControllerButton {
    DPAD_UP = "DPAD_UP",
    DPAD_DOWN = "DPAD_DOWN",
    DPAD_LEFT = "DPAD_LEFT",
    DPAD_RIGHT = "DPAD_RIGHT",
    L1 = "L1",
    R1 = "R1",
    L2 = "L2",
    R2 = "R2",
    L3 = "L3",
    R3 = "R3",
    B1 = "B1",
    B2 = "B2",
    B3 = "B3",
    B4 = "B4",
    S1 = "S1",
    S2 = "S2",
    A1 = "A1",
    A2 = "A2",
}

export const GameControllerButtonList = [
    GameControllerButton.DPAD_UP,
    GameControllerButton.DPAD_LEFT,
    GameControllerButton.DPAD_RIGHT,
    GameControllerButton.DPAD_DOWN,

    GameControllerButton.B4,
    GameControllerButton.B3,
    GameControllerButton.B2,
    GameControllerButton.B1,

    GameControllerButton.L3,
    GameControllerButton.R3,
    GameControllerButton.L2,
    GameControllerButton.R2,
    GameControllerButton.L1,
    GameControllerButton.R1,

    GameControllerButton.S1,
    GameControllerButton.S2,
    GameControllerButton.A1,
    GameControllerButton.A2
];

export const XInputButtonMap = new Map<GameControllerButton, string>([
    [GameControllerButton.DPAD_UP, "UP"],
    [GameControllerButton.DPAD_DOWN, "DOWN"],
    [GameControllerButton.DPAD_LEFT, "LEFT"],
    [GameControllerButton.DPAD_RIGHT, "RIGHT"],
    [GameControllerButton.S1, "BACK"],
    [GameControllerButton.S2, "START"],
    [GameControllerButton.A1, "GUIDE"],
    [GameControllerButton.L1, "LB"],
    [GameControllerButton.R1, "RB"],
    [GameControllerButton.L2, "LT"],
    [GameControllerButton.R2, "RT"],
    [GameControllerButton.L3, "LS"],
    [GameControllerButton.R3, "RS"],
    [GameControllerButton.B1, "A"],
    [GameControllerButton.B2, "B"],
    [GameControllerButton.B3, "X"],
    [GameControllerButton.B4, "Y"],
]);

export const PS4ButtonMap = new Map<GameControllerButton, string>([
    [GameControllerButton.DPAD_UP, "UP"],
    [GameControllerButton.DPAD_DOWN, "DOWN"],
    [GameControllerButton.DPAD_LEFT, "LEFT"],
    [GameControllerButton.DPAD_RIGHT, "RIGHT"],
    [GameControllerButton.S1, "SHARE"],
    [GameControllerButton.S2, "OPTIONS"],
    [GameControllerButton.A1, "PS"],
    [GameControllerButton.A2, "TOUCHPAD"],
    [GameControllerButton.L1, "L1"],
    [GameControllerButton.R1, "R1"],
    [GameControllerButton.L2, "L2"],
    [GameControllerButton.R2, "R2"],
    [GameControllerButton.L3, "L3"],
    [GameControllerButton.R3, "R3"],
    [GameControllerButton.B1, "CROSS"],  // SOUTH
    [GameControllerButton.B2, "CIRCLE"],   // EAST
    [GameControllerButton.B3, "SQUARE"],   // WEST
    [GameControllerButton.B4, "TRIANGLE"], // NORTH
]);

export const SwitchButtonMap = new Map<GameControllerButton, string>([
    [GameControllerButton.DPAD_UP, "UP"],
    [GameControllerButton.DPAD_DOWN, "DOWN"],
    [GameControllerButton.DPAD_LEFT, "LEFT"],
    [GameControllerButton.DPAD_RIGHT, "RIGHT"],
    [GameControllerButton.S1, "MINUS"],
    [GameControllerButton.S2, "PLUS"],
    [GameControllerButton.A1, "HOME"],
    [GameControllerButton.A2, "CAPTURE"],
    [GameControllerButton.L1, "L"],
    [GameControllerButton.R1, "R"],
    [GameControllerButton.L2, "ZL"],
    [GameControllerButton.R2, "ZR"],
    [GameControllerButton.L3, "LS"],
    [GameControllerButton.R3, "RS"],
    [GameControllerButton.B1, "B"],
    [GameControllerButton.B2, "A"],
    [GameControllerButton.B3, "Y"],
    [GameControllerButton.B4, "X"],
]);

export type GameProfileList = {
    defaultId: string;
    maxNumProfiles: number;
    items: GameProfile[];
}

export enum GameSocdMode {
    SOCD_MODE_NEUTRAL = 0,                  // 中性 up + down = neutral, left + right = neutral
    SOCD_MODE_UP_PRIORITY = 1,              // 上优先 up + down = up, left + right = neutral
    SOCD_MODE_SECOND_INPUT_PRIORITY = 2,    // 第二输入优先 
    SOCD_MODE_FIRST_INPUT_PRIORITY = 3,     // 第一输入优先 
    SOCD_MODE_BYPASS = 4,                   // 绕过 不做任何处理 所有dpad信号都有效
    SOCD_MODE_NUM_MODES = 5,                // 模式数量
}

export enum HotkeyAction {
    None = "None",
    LedsEffectStyleNext = "LedsEffectStyleNext",
    LedsEffectStylePrev = "LedsEffectStylePrev",
    LedsBrightnessUp = "LedsBrightnessUp",
    LedsBrightnessDown = "LedsBrightnessDown",
    LedsEnableSwitch = "LedsEnableSwitch",
    AmbientLightEffectStyleNext = "AmbientLightEffectStyleNext",
    AmbientLightEffectStylePrev = "AmbientLightEffectStylePrev",
    AmbientLightBrightnessUp = "AmbientLightBrightnessUp",
    AmbientLightBrightnessDown = "AmbientLightBrightnessDown",
    AmbientLightEnableSwitch = "AmbientLightEnableSwitch",
    CalibrationMode = "CalibrationMode",
    WebConfigMode = "WebConfigMode",
    XInputMode = "XInputMode",
    PS4Mode = "PS4Mode",
    PS5Mode = "PS5Mode",
    NSwitchMode = "NSwitchMode",
    XBoxMode = "XBoxMode",
    SystemReboot = "SystemReboot",
}

export const HotkeyActionList = Object.values(HotkeyAction);

// export const HotkeyActionLabelMap = new Map<HotkeyAction, { label: string, description: string }>([
//     [HotkeyAction.None, { 
//         label: "None", 
//         description: "No action" 
//     }],
//     [HotkeyAction.LedsEffectStyleNext, { 
//         label: "Next LED Effect", 
//         description: "Switch to next LED effect style" 
//     }],
//     [HotkeyAction.LedsEffectStylePrev, { 
//         label: "Previous LED Effect", 
//         description: "Switch to previous LED effect style" 
//     }],
//     [HotkeyAction.LedsBrightnessUp, { 
//         label: "Increase Brightness", 
//         description: "Increase LED brightness" 
//     }],
//     [HotkeyAction.LedsBrightnessDown, { 
//         label: "Decrease Brightness", 
//         description: "Decrease LED brightness" 
//     }],
//     [HotkeyAction.LedsEnableSwitch, { 
//         label: "Toggle LEDs", 
//         description: "Enable/Disable LEDs" 
//     }],
//     [HotkeyAction.AmbientLightEffectStyleNext, { 
//         label: "Next Ambient Light Effect", 
//         description: "Switch to next ambient light effect style" 
//     }],
//     [HotkeyAction.AmbientLightEffectStylePrev, { 
//         label: "Previous Ambient Light Effect", 
//         description: "Switch to previous ambient light effect style" 
//     }],
//     [HotkeyAction.AmbientLightBrightnessUp, { 
//         label: "Increase Ambient Light Brightness", 
//         description: "Increase ambient light brightness" 
//     }],
//     [HotkeyAction.AmbientLightBrightnessDown, { 
//         label: "Decrease Ambient Light Brightness", 
//         description: "Decrease ambient light brightness" 
//     }],
//     [HotkeyAction.AmbientLightEnableSwitch, { 
//         label: "Toggle Ambient Light", 
//         description: "Enable/Disable ambient light" 
//     }],
//     [HotkeyAction.WebConfigMode, { 
//         label: "Web Config Mode", 
//         description: "Enter web configuration mode" 
//     }],
//     [HotkeyAction.CalibrationMode, { 
//         label: "Calibration Mode", 
//         description: "Toggle calibration mode" 
//     }],
//     [HotkeyAction.XInputMode, { 
//         label: "XInput Mode", 
//         description: "Switch to XInput mode" 
//     }],
//     [HotkeyAction.PS4Mode, { 
//         label: "PS4 Mode", 
//         description: "Switch to PS4 mode" 
//     }],
//     [HotkeyAction.PS5Mode, { 
//         label: "PS5 Mode", 
//         description: "Switch to PS5 mode" 
//     }],
//     [HotkeyAction.NSwitchMode, { 
//         label: "Nintendo Switch Mode", 
//         description: "Switch to Nintendo Switch mode" 
//     }],
// ]);

export type Hotkey = {
    key: number,
    action: HotkeyAction,
    isLocked?: boolean,
    isHold?: boolean,
}

// 按键组合键配置 说明 哪些游戏控制器按键组合键对应哪些物理按键
export type KeyCombination = {
    keyIndexes: number[];
    gameControllerButtons: GameControllerButton[];
}

export interface KeysConfig {
    inputMode?: Platform;
    socdMode?: GameSocdMode;
    invertXAxis?: boolean;
    invertYAxis?: boolean;
    fourWayMode?: boolean;
    keysEnableTag?: boolean[]; // 按键启用状态数组，对应 NUM_ADC_BUTTONS 个按键
    keyMapping?: {
        [key in GameControllerButton]?: number[];
    };
    keyCombinations?: KeyCombination[]; // 按键组合键配置 说明 哪些游戏控制器按键组合键对应哪些物理按键
}

export interface GameProfile {
    id: string;
    name?: string;
    keysConfig?: KeysConfig;
    triggerConfigs?: {
        isAllBtnsConfiguring?: boolean;
        debounceAlgorithm?: number;
        triggerConfigs?: RapidTriggerConfig[];
    };
    ledsConfigs?: {
        ledEnabled: boolean;
        ledsEffectStyle: LedsEffectStyle;
        ledColors: string[];
        ledBrightness: number;
        ledAnimationSpeed: number;

        // 环绕灯配置
        hasAroundLed?: boolean;
        aroundLedEnabled: boolean;
        aroundLedSyncToMainLed: boolean;
        aroundLedTriggerByButton: boolean;
        aroundLedEffectStyle: AroundLedsEffectStyle;
        aroundLedColors: string[];
        aroundLedBrightness: number;
        aroundLedAnimationSpeed: number;
    };
}

export type ADCButton = {
    virtualPin: number,
    magnettization: number,
    topPosition: number,
    bottomPosition: number,
}

export type GPIOButton = {
    virtualPin: number,
}

export interface GamepadConfig {
    version?: number;
    inputMode?: Platform;
    defaultProfileId?: string;
    numProfilesMax?: number;
    ADCButtons?: ADCButton[];
    GPIOButtons?: GPIOButton[];
    profiles?: GameProfile[];
    hotkeys?: Hotkey[];
    autoCalibrationEnabled?: boolean;
}

// LED 动画效果类型
export enum LedsEffectStyle {
    STATIC = 0,
    BREATHING = 1,
    STAR = 2,
    FLOWING = 3,
    RIPPLE = 4,
    TRANSFORM = 5,
}

export enum AroundLedsEffectStyle {
    STATIC = 0,
    BREATHING = 1,
    QUAKE = 2,
    METEOR = 3,
}

export interface LedsEffectStyleConfig {
    ledEnabled?: boolean;
    ledsEffectStyle?: LedsEffectStyle;
    ledColors?: GamePadColor[];
    brightness?: number;
    animationSpeed?: number; // 1-5

    // 环绕灯配置
    hasAroundLed?: boolean;
    aroundLedEnabled?: boolean;
    aroundLedSyncToMainLed?: boolean;
    aroundLedTriggerByButton?: boolean;
    aroundLedEffectStyle?: AroundLedsEffectStyle;
    aroundLedColors?: GamePadColor[];
    aroundLedBrightness?: number;
    aroundLedAnimationSpeed?: number; // 1-5
}

export type RapidTriggerConfig = {
    topDeadzone: number;
    bottomDeadzone: number;
    pressAccuracy: number;
    releaseAccuracy: number;
}

export enum ButtonPerformancePresetName {
    FASTEST = 'Fastest',
    BALANCED = 'Balanced',
    STABILITY = 'Stability',
    CUSTOM = 'Custom',
}

export const ButtonPerformancePresetConfigs = [
    {
        name: ButtonPerformancePresetName.FASTEST,
        configs: {
            topDeadzone: 0,
            bottomDeadzone: 0,
            pressAccuracy: 0.1,
            releaseAccuracy: 0.01,
        }
    },
    {
        name: ButtonPerformancePresetName.BALANCED,
        configs: {
            topDeadzone: 0.3,
            bottomDeadzone: 0.3,
            pressAccuracy: 0.1,
            releaseAccuracy: 0.1,
        }
    },
    {
        name: ButtonPerformancePresetName.STABILITY,
        configs: {
            topDeadzone: 0.8,
            bottomDeadzone: 1.0,
            pressAccuracy: 0.1,
            releaseAccuracy: 0.1,
        }
    }
]

export enum ADCButtonDebounceAlgorithm {
    NONE = 0,
    NORMAL = 1,
    MAX = 2,
}

export const ledColorsLabel = [ "Front Color", "Back Color 1", "Back Color 2" ];

// UI Text Constants
export const UI_TEXT = {
    // Common Button Labels
    BUTTON_RESET: "Reset",
    BUTTON_SAVE: "Save",
    BUTTON_REBOOT_WITH_SAVING: "Finish Configuration",
    BUTTON_CANCEL: "Cancel",
    BUTTON_SUBMIT: "Submit",
    BUTTON_CONFIRM: "Confirm",
    BUTTON_PREVIEW_IN_TABLE_VIEW: "Preview in Table View",
    BUTTON_EXPORT_CONFIG: "Export Config",
    EXPORT_CONFIG_FAILED_TITLE: "Export Failed",
    EXPORT_CONFIG_FAILED_MESSAGE: "Failed to export configuration",
    BUTTON_IMPORT_CONFIG: "Import Config",
    IMPORT_CONFIG_FAILED_TITLE: "Import Failed",
    IMPORT_CONFIG_FAILED_MESSAGE: "Failed to import configuration",
    IMPORT_CONFIG_SUCCESS_TITLE: "Import Successful",
    IMPORT_CONFIG_SUCCESS_MESSAGE: "Configuration imported successfully. Click Confirm to refresh the page.",
    // Dialog Titles
    DIALOG_REBOOT_CONFIRM_TITLE: "Are you sure?",
    DIALOG_REBOOT_SUCCESS_TITLE: "Reboot successful",
    DIALOG_CREATE_PROFILE_TITLE: "Create New Profile",
    DIALOG_RENAME_PROFILE_TITLE: "Rename Profile",
    
    // Dialog Messages
    DIALOG_REBOOT_CONFIRM_MESSAGE: "Rebooting the system with saving will save the current profile and ending the current session. \nAre you sure to continue?",
    DIALOG_REBOOT_SUCCESS_MESSAGE: "Rebooting the system with saving is successful. \nYou can now close this window and start enjoying the gaming experience.",
    DIALOG_CREATE_PROFILE_CONFIRM_MESSAGE: "Creating a new profile will create a new profile and ending the current session. Are you sure to continue?",
    DIALOG_RENAME_PROFILE_CONFIRM_MESSAGE: "Renaming the current profile will save the current profile and ending the current session. Are you sure to continue?",
    
    // Error Messages
    ERROR_KEY_ALREADY_BINDED_TITLE: "Key already binded",
    ERROR_KEY_ALREADY_BINDED_DESC: "Please select another key, or unbind the key first",
    
    // Profile Related
    PROFILE_CREATE_DIALOG_TITLE: "Create New Profile",
    PROFILE_DELETE_DIALOG_TITLE: "Delete Profile",
    PROFILE_DELETE_CONFIRM_MESSAGE: "Deleting this profile can not be undone or reverted. Are you sure you want to delete this profile?",
    PROFILE_NAME_LABEL: "Profile Name",
    PROFILE_NAME_PLACEHOLDER: "Enter profile name",

    // Calibration
    AUTO_CALIBRATION_TITLE: "Auto Calibration",
    MANUAL_CALIBRATION_TITLE: "Manual Calibration",
    CALIBRATION_HELPER_TEXT: "calibration is the process of finding the optimal button travel for the gamepad. It is a process of finding the optimal button travel for the gamepad.",
    CALIBRATION_START_BUTTON: "Start Calibration",
    CALIBRATION_STOP_BUTTON: "Stop Calibration",
    CALIBRATION_CLEAR_DATA_BUTTON: "Reset Calibration",
    CALIBRATION_CLEAR_DATA_DIALOG_TITLE: "Clear Calibration Data",
    CALIBRATION_CLEAR_DATA_DIALOG_MESSAGE: "Clearing the calibration data will delete all the calibration data. It will need to be calibrated again. Are you sure you want to clear the calibration data?",
    CALIBRATION_COMPLETION_DIALOG_TITLE: "Calibration Completed",
    CALIBRATION_COMPLETION_DIALOG_MESSAGE: "All buttons have been calibrated. Do you want to end the calibration mode? If you want to calibrate again, please clear the calibration data first.",
    CALIBRATION_CHECK_COMPLETED_DIALOG_TITLE: "Has Uncalibrated Buttons",
    CALIBRATION_CHECK_COMPLETED_DIALOG_MESSAGE: "The calibration is not completed, which will cause the buttons to be unusable. Do you want to start the calibration immediately?",

    // Settings Labels
    SETTINGS_SOCD_LABEL: "SOCD Mode",
    SETTINGS_PLATFORM_LABEL: "Platform",
    SETTINGS_LEDS_ENABLE_LABEL: "Enable LEDs",
    SETTINGS_LEDS_EFFECT_LABEL: "LED Effect Style",
    SETTINGS_LEDS_BRIGHTNESS_LABEL: "LED Brightness",
    SETTINGS_LEDS_ANIMATION_SPEED_LABEL: "LED Animation Speed",
    SETTINGS_LEDS_COLOR_FRONT_LABEL: "Front Color",
    SETTINGS_LEDS_COLOR_BACK1_LABEL: "Back Color 1",
    SETTINGS_LEDS_COLOR_BACK2_LABEL: "Back Color 2",

    SETTINGS_AMBIENT_LIGHT_ENABLE_LABEL: "Enable Ambient Light",
    SETTINGS_AMBIENT_LIGHT_SYNC_WITH_LEDS_LABEL: "Sync To Button LED",
    SETTINGS_AMBIENT_LIGHT_TRIGGER_BY_BUTTON_LABEL: "Trigger by Button",
    SETTINGS_AMBIENT_LIGHT_EFFECT_LABEL: "Ambient Light Effect",
    SETTINGS_AMBIENT_LIGHT_BRIGHTNESS_LABEL: "Ambient Light Brightness",
    SETTINGS_AMBIENT_LIGHT_ANIMATION_SPEED_LABEL: "Ambient Light Animation Speed",
    SETTINGS_AMBIENT_LIGHT_COLOR1_LABEL: "Ambient Light Color 1",
    SETTINGS_AMBIENT_LIGHT_COLOR2_LABEL: "Ambient Light Color 2",

    SETTINGS_KEY_MAPPING_AUTO_SWITCH_LABEL: "Auto Switch Mode",
    SETTINGS_KEY_MAPPING_MANUAL_SWITCH_LABEL: "Manual Switch Mode",
    SETTINGS_KEY_MAPPING_CLEAR_MAPPING_BUTTON: "Clear mapping",
    SETTINGS_RAPID_TRIGGER_ONFIGURING_BUTTON: "Configuring button: ",
    SETTINGS_RAPID_TRIGGER_SELECT_A_BUTTON_TO_CONFIGURE: "Select a button to configure",
    SETTINGS_RAPID_TRIGGER_TOP_DEADZONE_LABEL: "Top Deadzone:",
    SETTINGS_RAPID_TRIGGER_BOTTOM_DEADZONE_LABEL: "Bottom Deadzone:",
    SETTINGS_RAPID_TRIGGER_PRESS_ACCURACY_LABEL: "Press Accuracy:",
    SETTINGS_RAPID_TRIGGER_RELEASE_ACCURACY_LABEL: "Release Accuracy:",
    SETTINGS_RAPID_TRIGGER_CONFIGURE_ALL_TITLE: "Configure All Buttons",
    SETTINGS_RAPID_TRIGGER_CONFIGURE_ALL_MESSAGE: "Are you sure you want to configure all buttons at once? This will overwrite the current configuration of all buttons.",
    SETTINGS_BUTTONS_PERFORMANCE_PRESET_CONFIRM_TITLE: "Select Preset",
    SETTINGS_BUTTONS_PERFORMANCE_PRESET_CONFIRM_MESSAGE: "Are you sure you want to select this preset? This will overwrite the current configuration of all buttons.",

    SETTINGS_RAPID_TRIGGER_ENTER_TEST_MODE_BUTTON: "Enter Button Test Mode",
    SETTINGS_RAPID_TRIGGER_EXIT_TEST_MODE_BUTTON: "Exit Button Test Mode",
    SETTINGS_BUTTONS_PERFORMANCE_TEST_TITLE: "BUTTONS PERFORMANCE TEST",
    SETTINGS_BUTTONS_PERFORMANCE_TEST_HELPER_TEXT: "Test the performance and sensitivity of the buttons. Press and release the magnetic axis button on the device, this will record the stroke and the point of the button. Because sampling interval exists, slow press and release can get more accurate data.\n- Key: The index of the button.\n- Press: The stroke of the button when pressed (mm).\n- PoD: The stroke of the button when pressed out of the top deadzone (mm).\n- Release: The stroke of the button when released (mm).\n- RoD: The stroke of the button when released out of the bottom deadzone (mm).\n- After testing, [Exit button test mode] can exit this mode and continue to configure other settings.",
    TEST_MODE_FIELD_KEY_LABEL: "Key:",
    TEST_MODE_FIELD_PRESS_TRIGGER_POINT_LABEL: "Press:",
    TEST_MODE_FIELD_RELEASE_START_POINT_LABEL: "Release:",

    // Select Value Text
    SELECT_VALUE_TEXT_PLACEHOLDER: "Select action",
    
    // Tooltips
    TOOLTIP_SOCD_MODE: "SOCD (Simultaneous Opposing Cardinal Directions) handling mode",
    TOOLTIP_PLATFORM_MODE: "Select the platform for controller input",
    TOOLTIP_LEDS_ENABLE: "Toggle LED lighting effects",
    TOOLTIP_LEDS_EFFECT: "Choose the LED animation style",
    TOOLTIP_LEDS_BRIGHTNESS: "Adjust the brightness of LEDs",
    TOOLTIP_AUTO_SWITCH: "Auto Switch Mode: Automatically switch the button field when the button is binded. \nCan be used with device button clicks to quickly bind multiple buttons.",
    
    // API Response Messages
    API_REBOOT_SUCCESS_MESSAGE: "System is rebooting",
    API_REBOOT_ERROR_MESSAGE: "Failed to reboot system",
    
    // Loading States
    LOADING_MESSAGE: "Loading...",
    
    // Validation Messages
    VALIDATION_PROFILE_NAME_MAX_LENGTH: `Profile name cannot exceed ${PROFILE_NAME_MAX_LENGTH} characters`,
    VALIDATION_PROFILE_NAME_REQUIRED: "Profile name is required",
    VALIDATION_PROFILE_NAME_CANNOT_BE_SAME_AS_CURRENT_PROFILE_NAME: "Profile name cannot be the same as the current profile name",
    VALIDATION_PROFILE_NAME_ALREADY_EXISTS: "Profile name already exists",
    VALIDATION_PROFILE_NAME_SPECIAL_CHARACTERS: "Profile name cannot contain special characters",
    
    // Input Mode Settings
    INPUT_MODE_TITLE: "Input Mode",

    // Keys Settings
    SETTINGS_KEYS_TITLE: "KEYS SETTINGS",
    SETTINGS_KEYS_HELPER_TEXT: `Set the mapping between Hitbox buttons and game controller buttons.\n- Select a configuration field, then press left Hitbox button or the button of the device to bind it.\n- Multiple key mappings can be binded to one controller button.`,
    KEYS_ENABLE_START_BUTTON_LABEL: "Configuring keys enablement",
    KEYS_ENABLE_STOP_BUTTON_LABEL: "Exit configuring",
    KEYS_ENABLE_HELPER_TEXT: "- Click [Configuring keys enablement] to start configuring keys. The available keys will be shown in green, and the disabled keys will be shown in gray.\n- Click the green button to disable the key, and click the gray button to enable the key.\n- Click the [Exit configuring] button to exit the configuring mode. \n- Disabled keys cannot trigger press and release, and cannot light up the LED.",
    KEYS_MAPPING_TITLE_DPAD: "Dpad Buttons",
    KEYS_MAPPING_TITLE_CORE: "Core Buttons",
    KEYS_MAPPING_TITLE_SHOULDER: "Shoulder & Stick Buttons",
    KEYS_MAPPING_TITLE_FUNCTION: "Function Buttons",
    KEYS_MAPPING_TITLE_CUSTOM_COMBINATION: "Custom Button Combinations",


    // Switch Marking Settings
    SETTINGS_SWITCH_MARKING_TITLE: "NEW SWITCH MARKINGS",
    SETTINGS_SWITCH_MARKING_HELPER_TEXT: "Each switch marking can be customized with length and step value.\n- Length: The length of the switch marking.\n- Step: The step value of the switch marking.",

    // LEDs Settings
    SETTINGS_LEDS_TITLE: "LIGHTING SETTINGS",
    SETTINGS_LEDS_HELPER_TEXT: "The lighting effect style, colors, and brightness can be customized here.",

    // Rapid Trigger Settings
    SETTINGS_RAPID_TRIGGER_TITLE: "BUTTONS PERFORMANCE",
    SETTINGS_RAPID_TRIGGER_HELPER_TEXT: "Configure the performance parameters of the buttons, the preset configuration is usually enough, if you need more precise control, you can customize the configuration.\n- Top Deadzone: \"Press\" cannot be triggered within this stroke.\n- Bottom Deadzone: \"Release\" cannot be triggered within this stroke.\n- Press Accuracy: The distance from the top of the button to the press point.\n- Release Accuracy: The distance from the bottom of the button to the release point.",
    SETTINGS_RAPID_TRIGGER_CONFIGURE_ALL: "Configure All Buttons At Once",

    // ADC Button Debounce Algorithm
    SETTINGS_ADC_BUTTON_DEBOUNCE_TITLE: "Button Debounce",
    SETTINGS_ADC_BUTTON_DEBOUNCE_LABEL_NONE: "Fastest",
    SETTINGS_ADC_BUTTON_DEBOUNCE_LABEL_NORMAL: "Balanced",
    SETTINGS_ADC_BUTTON_DEBOUNCE_LABEL_MAX: "Stablility",
    SETTINGS_ADC_BUTTON_DEBOUNCE_LABEL_NONE_DESC: "No debounce, low latency",
    SETTINGS_ADC_BUTTON_DEBOUNCE_LABEL_NORMAL_DESC: "Balanced, increase the latency by 0.25ms",
    SETTINGS_ADC_BUTTON_DEBOUNCE_LABEL_MAX_DESC: "More stable, increase the latency by 0.5ms",

    SETTING_BUTTON_PERFORMANCE_PRESET_TITLE: "Preset Configuration",
    SETTING_BUTTON_PERFORMANCE_PRESET_CUSTOM_LABEL: "Custom",
    SETTING_BUTTON_PERFORMANCE_PRESET_FASTEST_LABEL: "Fastest",
    SETTING_BUTTON_PERFORMANCE_PRESET_BALANCED_LABEL: "Balanced",
    SETTING_BUTTON_PERFORMANCE_PRESET_STABILITY_LABEL: "Stablility",
    SETTING_BUTTON_PERFORMANCE_PRESET_CUSTOM_DESC: "Find the best control feel.",
    SETTING_BUTTON_PERFORMANCE_PRESET_FASTEST_DESC: "Fastest response speed.",
    SETTING_BUTTON_PERFORMANCE_PRESET_BALANCED_DESC: "Suitable for most scenes.",
    SETTING_BUTTON_PERFORMANCE_PRESET_STABILITY_DESC: "Similar to mechanical switch.",

    // Hotkeys Settings
    SETTINGS_HOTKEYS_TITLE: "HOTKEYS SETTINGS",
    SETTINGS_HOTKEYS_HELPER_TEXT: `Configure up to ${DEFAULT_NUM_HOTKEYS_MAX} hotkeys for quick access to various functions.\n- Click on the hotkey field and press the desired key on the hitbox or device to bind the hotkey.\n- Choice the hotkey action from the dropdown list.\n- Locked hotkeys are used for web configuration mode and calibration mode because this function is required. `,
    SETTINGS_HOTKEYS_BUTTON_MONITORING_TITLE: "Device Button Monitoring",

    // Firmware Settings
    SETTINGS_FIRMWARE_TITLE: "FIRMWARE UPDATE",
    SETTINGS_FIRMWARE_CURRENT_VERSION_LABEL: "Current Version: ",
    SETTINGS_FIRMWARE_LATEST_VERSION_LABEL: "Latest Firmware Version: ",
    SETTINGS_FIRMWARE_NO_UPDATE_MESSAGE: "Current firmware is the latest version. No update available.",
    SETTINGS_FIRMWARE_UPDATE_TODO_MESSAGE: "Please click the button to update the firmware, it will take a few minutes, please do not disconnect the device.\nThe firmware update will overwrite the key data, please recalibrate the keys.",
    SETTINGS_FIRMWARE_UPDATE_SUCCESS_TITLE: "Firmware updated successfully",
    SETTINGS_FIRMWARE_UPDATE_SUCCESS_MESSAGE: "Congratulations! The firmware has been updated successfully.\nTo access the latest driver page, the page will be refreshed in {seconds} seconds.",
    SETTINGS_FIRMWARE_UPDATE_FAILED_MESSAGE: "The firmware update failed. Please click the button to try again.",
    SETTINGS_FIRMWARE_UPDATING_MESSAGE: "Updating firmware... Please do not disconnect the device.",
    SETTINGS_FIRMWARE_UPDATE_SUCCESS_BUTTON: "Refresh Page",
    SETTINGS_FIRMWARE_UPDATE_TODO_BUTTON: "Begin Firmware Update",
    SETTINGS_FIRMWARE_UPDATE_TODO_BUTTON_PROGRESS: "Updating firmware...",
    SETTINGS_FIRMWARE_UPDATE_TODO_BUTTON_RETRY: "Retry Update",

    // Profile Select
    PROFILE_SELECT_TITLE: "Profile Select",
    PROFILE_SELECT_CREATE_BUTTON: "Create New Profile",
    PROFILE_SELECT_RENAME_BUTTON: "Rename Profile",
    PROFILE_SELECT_DELETE_BUTTON: "Delete Profile",
    PROFILE_SELECT_MENU_BUTTON: "Profile Menu",
    PROFILE_SELECT_RENAME_DIALOG_TITLE: "Rename Profile",
    PROFILE_SELECT_RENAME_FIELD_LABEL: "Profile Name",
    PROFILE_SELECT_RENAME_FIELD_PLACEHOLDER: "Enter new profile name",
    PROFILE_SELECT_DELETE_CONFIRM_MESSAGE: "Deleting this profile can not be undone or reverted. Are you sure you want to delete this profile?",
    PROFILE_SELECT_VALIDATION_SPECIAL_CHARS: "Profile name cannot contain special characters",
    PROFILE_SELECT_VALIDATION_LENGTH: `Profile name length must be between 1 and ${PROFILE_NAME_MAX_LENGTH} characters, current length is {0}`,
    PROFILE_SELECT_VALIDATION_SAME_NAME: "Profile name cannot be the same as the current profile name",
    PROFILE_SELECT_VALIDATION_EXISTS: "Profile name already exists",

    // Key Mapping Field
    KEY_MAPPING_KEY_PREFIX: "KEY-",

    // Key Mapping Fieldset
    KEY_MAPPING_ERROR_ALREADY_BINDED_TITLE: "Key already binded",
    KEY_MAPPING_ERROR_ALREADY_BINDED_DESC: "Please select another key",
    KEY_MAPPING_ERROR_MAX_KEYS_TITLE: "Max number of key binding per button reached",
    KEY_MAPPING_ERROR_MAX_KEYS_DESC: "Please unbind some keys first",
    KEY_MAPPING_INFO_UNBIND_FROM_OTHER_TITLE: "Key already binded on other button",
    KEY_MAPPING_INFO_UNBIND_FROM_OTHER_DESC: "Unbinded from [ {0} ] button and Rebinded to [ {1} ] button",

    // Keys Settings
    SETTINGS_KEYS_INVERT_X_AXIS: "Invert X Axis",
    SETTINGS_KEYS_INVERT_Y_AXIS: "Invert Y Axis",
    SETTINGS_KEYS_FOURWAY_MODE: "FourWay Mode",
    SETTINGS_KEYS_FOURWAY_MODE_TOOLTIP: "FourWay Mode: Enable the four-way mode of the Dpad, which means the Dpad will be treated as a four-way direction pad.\n(Only available when the input mode is Switch)",
    SETTINGS_KEYS_MAPPING_TITLE: "Key Mapping",

    // Switch Marking Settings
    SETTINGS_SWITCH_MARKING_NAME_LABEL: "Name",
    SETTINGS_SWITCH_MARKING_NAME_PLACEHOLDER: "Enter name",
    SETTINGS_SWITCH_MARKING_LENGTH_LABEL: "Length",
    SETTINGS_SWITCH_MARKING_LENGTH_PLACEHOLDER: "Enter length",
    SETTINGS_SWITCH_MARKING_STEP_LABEL: "Step (mm)",
    SETTINGS_SWITCH_MARKING_STEP_PLACEHOLDER: "Enter step",
    SETTINGS_SWITCH_MARKING_VALIDATION_SPECIAL_CHARS: "Switch marking name cannot contain special characters",
    SETTINGS_SWITCH_MARKING_VALIDATION_LENGTH: `Switch marking name length must be between 1 and ${SWITCH_MARKING_NAME_MAX_LENGTH} characters, current length is {0}`,
    SETTINGS_SWITCH_MARKING_VALIDATION_SAME_NAME: "Switch marking name cannot be the same as the current switch marking name",
    SETTINGS_SWITCH_MARKING_VALIDATION_LENGTH_RANGE: "Switch marking length must be between 1 and 50",
    SETTINGS_SWITCH_MARKING_VALIDATION_STEP_RANGE: "Switch marking step must be between 0.1 and 10",
    SETTINGS_SWITCH_MARKING_DELETE_DIALOG_TITLE: "Delete Switch Marking",
    SETTINGS_SWITCH_MARKING_DELETE_CONFIRM_MESSAGE: "Deleting this switch marking can not be undone or reverted. Are you sure you want to delete this switch marking?",
    SETTINGS_SWITCH_MARKING_COMPLETED_DIALOG_MESSAGE: "Congratulations! Switch marking is completed. You can now close this window and start enjoying the gaming experience.",
    SETTINGS_SWITCH_MARKING_START_DIALOG_MESSAGE: `Press the [Start Marking] button to start marking.`,
    SETTINGS_SWITCH_MARKING_SAMPLING_START_DIALOG_MESSAGE: `Step <step> ready to sampling, please keep the button travel at <distance>mm, and press the [Step] button.`,
    SETTINGS_SWITCH_MARKING_SAMPLING_DIALOG_MESSAGE: `Step <step> is sampling, please keep the button travel at <distance>mm, and wait for the sampling to complete.`,  
    SETTINGS_SWITCH_MARKING_SAVE_DIALOG_MESSAGE: `All steps have been sampled, please press the [Step] button to save the switch marking.`,
    SETTINGS_SWITCH_MARKING_UNSAVED_CHANGES_WARNING_TITLE: "Uncompleted Switch Marking",
    SETTINGS_SWITCH_MARKING_UNSAVED_CHANGES_WARNING_MESSAGE: "You have uncompleted switch marking. If you leave without saving, the switch marking data will be lost.",

    // Settings Layout
    SETTINGS_TAB_GLOBAL: "Global Setting",
    SETTINGS_TAB_KEYS: "Keys Setting",
    SETTINGS_TAB_LEDS: "Lighting Setting",
    SETTINGS_TAB_BUTTONS_PERFORMANCE: "Buttons Performance",
    SETTINGS_TAB_SWITCH_MARKING: "Switch Marking",
    SETTINGS_TAB_FIRMWARE: "Firmware",
    SETTINGS_TAB_VIEW_LOGS: "View Logs",

    // Keys Settings
    SETTINGS_KEYS_INPUT_MODE_TITLE: "Input Mode Choice",
    SETTINGS_KEYS_SOCD_MODE_TITLE: "SOCD Mode Choice",
    SETTINGS_KEYS_PLATFORM_MODE_TOOLTIP: "Platform Mode: Select the platform for controller input",
    SETTINGS_KEYS_SOCD_MODE_TOOLTIP: "SOCD Mode: Select the SOCD (Simultaneous Opposing Cardinal Directions) handling mode",

    // LEDs Settings
    SETTINGS_LEDS_EFFECT_STYLE_CHOICE: "LED Effect Style",
    SETTINGS_LEDS_STATIC_LABEL: "Static",
    SETTINGS_LEDS_BREATHING_LABEL: "Breathing",
    SETTINGS_LEDS_STAR_LABEL: "Star",
    SETTINGS_LEDS_FLOWING_LABEL: "Flowing",
    SETTINGS_LEDS_RIPPLE_LABEL: "Ripple",
    SETTINGS_LEDS_TRANSFORM_LABEL: "Transform",
    SETTINGS_LEDS_QUAKE_LABEL: "Quake",
    SETTINGS_LEDS_METEOR_LABEL: "Meteor",
    SETTINGS_LEDS_COLORS_LABEL: "LED Colors",
    SETTINGS_LEDS_FRONT_COLOR: "Front Color",
    SETTINGS_LEDS_BACK_COLOR1: "Back Color 1",
    SETTINGS_LEDS_BACK_COLOR2: "Back Color 2",

    SETTINGS_LEDS_AMBIENT_LIGHT_TITLE: "Ambient Lighting Settings",
    SETTINGS_LEDS_AMBIENT_LIGHT_COLORS_LABEL: "Ambient Lighting Colors",
    SETTINGS_LEDS_AMBIENT_LIGHT_COLOR1: "Ambient Lighting Color 1",
    SETTINGS_LEDS_AMBIENT_LIGHT_COLOR2: "Ambient Lighting Color 2",

    // Unsaved Changes Warning
    UNSAVED_CHANGES_WARNING_TITLE: "Are you sure?",
    UNSAVED_CHANGES_WARNING_MESSAGE: "You have unsaved changes. If you leave without saving, your changes will be lost.",
    CALIBRATION_MODE_WARNING_TITLE: "Calibration in Progress",
    CALIBRATION_MODE_WARNING_MESSAGE: "Calibration is in progress. You must stop calibration before leaving this page.",
    CALIBRATION_TIP_MESSAGE: "Click [Start Calibration] to start calibration. If there are buttons to be calibrated, they will be shown in dark-blue on the device. Press and hold the dark-blue button until the LED light turns green, then the calibration is complete. \n- Note that after pressing the button to the bottom, you need to keep the button pressed and stable, otherwise the calibration will fail. \n- When all buttons are calibrated, click the [Stop Calibration] button to end the calibration mode. \n- If you want to re-calibrate a button, please click the [Reset Calibration] button first. \n- [Reset Calibration] button will clear all the calibration data, please use it with caution.",

    // Hotkeys Actions
    HOTKEY_ACTION_NONE: "None",
    HOTKEY_ACTION_WEB_CONFIG: "Web Config Mode",
    HOTKEY_ACTION_LEDS_ENABLE: "LEDs Enable/Disable",
    HOTKEY_ACTION_LEDS_EFFECT_NEXT: "LEDs Effect Next",
    HOTKEY_ACTION_LEDS_EFFECT_PREV: "LEDs Effect Previous",
    HOTKEY_ACTION_LEDS_BRIGHTNESS_UP: "LEDs Brightness Up",
    HOTKEY_ACTION_LEDS_BRIGHTNESS_DOWN: "LEDs Brightness Down",
    HOTKEY_ACTION_AMBIENT_LIGHT_ENABLE: "Ambient Lighting Enable/Disable",
    HOTKEY_ACTION_AMBIENT_LIGHT_EFFECT_NEXT: "Ambient Lighting Effect Next",
    HOTKEY_ACTION_AMBIENT_LIGHT_EFFECT_PREV: "Ambient Lighting Effect Previous",
    HOTKEY_ACTION_AMBIENT_LIGHT_BRIGHTNESS_UP: "Ambient Lighting Brightness Up",
    HOTKEY_ACTION_AMBIENT_LIGHT_BRIGHTNESS_DOWN: "Ambient Lighting Brightness Down",
    HOTKEY_ACTION_CALIBRATION_MODE: "Calibration Mode",
    HOTKEY_ACTION_XINPUT_MODE: "XInput Mode",
    HOTKEY_ACTION_PS4_MODE: "PlayStation 4 Mode",
    HOTKEY_ACTION_PS5_MODE: "PlayStation 5 Mode",
    HOTKEY_ACTION_NSWITCH_MODE: "Nintendo Switch Mode",
    HOTKEY_ACTION_XBOX_MODE: "Xbox Mode",
    HOTKEY_ACTION_SYSTEM_REBOOT: "Device Reboot",

    HOTKEY_TRIGGER_HOLD: "Hold",
    HOTKEY_TRIGGER_CLICK: "Click",

    // Reconnect Modal
    RECONNECT_MODAL_MESSAGE: "The connection to the device has been lost, possible reasons: \n1. The USB connection has been lost. \n2. The device is not in webconfig mode. \n3. Other web pages are connected to the device at the same time. \nPlease check and click the button to reconnect.",
    RECONNECT_MODAL_BUTTON: "Reconnect Device",
    RECONNECT_MODAL_TITLE: "Device Disconnected",
    RECONNECT_FAILED_TITLE: "Failed to Reconnect",
    RECONNECT_FAILED_MESSAGE: "Failed to reconnect to the device. Please check the USB connection and make sure the device is in webconfig mode, then try again.",

    SOCD_NEUTRAL: "Neutral",
    SOCD_NEUTRAL_DESC: "When opposite directional keys are pressed simultaneously, a neutral signal will be output, meaning no directional input.",
    SOCD_UP_PRIORITY: "Up Priority",
    SOCD_UP_PRIORITY_DESC: "In the horizontal direction, opposite directional inputs are handled in NEUTRAL mode, meaning a neutral signal is output; when both up and down are pressed simultaneously in the vertical direction, the \"up\" directional signal is prioritized for output.",
    SOCD_SEC_INPUT_PRIORITY: "Sec Input Priority",
    SOCD_SEC_INPUT_PRIORITY_DESC: "The later input directional command is prioritized for sending.",
    SOCD_FIRST_INPUT_PRIORITY: "First Input Priority",
    SOCD_FIRST_INPUT_PRIORITY_DESC: "The First input directional command is prioritized for sending.",
    SOCD_BYPASS: "Bypass",
    SOCD_BYPASS_DESC: "All directional commands are sent directly without any processing.",

    // Combination Field
    COMBINATION_FIELD_EDIT_BUTTON: "Edit",
    COMBINATION_SELECT_PLACEHOLDER: "Select The Game Controller Button",
    COMBINATION_DIALOG_TITLE: "Edit Button Combination",
    COMBINATION_DIALOG_MESSAGE: `Please select up to ${COMBINATION_KEY_MAX_LENGTH} buttons to form a combination key`,
    COMBINATION_DIALOG_BUTTON_CONFIRM: "Confirm",

} as const;

export const UI_TEXT_ZH = {
    // 通用按钮文案
    BUTTON_RESET: "重置",
    BUTTON_SAVE: "保存",
    BUTTON_REBOOT_WITH_SAVING: "结束配置并开始游戏",
    BUTTON_CANCEL: "取消",
    BUTTON_SUBMIT: "确定",
    BUTTON_CONFIRM: "确认",
    BUTTON_PREVIEW_IN_TABLE_VIEW: "在表格视图中预览",
    BUTTON_EXPORT_CONFIG: "导出配置",
    EXPORT_CONFIG_FAILED_TITLE: "导出失败",
    EXPORT_CONFIG_FAILED_MESSAGE: "导出配置失败",
    BUTTON_IMPORT_CONFIG: "导入配置",
    IMPORT_CONFIG_FAILED_TITLE: "导入失败",
    IMPORT_CONFIG_FAILED_MESSAGE: "导入配置失败",
    IMPORT_CONFIG_SUCCESS_TITLE: "导入成功",
    IMPORT_CONFIG_SUCCESS_MESSAGE: "配置导入成功。点击确认刷新页面。",
    // 对话框标题
    DIALOG_REBOOT_CONFIRM_TITLE: "确认重启?",
    DIALOG_REBOOT_SUCCESS_TITLE: "重启成功",
    DIALOG_CREATE_PROFILE_TITLE: "创建新配置",
    DIALOG_RENAME_PROFILE_TITLE: "重命名配置",
    
    // 对话框消息
    DIALOG_REBOOT_CONFIRM_MESSAGE: "保存并重启系统将会保存当前配置并结束当前会话。\n是否确认继续？",
    DIALOG_REBOOT_SUCCESS_MESSAGE: "系统重启成功。\n您现在可以关闭此窗口并开始享受游戏体验。",
    DIALOG_CREATE_PROFILE_CONFIRM_MESSAGE: "创建新配置将会结束当前会话。是否确认继续？",
    DIALOG_RENAME_PROFILE_CONFIRM_MESSAGE: "重命名当前配置将会保存当前配置并结束当前会话。是否确认继续？",
    
    // 错误消息
    ERROR_KEY_ALREADY_BINDED_TITLE: "按键已被绑定",
    ERROR_KEY_ALREADY_BINDED_DESC: "请选择其他按键，或先解绑当前按键",
    
    // 配置相关
    PROFILE_CREATE_DIALOG_TITLE: "创建新配置",
    PROFILE_DELETE_DIALOG_TITLE: "删除配置",
    PROFILE_DELETE_CONFIRM_MESSAGE: "删除此配置后将无法恢复。是否确认删除？",
    PROFILE_NAME_LABEL: "配置名称",
    PROFILE_NAME_PLACEHOLDER: "请输入配置名称",
    
    // 校准
    AUTO_CALIBRATION_TITLE: "自动磁轴校准",
    MANUAL_CALIBRATION_TITLE: "手动磁轴校准",
    CALIBRATION_HELPER_TEXT: "磁轴校准是找到控制器最佳按键行程的过程。它是找到控制器最佳按键行程的过程。",
    CALIBRATION_START_BUTTON: "开始按键校准",
    CALIBRATION_STOP_BUTTON: "停止按键校准",
    CALIBRATION_CLEAR_DATA_BUTTON: "重置校准数据",
    CALIBRATION_CLEAR_DATA_DIALOG_TITLE: "清除校准数据",
    CALIBRATION_CLEAR_DATA_DIALOG_MESSAGE: "清除校准数据后将无法恢复，需要重新校准，是否确认清除？",
    CALIBRATION_COMPLETION_DIALOG_TITLE: "校准完成",
    CALIBRATION_COMPLETION_DIALOG_MESSAGE: "所有按键已校准完成，是否要结束校准模式？如果需要重新校准，请先清除校准数据",
    CALIBRATION_CHECK_COMPLETED_DIALOG_TITLE: "存在未校准的按键",
    CALIBRATION_CHECK_COMPLETED_DIALOG_MESSAGE: "按键校准未完成，会导致按键无法使用，是否立即开始校准？",

    // 设置标签
    SETTINGS_SOCD_LABEL: "SOCD模式",
    SETTINGS_PLATFORM_LABEL: "平台",
    SETTINGS_LEDS_ENABLE_LABEL: "启用LED",
    SETTINGS_LEDS_EFFECT_LABEL: "LED效果样式",
    SETTINGS_LEDS_BRIGHTNESS_LABEL: "LED亮度",
    SETTINGS_LEDS_ANIMATION_SPEED_LABEL: "LED灯效动画速度",
    SETTINGS_LEDS_COLOR_FRONT_LABEL: "LED前置颜色",
    SETTINGS_LEDS_COLOR_BACK1_LABEL: "LED背景颜色1",
    SETTINGS_LEDS_COLOR_BACK2_LABEL: "LED背景颜色2",

    SETTINGS_AMBIENT_LIGHT_ENABLE_LABEL: "启用氛围灯",
    SETTINGS_AMBIENT_LIGHT_SYNC_WITH_LEDS_LABEL: "效果与按键的LED同步",
    SETTINGS_AMBIENT_LIGHT_TRIGGER_BY_BUTTON_LABEL: "由按键触发",
    SETTINGS_AMBIENT_LIGHT_EFFECT_LABEL: "氛围灯效果",
    SETTINGS_AMBIENT_LIGHT_BRIGHTNESS_LABEL: "氛围灯亮度",
    SETTINGS_AMBIENT_LIGHT_ANIMATION_SPEED_LABEL: "氛围灯动画速度",
    SETTINGS_AMBIENT_LIGHT_COLOR1_LABEL: "氛围灯颜色1",
    SETTINGS_AMBIENT_LIGHT_COLOR2_LABEL: "氛围灯颜色2",

    SETTINGS_KEY_MAPPING_AUTO_SWITCH_LABEL: "自动切换模式",
    SETTINGS_KEY_MAPPING_MANUAL_SWITCH_LABEL: "手动切换模式",
    SETTINGS_KEY_MAPPING_CLEAR_MAPPING_BUTTON: "清空映射",
    SETTINGS_RAPID_TRIGGER_ONFIGURING_BUTTON: "正在配置按键: ",
    SETTINGS_RAPID_TRIGGER_SELECT_A_BUTTON_TO_CONFIGURE: "请选择要配置的按键",
    SETTINGS_RAPID_TRIGGER_TOP_DEADZONE_LABEL: "顶部死区：",
    SETTINGS_RAPID_TRIGGER_BOTTOM_DEADZONE_LABEL: "底部死区：",
    SETTINGS_RAPID_TRIGGER_PRESS_ACCURACY_LABEL: "按下精度：",
    SETTINGS_RAPID_TRIGGER_RELEASE_ACCURACY_LABEL: "释放精度：",
    SETTINGS_RAPID_TRIGGER_CONFIGURE_ALL_TITLE: "同时配置所有按键",
    SETTINGS_RAPID_TRIGGER_CONFIGURE_ALL_MESSAGE: "确定要同时配置所有按键吗？此操作将覆盖所有按键的当前配置。",
    SETTINGS_BUTTONS_PERFORMANCE_PRESET_CONFIRM_TITLE: "选择预设",
    SETTINGS_BUTTONS_PERFORMANCE_PRESET_CONFIRM_MESSAGE: "确定要选择这个预设吗？此操作将覆盖所有按键的当前配置。",

    SETTINGS_RAPID_TRIGGER_ENTER_TEST_MODE_BUTTON: "进入按键测试模式",
    SETTINGS_RAPID_TRIGGER_EXIT_TEST_MODE_BUTTON: "退出按键测试模式",
    SETTINGS_BUTTONS_PERFORMANCE_TEST_TITLE: "按键性能测试",
    SETTINGS_BUTTONS_PERFORMANCE_TEST_HELPER_TEXT: "可以在这里测试按键性能和灵敏度。按下并释放设备上的磁轴按键，这里会记录每个按键的最近一次按下和释放行程。由于采样间隔因素的存在，缓慢按压和释放，能得到更符合设置的数据。\n- key：按键索引\n- Press：按键触发按压的行程(mm)\n- PoD：按压行程在顶部死区之外的行程(mm)\n- Release：按键释放的行程(mm)\n- RoD：释放行程在底部死区之外的行程(mm)\n- 测试完成后，[退出按键测试模式]可以退出此模式继续其他的配置。",
    TEST_MODE_FIELD_KEY_LABEL: "按键:",
    TEST_MODE_FIELD_PRESS_TRIGGER_POINT_LABEL: "按下:",
    TEST_MODE_FIELD_RELEASE_START_POINT_LABEL: "释放:",
    
    // 选择值文本
    SELECT_VALUE_TEXT_PLACEHOLDER: "选择动作",
    
    // 工具提示
    TOOLTIP_SOCD_MODE: "SOCD(同时按下相对方向键)处理模式",
    TOOLTIP_PLATFORM_MODE: "选择控制器输入平台",
    TOOLTIP_LEDS_ENABLE: "开启/关闭LED灯光效果",
    TOOLTIP_LEDS_EFFECT: "选择LED动画样式",
    TOOLTIP_LEDS_BRIGHTNESS: "调整LED亮度",
    TOOLTIP_AUTO_SWITCH: "自动切换模式：当前按键绑定后，会自动切换至下一个按键。可配合设备按键点击，快速绑定多个按键。",
    
    // API响应消息
    API_REBOOT_SUCCESS_MESSAGE: "系统正在重启",
    API_REBOOT_ERROR_MESSAGE: "系统重启失败",
    
    // 加载状态
    LOADING_MESSAGE: "加载中...",
    
    // 验证消息
    VALIDATION_PROFILE_NAME_MAX_LENGTH: `配置名称不能超过${PROFILE_NAME_MAX_LENGTH}个字符`,
    VALIDATION_PROFILE_NAME_REQUIRED: "配置名称不能为空",
    VALIDATION_PROFILE_NAME_CANNOT_BE_SAME_AS_CURRENT_PROFILE_NAME: "新配置名称不能与当前配置名称相同",
    VALIDATION_PROFILE_NAME_ALREADY_EXISTS: "配置名称已存在",
    VALIDATION_PROFILE_NAME_SPECIAL_CHARACTERS: "配置名称不能包含特殊字符",
    
    // 输入模式
    INPUT_MODE_TITLE: "输入模式",

    // 按键设置
    SETTINGS_KEYS_TITLE: "按键设置",
    SETTINGS_KEYS_HELPER_TEXT: `设置格斗板按键和游戏控制器按键的映射关系。\n- 选中下面的游戏控制器按键配置框，然后点击左侧格斗板图示按键或者按压实体设备上的按键即可绑定。 \n- 每个控制器按键可以绑定多个格斗板按键。`,
    KEYS_ENABLE_START_BUTTON_LABEL: "开始配置按键启用/禁用",
    KEYS_ENABLE_STOP_BUTTON_LABEL: "结束配置按键启用/禁用",
    KEYS_ENABLE_HELPER_TEXT: "- 点击[开始配置按键启用/禁用]后，下面的格斗板图示上，可用按键会以绿色显示，禁用按键会以灰色显示。\n- 点击绿色按键可以禁用此按键，点击灰色按键可以启用此按键。\n- 点击[结束配置按键启用/禁用]后，配置模式结束。\n- 被禁用的按键无法触发按下和回弹，无法点亮LED。",
    KEYS_MAPPING_TITLE_DPAD: "十字方向键",
    KEYS_MAPPING_TITLE_CORE: "核心按键",
    KEYS_MAPPING_TITLE_SHOULDER: "肩键和摇杆键",
    KEYS_MAPPING_TITLE_FUNCTION: "功能按键",
    KEYS_MAPPING_TITLE_CUSTOM_COMBINATION: "自定义组合键",

    // 磁轴标记设置
    SETTINGS_SWITCH_MARKING_TITLE: "新磁轴标记",
    SETTINGS_SWITCH_MARKING_HELPER_TEXT: "每个磁轴标记可以自定义长度和步进值。用于形成磁轴行程和模拟电压值的映射关系。\n- 长度：磁轴标记的长度\n- 步进值：磁轴标记的步进值",
    
    // LED设置
    SETTINGS_LEDS_TITLE: "灯光设置",
    SETTINGS_LEDS_HELPER_TEXT: "可以在这里自定义LED效果样式、颜色和亮度。",
    
    // 快速触发设置
    SETTINGS_RAPID_TRIGGER_TITLE: "按键性能",
    SETTINGS_RAPID_TRIGGER_HELPER_TEXT: "配置按键的性能参数，大多数情况下预设配置已经足够，如果需要更精细的控制，可以自定义配置。\n- 顶部死区：按键顶部的保护行程，在此行程中“按下”不会触发，一般用于防止误触。\n- 底部死区：按键底部的保护行程，在此行程中“释放”不会触发，一般用于防止意外的“释放”。\n- 按下精度：在顶部死区外，按下超过此行程后即可触发“按下”，用于控制按键的触发灵敏度。\n- 释放精度：在底部死区外，弹起超过此行程后即可触发“释放”，用于控制按键的触发灵敏度。\n- 同时配置所有按键：同时配置所有按键的性能参数，用于快速配置所有按键，会覆盖当前所有按键的配置。\n- [在表格试图中预览]可以看到所有按键的性能参数配置。",
    SETTINGS_RAPID_TRIGGER_CONFIGURE_ALL: "同时配置所有按键",

    // 按钮防抖设置
    SETTINGS_ADC_BUTTON_DEBOUNCE_TITLE: "按键防抖",
    SETTINGS_ADC_BUTTON_DEBOUNCE_LABEL_NONE: "急速",
    SETTINGS_ADC_BUTTON_DEBOUNCE_LABEL_NORMAL: "平衡",
    SETTINGS_ADC_BUTTON_DEBOUNCE_LABEL_MAX: "稳定",
    SETTINGS_ADC_BUTTON_DEBOUNCE_LABEL_NONE_DESC: "无防抖，延迟最低",
    SETTINGS_ADC_BUTTON_DEBOUNCE_LABEL_NORMAL_DESC: "平衡，增加0.25ms延迟",
    SETTINGS_ADC_BUTTON_DEBOUNCE_LABEL_MAX_DESC: "稳定，增加0.5ms延迟",

    // 按钮性能设置
    SETTING_BUTTON_PERFORMANCE_PRESET_TITLE: "按键性能预设",
    SETTING_BUTTON_PERFORMANCE_PRESET_CUSTOM_LABEL: "自定义",
    SETTING_BUTTON_PERFORMANCE_PRESET_FASTEST_LABEL: "急速",
    SETTING_BUTTON_PERFORMANCE_PRESET_BALANCED_LABEL: "平衡",
    SETTING_BUTTON_PERFORMANCE_PRESET_STABILITY_LABEL: "稳定",
    SETTING_BUTTON_PERFORMANCE_PRESET_CUSTOM_DESC: "找到自己的最佳手感。",
    SETTING_BUTTON_PERFORMANCE_PRESET_FASTEST_DESC: "适合对按键延迟要求极高的场景。",
    SETTING_BUTTON_PERFORMANCE_PRESET_BALANCED_DESC: "适合大多数场景。",
    SETTING_BUTTON_PERFORMANCE_PRESET_STABILITY_DESC: "类似机械轴的体验。",
    
    // 热键设置
    SETTINGS_HOTKEYS_TITLE: "快捷键设置",
    SETTINGS_HOTKEYS_HELPER_TEXT: `最多可以配置${DEFAULT_NUM_HOTKEYS_MAX}个快捷键来快速访问各种功能。\n- 点击快捷键区域并在实体设备上或者左侧格斗板图示上按下想要绑定的按键\n- 从下拉列表中选择快捷键动作\n- 锁定的快捷键用于网页配置模式和校准模式，因为这些功能是必需的。`,
    SETTINGS_HOTKEYS_BUTTON_MONITORING_TITLE: "设备按键监控",

    // 固件更新
    SETTINGS_FIRMWARE_TITLE: "设备固件更新",
    SETTINGS_FIRMWARE_CURRENT_VERSION_LABEL: "当前固件版本: ",
    SETTINGS_FIRMWARE_LATEST_VERSION_LABEL: "最新固件版本: ",
    SETTINGS_FIRMWARE_NO_UPDATE_MESSAGE: "当前固件版本为最新版本，没有可用的更新",
    SETTINGS_FIRMWARE_UPDATE_TODO_MESSAGE: "请点击按钮更新固件，过程需要几分钟时间，不要断开设备连接。\n固件更新刷写了按键数据，请重新校准按键。",
    SETTINGS_FIRMWARE_UPDATE_SUCCESS_TITLE: "固件更新成功",
    SETTINGS_FIRMWARE_UPDATE_SUCCESS_MESSAGE: "太棒了！固件更新成功。\n为了访问最新的驱动页面，{seconds}秒后，页面将自动刷新。",
    SETTINGS_FIRMWARE_UPDATE_FAILED_MESSAGE: "固件更新失败，请点击按钮重试",
    SETTINGS_FIRMWARE_UPDATING_MESSAGE: "固件更新中...请不要断开设备连接",
    SETTINGS_FIRMWARE_UPDATE_SUCCESS_BUTTON: "刷新页面",
    SETTINGS_FIRMWARE_UPDATE_TODO_BUTTON: "开始固件更新",
    SETTINGS_FIRMWARE_UPDATE_TODO_BUTTON_PROGRESS: "固件更新中...",
    SETTINGS_FIRMWARE_UPDATE_TODO_BUTTON_RETRY: "重试更新",

    // 配置选择
    PROFILE_SELECT_TITLE: "配置选择",
    PROFILE_SELECT_CREATE_BUTTON: "创建新配置",
    PROFILE_SELECT_RENAME_BUTTON: "重命名配置",
    PROFILE_SELECT_DELETE_BUTTON: "删除配置",
    PROFILE_SELECT_MENU_BUTTON: "配置菜单",
    PROFILE_SELECT_RENAME_DIALOG_TITLE: "重命名配置",
    PROFILE_SELECT_RENAME_FIELD_LABEL: "配置名称",
    PROFILE_SELECT_RENAME_FIELD_PLACEHOLDER: "请输入新的配置名称",
    PROFILE_SELECT_DELETE_CONFIRM_MESSAGE: "删除此配置后将无法恢复。是否确认删除？",
    PROFILE_SELECT_VALIDATION_SPECIAL_CHARS: "配置名称不能包含特殊字符",
    PROFILE_SELECT_VALIDATION_LENGTH: `配置名称长度必须在1到${PROFILE_NAME_MAX_LENGTH}个字符之间，当前长度为{0}`,
    PROFILE_SELECT_VALIDATION_SAME_NAME: "配置名称不能与当前配置名称相同",
    PROFILE_SELECT_VALIDATION_EXISTS: "配置名称已存在",

    // Key Mapping Field
    KEY_MAPPING_KEY_PREFIX: "按键-",

    // Key Mapping Fieldset
    KEY_MAPPING_ERROR_ALREADY_BINDED_TITLE: "按键已被绑定",
    KEY_MAPPING_ERROR_ALREADY_BINDED_DESC: "请选择其他按键",
    KEY_MAPPING_ERROR_MAX_KEYS_TITLE: "已达到每个按键的最大绑定数",
    KEY_MAPPING_ERROR_MAX_KEYS_DESC: "请先解绑一些按键",
    KEY_MAPPING_INFO_UNBIND_FROM_OTHER_TITLE: "按键已在其他按钮上绑定",
    KEY_MAPPING_INFO_UNBIND_FROM_OTHER_DESC: "已从 [ {0} ] 按钮解绑并重新绑定到 [ {1} ] 按钮",

    // Keys Settings
    SETTINGS_KEYS_INVERT_X_AXIS: "反转X轴",
    SETTINGS_KEYS_INVERT_Y_AXIS: "反转Y轴",
    SETTINGS_KEYS_FOURWAY_MODE: "四方向模式",
    SETTINGS_KEYS_FOURWAY_MODE_TOOLTIP: "四方向模式：启用十字键的四方向模式，这意味着十字键将被视为四方向键盘。\n(仅在输入模式为Switch时可用)",
    SETTINGS_KEYS_MAPPING_TITLE: "按键映射",

    // Switch Marking Settings
    SETTINGS_SWITCH_MARKING_NAME_LABEL: "名称",
    SETTINGS_SWITCH_MARKING_NAME_PLACEHOLDER: "请输入名称",
    SETTINGS_SWITCH_MARKING_LENGTH_LABEL: "长度",
    SETTINGS_SWITCH_MARKING_LENGTH_PLACEHOLDER: "请输入长度",
    SETTINGS_SWITCH_MARKING_STEP_LABEL: "步进值 (mm)",
    SETTINGS_SWITCH_MARKING_STEP_PLACEHOLDER: "请输入步进值",
    SETTINGS_SWITCH_MARKING_VALIDATION_SPECIAL_CHARS: "磁轴标记名称不能包含特殊字符",
    SETTINGS_SWITCH_MARKING_VALIDATION_LENGTH: `磁轴标记名称长度必须在1到${SWITCH_MARKING_NAME_MAX_LENGTH}个字符之间，当前长度为{0}`,
    SETTINGS_SWITCH_MARKING_VALIDATION_SAME_NAME: "磁轴标记名称不能与当前磁轴标记名称相同",
    SETTINGS_SWITCH_MARKING_VALIDATION_LENGTH_RANGE: "磁轴标记长度必须在1到50之间",
    SETTINGS_SWITCH_MARKING_VALIDATION_STEP_RANGE: "磁轴标记步进值必须在0.1到10之间",
    SETTINGS_SWITCH_MARKING_DELETE_DIALOG_TITLE: "删除磁轴标记",
    SETTINGS_SWITCH_MARKING_DELETE_CONFIRM_MESSAGE: "删除此磁轴标记后将无法恢复。是否确认删除？",
    SETTINGS_SWITCH_MARKING_COMPLETED_DIALOG_TITLE: "磁轴标记完成",
    SETTINGS_SWITCH_MARKING_COMPLETED_DIALOG_MESSAGE: "恭喜！磁轴标记已完成。您现在可以关闭此窗口并开始享受游戏体验。",
    SETTINGS_SWITCH_MARKING_START_DIALOG_MESSAGE: `按下[Start Marking]按钮开始标记。`,
    SETTINGS_SWITCH_MARKING_SAMPLING_START_DIALOG_MESSAGE: `准备开始采样第<step>步，请使按钮行程保持在<distance>mm，并按下[step]按钮。`,
    SETTINGS_SWITCH_MARKING_SAMPLING_DIALOG_MESSAGE: `第<step>步正在采样中，请使按钮行程保持在<distance>mm，等待采样完成。`,
    SETTINGS_SWITCH_MARKING_SAVE_DIALOG_MESSAGE: "所有步进已采样完成，请按下[step]按钮保存磁轴标记。",
    SETTINGS_SWITCH_MARKING_UNSAVED_CHANGES_WARNING_TITLE: "未完成磁轴标记",
    SETTINGS_SWITCH_MARKING_UNSAVED_CHANGES_WARNING_MESSAGE: "您有未完成的磁轴标记。如果离开而不保存，磁轴标记数据将会丢失。",

    // Settings Layout
    SETTINGS_TAB_GLOBAL: "全局设置",
    SETTINGS_TAB_KEYS: "按键设置",
    SETTINGS_TAB_LEDS: "灯光设置",
    SETTINGS_TAB_BUTTONS_PERFORMANCE: "按键性能",
    SETTINGS_TAB_SWITCH_MARKING: "磁轴标记",
    SETTINGS_TAB_FIRMWARE: "固件升级",
    SETTINGS_TAB_VIEW_LOGS: "查看日志",
    

    // Keys Settings
    SETTINGS_KEYS_INPUT_MODE_TITLE: "输入模式选择",
    SETTINGS_KEYS_SOCD_MODE_TITLE: "SOCD模式选择",
    SETTINGS_KEYS_PLATFORM_MODE_TOOLTIP: "平台模式：选择控制器输入平台",
    SETTINGS_KEYS_SOCD_MODE_TOOLTIP: "SOCD模式：选择SOCD(同时按下相对方向键)处理模式",

    // LEDs Settings
    SETTINGS_LEDS_EFFECT_STYLE_CHOICE: "LED效果样式",
    SETTINGS_LEDS_STATIC_LABEL: "静态",
    SETTINGS_LEDS_BREATHING_LABEL: "呼吸",
    SETTINGS_LEDS_STAR_LABEL: "星光",
    SETTINGS_LEDS_FLOWING_LABEL: "流光",
    SETTINGS_LEDS_RIPPLE_LABEL: "涟漪",
    SETTINGS_LEDS_TRANSFORM_LABEL: "质变",
    SETTINGS_LEDS_QUAKE_LABEL: "震颤",
    SETTINGS_LEDS_METEOR_LABEL: "流星",
    SETTINGS_LEDS_COLORS_LABEL: "LED颜色",
    SETTINGS_LEDS_FRONT_COLOR: "前置颜色",
    SETTINGS_LEDS_BACK_COLOR1: "背景颜色1",
    SETTINGS_LEDS_BACK_COLOR2: "背景颜色2",

    SETTINGS_LEDS_AMBIENT_LIGHT_TITLE: "氛围灯设置",
    SETTINGS_LEDS_AMBIENT_LIGHT_COLORS_LABEL: "氛围灯颜色",
    SETTINGS_LEDS_AMBIENT_LIGHT_COLOR1: "氛围灯颜色1",
    SETTINGS_LEDS_AMBIENT_LIGHT_COLOR2: "氛围灯颜色2",

    // Unsaved Changes Warning
    UNSAVED_CHANGES_WARNING_TITLE: "确认离开?",
    UNSAVED_CHANGES_WARNING_MESSAGE: "您有未保存的更改。如果离开而不保存，您的更改将会丢失。",
    CALIBRATION_MODE_WARNING_TITLE: "校准进行中",
    CALIBRATION_MODE_WARNING_MESSAGE: "校准正在进行中。在离开此页面之前，您必须停止校准。",
    CALIBRATION_TIP_MESSAGE: "点击[开始按键校准]后，如果有待校准的按键，设备上会以深蓝色展示，把深蓝色的按钮按压到底并保持住，直到按钮上的LED灯变成绿色，则校准完成。\n- 注意按压到底后，需要保持稳定按压状态，不要抖动或者移动手指，否则会校准失败。\n- 所有按键校准完成后，点击[结束校准]按钮结束校准模式。\n- 已经校准过的按键，如果希望重新校准，请先点击[重置校准数据]按键。\n- [重置校准数据]按键会清除所有按键的校准数据，请谨慎使用。",

    // Hotkeys Actions
    HOTKEY_ACTION_NONE: "无",
    HOTKEY_ACTION_WEB_CONFIG: "网页配置模式",
    HOTKEY_ACTION_LEDS_ENABLE: "开启/关闭LED",
    HOTKEY_ACTION_LEDS_EFFECT_NEXT: "下一个LED效果",
    HOTKEY_ACTION_LEDS_EFFECT_PREV: "上一个LED效果",
    HOTKEY_ACTION_LEDS_BRIGHTNESS_UP: "LED亮度增加",
    HOTKEY_ACTION_LEDS_BRIGHTNESS_DOWN: "LED亮度减少",
    HOTKEY_ACTION_AMBIENT_LIGHT_ENABLE: "开启/关闭氛围灯",
    HOTKEY_ACTION_AMBIENT_LIGHT_EFFECT_NEXT: "下一个氛围灯效果",
    HOTKEY_ACTION_AMBIENT_LIGHT_EFFECT_PREV: "上一个氛围灯效果",
    HOTKEY_ACTION_AMBIENT_LIGHT_BRIGHTNESS_UP: "氛围灯亮度增加",
    HOTKEY_ACTION_AMBIENT_LIGHT_BRIGHTNESS_DOWN: "氛围灯亮度减少",
    HOTKEY_ACTION_CALIBRATION_MODE: "校准模式",
    HOTKEY_ACTION_XINPUT_MODE: "XInput模式",
    HOTKEY_ACTION_PS4_MODE: "PlayStation 4模式",
    HOTKEY_ACTION_PS5_MODE: "PlayStation 5模式",
    HOTKEY_ACTION_NSWITCH_MODE: "Nintendo Switch模式",
    HOTKEY_ACTION_XBOX_MODE: "Xbox模式",
    HOTKEY_ACTION_SYSTEM_REBOOT: "重启设备",

    HOTKEY_TRIGGER_HOLD: "长按",
    HOTKEY_TRIGGER_CLICK: "短按",

    // Reconnect Modal
    RECONNECT_MODAL_MESSAGE: "与设备的连接已断开，可能的原因：\n1. 设备USB已经断开。\n2. 设备没有打开网页配置模式。\n3. 同时打开了其他网页连接了设备。\n请检查无误后点击按钮重新连接。",
    RECONNECT_MODAL_BUTTON: "重新连接设备",
    RECONNECT_MODAL_TITLE: "设备断开连接",
    RECONNECT_FAILED_TITLE: "重连设备失败",
    RECONNECT_FAILED_MESSAGE: "无法重新连接到设备，请检查USB连接，以及保证设备在网页配置模式并重试。",

    SOCD_NEUTRAL: "中立",
    SOCD_NEUTRAL_DESC: "当同时按下相对方向键时，输出中立信号，表示没有方向输入。", 
    SOCD_UP_PRIORITY: "上优先",
    SOCD_UP_PRIORITY_DESC: "在水平方向上，相反方向的输入类似<中立>模式下处理一样，输出中立信号；当同时按下上和下时，优先输出上方向信号。",
    SOCD_SEC_INPUT_PRIORITY: "后输入优先",
    SOCD_SEC_INPUT_PRIORITY_DESC: "优先发送后输入的方向命令。",
    SOCD_FIRST_INPUT_PRIORITY: "前输入优先",
    SOCD_FIRST_INPUT_PRIORITY_DESC: "优先发送先输入的方向命令。",
    SOCD_BYPASS: "绕过",
    SOCD_BYPASS_DESC: "所有方向命令直接发送，不进行任何处理。",

    // Combination Field
    COMBINATION_FIELD_EDIT_BUTTON: "编辑",
    COMBINATION_SELECT_PLACEHOLDER: "选择游戏控制器按键",
    COMBINATION_DIALOG_TITLE: "编辑按键组合",
    COMBINATION_DIALOG_MESSAGE: `请选择最多 ${COMBINATION_KEY_MAX_LENGTH} 个按键组成组合键`,
    COMBINATION_DIALOG_BUTTON_CONFIRM: "确定",
    
} as const;

