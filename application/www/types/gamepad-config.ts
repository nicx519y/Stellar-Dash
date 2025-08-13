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
export const DEFAULT_FIRMWARE_SERVER_HOST = 'http://182.92.72.220:3000';

// hitbox button position list
const btnsPos = [
    { x: 120.19, y: 123.51, r: 26 },         // 0
    { x: 147.24, y: 130.70, r: 34 },         // 1
    { x: 174.30, y: 123.51, r: 26 },         // 2
    { x: 198.48, y: 117.14, r: 26 },         // 3
    { x: 73.39, y: 63.66, r: 26 },           // 4
    { x: 98.95, y: 59.57, r: 26 },           // 5
    { x: 122.09, y: 63.66, r: 26 },          // 6
    { x: 141.34, y: 77.13, r: 26 },          // 7
    { x: 131.09, y: 41.94, r: 26 },          // 8
    { x: 168.08, y: 79.30, r: 26 },          // 9
    { x: 157.34, y: 56.87, r: 26 },          // 10
    { x: 155.16, y: 31.97, r: 26 },          // 11
    { x: 188.56, y: 64.96, r: 26 },          // 12
    { x: 177.82, y: 42.53, r: 26 },          // 13
    { x: 212.05, y: 56.41, r: 26 },          // 14
    { x: 201.31, y: 33.98, r: 26 },          // 15
    { x: 236.96, y: 54.23, r: 26 },          // 16
    { x: 226.22, y: 31.80, r: 26 },          // 17
    { x: 66.39, y: 15.39, r: 11.5 },           // 18
    { x: 50.39, y: 15.39, r: 11.5 },           // 19
    { x: 34.39, y: 15.39, r: 11.5 },           // 20
    { x: 18.39, y: 15.39, r: 11.5 },           // 21
];

export const HITBOX_BTN_POS_SCALE = 2.56;

export const HITBOX_BTN_POS_LIST = btnsPos.map(item => ({ x: item.x * HITBOX_BTN_POS_SCALE, y: item.y * HITBOX_BTN_POS_SCALE, r: item.r }));

// keys settings interactive ids
export const KEYS_SETTINGS_INTERACTIVE_IDS = [ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20 ]; // 0-20 共21个按键可以交互，并设置为按键

// leds settings interactive ids
export const LEDS_SETTINGS_INTERACTIVE_IDS = [ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21 ]; // 0-21 共22个按键可以交互，并设置为led

// hotkeys settings interactive ids
export const HOTKEYS_SETTINGS_INTERACTIVE_IDS = [ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20 ]; // 0-20 共21个按键可以交互，并设置为hotkey

// rapid trigger settings interactive ids
export const RAPID_TRIGGER_SETTINGS_INTERACTIVE_IDS = [ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17 ]; // 0-17 共18个按键可以交互，并设置为rapid trigger



export enum Platform {
    XINPUT = "XINPUT",
    PS4 = "PS4",
    PS5 = "PS5",
    SWITCH = "SWITCH",
}

export const PlatformList = Object.values(Platform);

