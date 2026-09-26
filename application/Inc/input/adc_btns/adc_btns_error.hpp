#ifndef __ADC_BTNS_ERROR_HPP__
#define __ADC_BTNS_ERROR_HPP__

enum class ADCBtnsError {
    SUCCESS = 0,                    // 操作成功
    
    // 通用错误 (-1 ~ -9)
    INVALID_PARAMS = -1,           // 参数无效
    MEMORY_ERROR = -2,             // 内存错误
    NOT_INITIALIZED = -3,          // 未初始化
    ALREADY_INITIALIZED = -4,      // 已经初始化
    GAMEPAD_PROFILE_NOT_FOUND = -5,// 游戏手柄配置未找到
    DMA1_STOP_FAILED = -6,         // DMA1停止失败
    DMA2_STOP_FAILED = -7,         // DMA2停止失败
    DMA3_STOP_FAILED = -8,         // DMA3停止失败

    // ADC/DMA 相关错误 (-10 ~ -19)
    ADC1_CALIB_FAILED = -10,       // ADC1校准失败
    ADC2_CALIB_FAILED = -11,       // ADC2校准失败
    ADC3_CALIB_FAILED = -12,       // ADC3校准失败
    DMA1_START_FAILED = -13,       // DMA1启动失败
    DMA2_START_FAILED = -14,       // DMA2启动失败
    DMA3_START_FAILED = -15,       // DMA3启动失败
    DMA_ALREADY_STARTED = -16,     // DMA已经启动
    DMA_NOT_STARTED = -17,         // DMA未启动
    
    // 映射相关错误 (-20 ~ -29)
    MAPPING_NOT_FOUND = -20,       // 映射未找到
    MAPPING_ALREADY_EXISTS = -21,  // 映射已存在
    MAPPING_CREATE_FAILED = -22,   // 创建映射失败
    MAPPING_UPDATE_FAILED = -23,   // 更新映射失败
    MAPPING_DELETE_FAILED = -24,   // 删除映射失败
    MAPPING_INVALID_RANGE = -25,   // 映射范围无效
    MAPPING_STORAGE_FULL = -26,   // 映射存储已满
    MAPPING_STORAGE_EMPTY = -27,  // 映射存储为空
    
    // 标定相关错误 (-30 ~ -39)
    CALIBRATION_IN_PROGRESS = -30, // 正在标定中
    CALIBRATION_NOT_STARTED = -31, // 标定未开始
    CALIBRATION_INVALID_DATA = -32,// 标定数据无效
    CALIBRATION_FAILED = -33,      // 标定失败
    CALIBRATION_VALUES_NOT_FOUND = -34, // 校准值未找到
    CALIBRATION_VALUES_INVALID = -35,   // 校准值无效
    CALIBRATION_SAMPLE_OUT_OF_RANGE = -36, // 采样值超出范围
    CALIBRATION_SAMPLE_UNSTABLE = -37,     // 采样值不稳定
    CALIBRATION_TIMEOUT = -38,             // 校准超时

    // 标记相关错误 (-40 ~ -49)
    ALREADY_MARKING = -40,        // 正在标记中
    ALREADY_SAMPLING = -41,       // 正在采样中
    NOT_MARKING = -42,           // 未标记
    MARKING_VALUE_FULL = -43,     // 标记值已满
    MARKING_VALUE_OVERFLOW = -44, // 标记值溢出
    
    // 按钮相关错误 (-50 ~ -59)
    BUTTON_CONFIG_ERROR = -50,     // 按钮配置错误
    BUTTON_STATE_ERROR = -51,      // 按钮状态错误
    BUTTON_RANGE_ERROR = -52,      // 按钮范围错误
    
    // 其他错误 (-50 ~)
    UNKNOWN_ERROR = -99            // 未知错误
};

#endif 