export const PlatformLabelMap = new Map<Platform, { label: string, description: string }>([
    [Platform.XINPUT, { 
        label: "XInput | Win", 
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

export const GameControllerButtonList = Object.values(GameControllerButton);



export const XInputButtonMap = new Map<GameControllerButton, string>([
    [GameControllerButton.DPAD_UP, "DPAD_UP"],
    [GameControllerButton.DPAD_DOWN, "DPAD_DOWN"],
    [GameControllerButton.DPAD_LEFT, "DPAD_LEFT"],
    [GameControllerButton.DPAD_RIGHT, "DPAD_RIGHT"],
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
    [GameControllerButton.DPAD_UP, "DPAD_UP"],
    [GameControllerButton.DPAD_DOWN, "DPAD_DOWN"],
    [GameControllerButton.DPAD_LEFT, "DPAD_LEFT"],
    [GameControllerButton.DPAD_RIGHT, "DPAD_RIGHT"],
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
    [GameControllerButton.B1, "\ue900"],  // SOUTH
    [GameControllerButton.B2, "\ue901"],   // EAST
    [GameControllerButton.B3, "\ue902"],   // WEST
    [GameControllerButton.B4, "\ue903"], // NORTH
]);

export const SwitchButtonMap = new Map<GameControllerButton, string>([
    [GameControllerButton.DPAD_UP, "DPAD_UP"],
    [GameControllerButton.DPAD_DOWN, "DPAD_DOWN"],
    [GameControllerButton.DPAD_LEFT, "DPAD_LEFT"],
    [GameControllerButton.DPAD_RIGHT, "DPAD_RIGHT"],
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

export const GameSocdModeLabelMap = new Map<GameSocdMode, { label: string, description: string }>([
    [GameSocdMode.SOCD_MODE_UP_PRIORITY, { 
        label: "Up Priority", 
        description: "The first input is prioritized when the two inputs are the same." 
    }],
    [GameSocdMode.SOCD_MODE_NEUTRAL, { 
        label: "Neutral", 
        description: "The first input is prioritized when the two inputs are different." 
    }],
    [GameSocdMode.SOCD_MODE_SECOND_INPUT_PRIORITY, { 
        label: "Sec Input Priority", 
        description: "The second input is prioritized when the two inputs are different." 
    }],
    [GameSocdMode.SOCD_MODE_FIRST_INPUT_PRIORITY, { 
        label: "First Input Priority", 
        description: "The first input is prioritized when the two inputs are different." 
    }],
    [GameSocdMode.SOCD_MODE_BYPASS, { 
        label: "Bypass", 
        description: "Bypass the SOCD mode and use the first input." 
    }],
]);

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
}

export const HotkeyActionList = Object.values(HotkeyAction);

export const HotkeyActionLabelMap = new Map<HotkeyAction, { label: string, description: string }>([
    [HotkeyAction.None, { 
        label: "None", 
        description: "No action" 
    }],
    [HotkeyAction.LedsEffectStyleNext, { 
        label: "Next LED Effect", 
        description: "Switch to next LED effect style" 
    }],
    [HotkeyAction.LedsEffectStylePrev, { 
        label: "Previous LED Effect", 
        description: "Switch to previous LED effect style" 
    }],
    [HotkeyAction.LedsBrightnessUp, { 
        label: "Increase Brightness", 
        description: "Increase LED brightness" 
    }],
    [HotkeyAction.LedsBrightnessDown, { 
        label: "Decrease Brightness", 
        description: "Decrease LED brightness" 
    }],
    [HotkeyAction.LedsEnableSwitch, { 
        label: "Toggle LEDs", 
        description: "Enable/Disable LEDs" 
    }],
    [HotkeyAction.AmbientLightEffectStyleNext, { 
        label: "Next Ambient Light Effect", 
        description: "Switch to next ambient light effect style" 
    }],
    [HotkeyAction.AmbientLightEffectStylePrev, { 
        label: "Previous Ambient Light Effect", 
        description: "Switch to previous ambient light effect style" 
    }],
    [HotkeyAction.AmbientLightBrightnessUp, { 
        label: "Increase Ambient Light Brightness", 
        description: "Increase ambient light brightness" 
    }],
    [HotkeyAction.AmbientLightBrightnessDown, { 
        label: "Decrease Ambient Light Brightness", 
        description: "Decrease ambient light brightness" 
    }],
    [HotkeyAction.AmbientLightEnableSwitch, { 
        label: "Toggle Ambient Light", 
        description: "Enable/Disable ambient light" 
    }],
    [HotkeyAction.WebConfigMode, { 
        label: "Web Config Mode", 
        description: "Enter web configuration mode" 
    }],
    [HotkeyAction.CalibrationMode, { 
        label: "Calibration Mode", 
        description: "Toggle calibration mode" 
    }],
    [HotkeyAction.XInputMode, { 
        label: "XInput Mode", 
        description: "Switch to XInput mode" 
    }],
    [HotkeyAction.PS4Mode, { 
        label: "PS4 Mode", 
        description: "Switch to PS4 mode" 
    }],
    [HotkeyAction.PS5Mode, { 
        label: "PS5 Mode", 
        description: "Switch to PS5 mode" 
    }],
    [HotkeyAction.NSwitchMode, { 
        label: "Nintendo Switch Mode", 
        description: "Switch to Nintendo Switch mode" 
    }],
]);

export type Hotkey = {
    key: number,
    action: HotkeyAction,
    isLocked?: boolean,
    isHold?: boolean,
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
    BUTTON_PREVIEW_IN_TABLE_VIEW: "Preview In Table View",

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

    SETTINGS_KEY_MAPPING_AUTO_SWITCH_LABEL: "Auto switch",
    SETTINGS_KEY_MAPPING_MANUAL_SWITCH_LABEL: "Manual switch",
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
    SETTINGS_BUTTONS_PERFORMANCE_TEST_HELPER_TEXT: "You can test the button performance here. Press and release the button on the device, and the travel distance and press/release points will be recorded here.\n- Press: The point when the button is pressed (mm)\n- Release: The point when the button is released (mm)",
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
    TOOLTIP_AUTO_SWITCH: "Auto Switch: Automatically switch the button field when the input key is changed.\nManual Switch: Manually set the active button field.",
    
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
    SETTINGS_KEYS_HELPER_TEXT: `Set the mapping between Hitbox buttons and game controller buttons.\n- Select a configuration box below, then press a Hitbox button on the left to bind it.\n- Multiple key mappings can be binded to one controller button.`,
    KEYS_ENABLE_START_BUTTON_LABEL: "Configuring keys enablement",
    KEYS_ENABLE_STOP_BUTTON_LABEL: "Exit configuring",

    // Switch Marking Settings
    SETTINGS_SWITCH_MARKING_TITLE: "NEW SWITCH MARKINGS",
    SETTINGS_SWITCH_MARKING_HELPER_TEXT: "Each switch marking can be customized with length and step value.\n- Length: The length of the switch marking.\n- Step: The step value of the switch marking.",

    // LEDs Settings
    SETTINGS_LEDS_TITLE: "LIGHTING SETTINGS",
    SETTINGS_LEDS_HELPER_TEXT: "The lighting effect style, colors, and brightness can be customized here.",

    // Rapid Trigger Settings
    SETTINGS_RAPID_TRIGGER_TITLE: "BUTTONS PERFORMANCE",
    SETTINGS_RAPID_TRIGGER_HELPER_TEXT: "The buttons performance settings can be customized here.\n- Top Deadzone: The distance from the top of the trigger to the deadzone.\n- Bottom Deadzone: The distance from the bottom of the trigger to the deadzone.\n- Press Accuracy: The accuracy of the trigger when pressed.\n- Release Accuracy: The accuracy of the trigger when released.",
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
    SETTINGS_HOTKEYS_HELPER_TEXT: `Configure up to ${DEFAULT_NUM_HOTKEYS_MAX} hotkeys for quick access to various functions.\n- Click on the hotkey field and press the desired key on the hitbox to bind the hotkey.\n- Choice the hotkey action from the dropdown list.\n- Locked hotkeys are used for web configuration mode because this function is required.\n- Enable device button monitoring to bind hotkeys by pressing the buttons on the device.`,
    SETTINGS_HOTKEYS_BUTTON_MONITORING_TITLE: "Device Button Monitoring",

    // Firmware Settings
    SETTINGS_FIRMWARE_TITLE: "FIRMWARE UPDATE",
    SETTINGS_FIRMWARE_CURRENT_VERSION_LABEL: "Current Version: ",
    SETTINGS_FIRMWARE_LATEST_VERSION_LABEL: "Latest Firmware Version: ",
    SETTINGS_FIRMWARE_NO_UPDATE_MESSAGE: "Current firmware is the latest version. No update available.",
    SETTINGS_FIRMWARE_UPDATE_TODO_MESSAGE: "Please click the button to update the firmware, it will take a few minutes, please do not disconnect the device.",
    SETTINGS_FIRMWARE_UPDATE_SUCCESS_TITLE: "Firmware updated successfully",
    SETTINGS_FIRMWARE_UPDATE_SUCCESS_MESSAGE: "Congratulations! The firmware has been updated successfully.\nTo access the latest driver page, the page will be refreshed in {seconds} seconds.",
    SETTINGS_FIRMWARE_UPDATE_FAILED_MESSAGE: "The firmware update failed. Please click the button to try again.",
    SETTINGS_FIRMWARE_UPDATING_MESSAGE: "Updating firmware... Please do not disconnect the device.",
    SETTINGS_FIRMWARE_UPDATE_SUCCESS_BUTTON: "Refresh Page",

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
    CALIBRATION_TIP_MESSAGE: "The buttons on the device that are blue are the buttons to be calibrated. Press the blue button until the LED light turns green, then the calibration is complete. When all buttons are calibrated, click the [Stop Calibration] button to end the calibration mode.",

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
    HOTKEY_ACTION_SYSTEM_REBOOT: "System Reboot",

    HOTKEY_TRIGGER_HOLD: "Hold",
    HOTKEY_TRIGGER_CLICK: "Click",

    // Reconnect Modal
    RECONNECT_MODAL_MESSAGE: "The connection to the device has been lost, possible reasons: \n1. The USB connection has been lost. \n2. The device is not in webconfig mode. \n3. Other web pages are connected to the device at the same time. \nPlease check and click the button to reconnect.",
    RECONNECT_MODAL_BUTTON: "Reconnect Device",
    RECONNECT_MODAL_TITLE: "Device Disconnected",
    RECONNECT_FAILED_TITLE: "Failed to Reconnect",
    RECONNECT_FAILED_MESSAGE: "Failed to reconnect to the device. Please check the USB connection and make sure the device is in webconfig mode, then try again.",

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

    SETTINGS_KEY_MAPPING_AUTO_SWITCH_LABEL: "自动切换",
    SETTINGS_KEY_MAPPING_MANUAL_SWITCH_LABEL: "手动切换",
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
    SETTINGS_BUTTONS_PERFORMANCE_TEST_HELPER_TEXT: "可以在这里测试按键性能。按下并释放设备上的磁轴按键，这里会记录下按键的行程，以及按下和触发的点位。\n- 按下：按键按下时的点位(mm)\n- 释放：按键释放时的点位(mm)",
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
    TOOLTIP_AUTO_SWITCH: "自动切换：输入按键改变时自动切换按键区域\n手动切换：手动设置活动按键区域",
    
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
    SETTINGS_KEYS_HELPER_TEXT: `设置hitbox按键和游戏控制器按键的映射关系。\n- 选中下面的配置框，然后按左侧hitbox按键即可绑定 \n- 可以设置多个按键映射，每个控制器按键映射可以设置多个hitbox按键`,
    KEYS_ENABLE_START_BUTTON_LABEL: "开始配置按键启用/禁用",
    KEYS_ENABLE_STOP_BUTTON_LABEL: "结束配置按键启用/禁用",

    // 磁轴标记设置
    SETTINGS_SWITCH_MARKING_TITLE: "新磁轴标记",
    SETTINGS_SWITCH_MARKING_HELPER_TEXT: "每个磁轴标记可以自定义长度和步进值。用于形成磁轴行程和模拟电压值的映射关系。\n- 长度：磁轴标记的长度\n- 步进值：磁轴标记的步进值",
    
    // LED设置
    SETTINGS_LEDS_TITLE: "灯光设置",
    SETTINGS_LEDS_HELPER_TEXT: "可以在这里自定义LED效果样式、颜色和亮度。",
    
    // 快速触发设置
    SETTINGS_RAPID_TRIGGER_TITLE: "按键性能",
    SETTINGS_RAPID_TRIGGER_HELPER_TEXT: "可以在这里自定义按键性能。\n- 顶部死区：扳机顶部到死区的距离\n- 底部死区：扳机底部到死区的距离\n- 按下精度：扳机按下时的精度\n- 释放精度：扳机释放时的精度",
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
    SETTINGS_HOTKEYS_HELPER_TEXT: `最多可以配置${DEFAULT_NUM_HOTKEYS_MAX}个快捷键来快速访问各种功能。\n- 点击快捷键区域并在左侧hitbox上按下想要绑定的按键\n- 从下拉列表中选择快捷键动作\n- 锁定的快捷键用于网页配置模式，因为这个功能是必需的\n- 启用设备按键监控后，可以直接按下设备上的按键来绑定热键`,
    SETTINGS_HOTKEYS_BUTTON_MONITORING_TITLE: "设备按键监控",

    // 固件更新
    SETTINGS_FIRMWARE_TITLE: "设备固件更新",
    SETTINGS_FIRMWARE_CURRENT_VERSION_LABEL: "当前固件版本: ",
    SETTINGS_FIRMWARE_LATEST_VERSION_LABEL: "最新固件版本: ",
    SETTINGS_FIRMWARE_NO_UPDATE_MESSAGE: "当前固件版本为最新版本，没有可用的更新",
    SETTINGS_FIRMWARE_UPDATE_TODO_MESSAGE: "请点击按钮更新固件，过程需要几分钟时间，不要断开设备连接",
    SETTINGS_FIRMWARE_UPDATE_SUCCESS_TITLE: "固件更新成功",
    SETTINGS_FIRMWARE_UPDATE_SUCCESS_MESSAGE: "太棒了！固件更新成功。\n为了访问最新的驱动页面，{seconds}秒后，页面将自动刷新",
    SETTINGS_FIRMWARE_UPDATE_FAILED_MESSAGE: "固件更新失败，请点击按钮重试",
    SETTINGS_FIRMWARE_UPDATING_MESSAGE: "固件更新中...请不要断开设备连接",
    SETTINGS_FIRMWARE_UPDATE_SUCCESS_BUTTON: "刷新页面",

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
    SETTINGS_TAB_FIRMWARE: "固件",

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
    CALIBRATION_TIP_MESSAGE: "设备上深蓝色的按钮代表待校准的按钮，把深蓝色的按钮按压到底，直到按钮上的LED灯变成绿色，则校准完成。所有按键校准完成后，点击[结束校准]按钮结束校准模式。",

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
    HOTKEY_ACTION_SYSTEM_REBOOT: "系统重启",

    HOTKEY_TRIGGER_HOLD: "长按",
    HOTKEY_TRIGGER_CLICK: "短按",

    // Reconnect Modal
    RECONNECT_MODAL_MESSAGE: "与设备的连接已断开，可能的原因：\n1. 设备USB已经断开。\n2. 设备没有打开网页配置模式。\n3. 同时打开了其他网页连接了设备。\n请检查无误后点击按钮重新连接。",
    RECONNECT_MODAL_BUTTON: "重新连接设备",
    RECONNECT_MODAL_TITLE: "设备断开连接",
    RECONNECT_FAILED_TITLE: "重连设备失败",
    RECONNECT_FAILED_MESSAGE: "无法重新连接到设备，请检查USB连接，以及保证设备在网页配置模式并重试。",
    
} as const;

