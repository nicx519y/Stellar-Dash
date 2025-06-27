#include "leds/led_animation.hpp"
#include "board_cfg.h"
#include <random>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// 按钮位置数组定义 (从TypeScript移植，调整为实际硬件数量)
// const ButtonPosition HITBOX_LED_POS_LIST[NUM_LED + NUM_LED_AROUND] = {
//     { 376.2f, 379.8f, 36.0f },      // 0
//     { 299.52f, 352.44f, 28.63f },   // 1
//     { 452.88f, 352.44f, 28.63f },   // 2
//     { 523.00f, 328.44f, 28.63f },   // 3
//     { 304.97f, 182.0f, 28.63f },    // 4
//     { 239.31f, 170.56f, 28.63f },   // 5
//     { 359.52f, 220.35f, 28.63f },   // 6
//     { 330.43f, 120.46f, 28.63f },   // 7
//     { 435.24f, 226.76f, 28.63f },   // 8
//     { 404.82f, 163.22f, 28.63f },   // 9
//     { 398.52f, 92.67f, 28.63f },    // 10
//     { 493.2f, 186.48f, 28.63f },    // 11
//     { 462.78f, 122.94f, 28.63f },   // 12
//     { 559.8f, 162.36f, 28.63f },    // 13
//     { 529.43f, 98.67f, 28.63f },    // 14
//     { 630.36f, 156.06f, 28.63f },   // 15
//     { 599.94f, 92.52f, 28.63f },    // 16
//     { 184.03f, 46.03f, 28.63f },    // 17
//     { 140.02f, 46.03f, 28.63f },    // 18
//     { 96.01f, 46.03f, 28.63f },     // 19
//     { 51.99f, 46.03f, 28.63f }      // 20

//     #if HAS_LED_AROUND
//     { 184.03f, 46.03f, 11.37f },    // 17
//     { 140.02f, 46.03f, 11.37f },    // 18
//     { 96.01f, 46.03f, 11.37f },     // 19
//     { 51.99f, 46.03f, 11.37f }      // 20
//     #endif
// };

const ButtonPosition HITBOX_LED_POS_LIST[NUM_LED + NUM_LED_AROUND] = {
    { 147.24f, 130.70f, 26.00f },      // 0
    { 120.19f, 123.51f, 21.50f },   // 1
    { 174.30f, 123.51f, 21.50f },   // 2
    { 198.48f, 117.14f, 21.50f },   // 3
    { 122.10f, 63.66f, 21.50f },   // 4
    { 98.95f, 59.57f, 21.50f },   // 5
    { 141.34f, 77.13f, 21.50f },   // 6
    { 131.09f, 41.94f, 21.50f },   // 7
    { 168.08f, 79.30f, 21.50f },   // 8
    { 157.34f, 56.87f, 21.50f },   // 9
    { 155.16f, 31.97f, 21.50f },    // 10
    { 188.56f, 64.96f, 21.50f },   // 11
    { 177.82f, 42.53f, 21.50f },    // 12
    { 212.05f, 56.41f, 21.50f },    // 13
    { 201.31f, 33.98f, 21.50f },   // 14
    { 236.96f, 54.23f, 21.50f },    // 15
    { 226.22f, 31.80f, 21.50f },    // 16
    { 84.39f, 15.39f, 21.50f },    // 17
    { 62.39f, 15.39f, 21.50f },     // 18
    { 40.39f, 15.39f, 21.50f },      // 19
    { 18.39f, 15.39f, 21.50f },      // 20

    #if HAS_LED_AROUND
    { 18.20f, 3.00f, 5.40f },    // 21
    { 48.60f, 3.00f, 5.40f },    // 22
    { 79.00f, 3.00f, 5.40f },    // 23
    { 109.40f, 3.00f, 5.40f },    // 24
    { 200.60f, 3.00f, 5.40f },    // 25
    { 231.00f, 3.00f, 5.40f },    // 26
    { 261.40f, 3.00f, 5.40f },    // 27
    { 291.80f, 3.00f, 5.40f },    // 28
    { 307.00f, 19.80f, 5.40f },    // 29
    { 307.00f, 50.20f, 5.40f },    // 30
    { 307.00f, 80.60f, 5.40f },    // 31
    { 307.00f, 111.00f, 5.40f },    // 32
    { 307.00f, 141.40f, 5.40f },    // 33
    { 307.00f, 171.80f, 5.40f },    // 34
    { 291.80f, 187.00f, 5.40f },    // 35
    { 261.40f, 187.00f, 5.40f },    // 36
    { 231.00f, 187.00f, 5.40f },    // 37
    { 200.60f, 187.00f, 5.40f },    // 38
    { 170.20f, 187.00f, 5.40f },    // 39
    { 139.80f, 187.00f, 5.40f },    // 40
    { 109.40f, 187.00f, 5.40f },    // 41
    { 79.00f, 187.00f, 5.40f },    // 42
    { 48.60f, 187.00f, 5.40f },    // 43
    { 18.20f, 187.00f, 5.40f },    // 44
    { 3.00f, 171.80f, 5.40f },    // 45
    { 3.00f, 141.40f, 5.40f },    // 46
    { 3.00f, 111.00f, 5.40f },    // 47
    { 3.00f, 80.60f, 5.40f },    // 48
    { 3.00f, 50.20f, 5.40f },    // 49
    { 3.00f, 19.80f, 5.40f }    // 50
    #endif
};

// 全局变量用于动画状态
static uint8_t currentStarButtons1[5] = {0};
static uint8_t currentStarButtons2[5] = {0};
static uint8_t starButtons1Count = 0;
static uint8_t starButtons2Count = 0;
static bool isFirstHalf = true;

static Ripple ripples[5];
static uint8_t rippleCount = 0;

static uint8_t transformPassedPositions[NUM_LED + NUM_LED_AROUND] = {0}; // 0: 未经过, 1: 已经过
static uint32_t transformCycleCount = 0;
static float lastTransformProgress = 0.0f;

// 缓存的边界值，避免每帧重复计算
static float cachedMinX = 0.0f;
static float cachedMaxX = 0.0f;
static bool boundariesCalculated = false;

// 计算按钮X坐标边界的函数
static void calculateBoundaries() {
    if (!boundariesCalculated) {
        cachedMinX = HITBOX_LED_POS_LIST[0].x;
        cachedMaxX = HITBOX_LED_POS_LIST[0].x;
        
        // 遍历所有按钮位置找到最小和最大X坐标
        for (uint8_t i = 1; i < NUM_LED; i++) {
            if (HITBOX_LED_POS_LIST[i].x < cachedMinX) {
                cachedMinX = HITBOX_LED_POS_LIST[i].x;
            }
            if (HITBOX_LED_POS_LIST[i].x > cachedMaxX) {
                cachedMaxX = HITBOX_LED_POS_LIST[i].x;
            }
        }
        
        // 添加缓冲区
        cachedMinX -= 100.0f;
        cachedMaxX += 100.0f;
        
        boundariesCalculated = true;
    }
}

// 颜色插值函数
RGBColor lerpColor(const RGBColor& colorA, const RGBColor& colorB, float t) {
    t = fmaxf(0.0f, fminf(1.0f, t)); // 确保t在0-1范围内
    
    RGBColor result;
    result.r = (uint8_t)(colorA.r * (1.0f - t) + colorB.r * t);
    result.g = (uint8_t)(colorA.g * (1.0f - t) + colorB.g * t);
    result.b = (uint8_t)(colorA.b * (1.0f - t) + colorB.b * t);
    
    return result;
}

// 随机选择不重复的按钮
static uint8_t selectRandomButtons(uint8_t total, uint8_t count, uint8_t* exclude, uint8_t excludeCount, uint8_t* result) {
    uint8_t available[NUM_LED];
    uint8_t availableCount = 0;
    
    // 生成可用按钮列表
    for (uint8_t i = 0; i < total; i++) {
        bool isExcluded = false;
        for (uint8_t j = 0; j < excludeCount; j++) {
            if (exclude[j] == i) {
                isExcluded = true;
                break;
            }
        }
        if (!isExcluded) {
            available[availableCount++] = i;
        }
    }
    
    // 随机选择
    uint8_t actualCount = (count < availableCount) ? count : availableCount;
    for (uint8_t i = 0; i < actualCount && availableCount > 0; i++) {
        uint8_t randomIndex = rand() % availableCount;
        result[i] = available[randomIndex];
        
        // 移除已选择的按钮
        for (uint8_t j = randomIndex; j < availableCount - 1; j++) {
            available[j] = available[j + 1];
        }
        availableCount--;
    }
    
    return actualCount;
}

// 静态动画
RGBColor staticAnimation(const LedAnimationParams& params) {
    RGBColor color = params.colorEnabled
        ? (params.pressed ? params.frontColor : params.backColor1)
        : params.defaultBackColor;
    
    // 应用亮度
    color.r = (uint8_t)(color.r * params.brightness / 100);
    color.g = (uint8_t)(color.g * params.brightness / 100);
    color.b = (uint8_t)(color.b * params.brightness / 100);
    
    return color;
}

// 呼吸动画
RGBColor breathingAnimation(const LedAnimationParams& params) {
    RGBColor color;
    
    if (params.colorEnabled) {
        if (params.pressed) {
            color = params.frontColor;
        } else {
            float t = sinf(params.progress * M_PI);
            color = lerpColor(params.backColor1, params.backColor2, t);
        }
    } else {
        color = params.defaultBackColor;
    }
    
    // 应用亮度
    color.r = (uint8_t)(color.r * params.brightness / 100);
    color.g = (uint8_t)(color.g * params.brightness / 100);
    color.b = (uint8_t)(color.b * params.brightness / 100);
    
    return color;
}

// 星光闪烁动画
RGBColor starAnimation(const LedAnimationParams& params) {
    if (!params.colorEnabled) {
        return params.defaultBackColor;
    }
    
    if (params.pressed) {
        RGBColor color = params.frontColor;
        color.r = (uint8_t)(color.r * params.brightness / 100);
        color.g = (uint8_t)(color.g * params.brightness / 100);
        color.b = (uint8_t)(color.b * params.brightness / 100);
        return color;
    }
    
    // 星光动画本身使用2倍速度（与 TypeScript 版本保持一致）
    float fastProgress = fmodf(params.progress * 2.0f, 1.0f);
    
    // 在周期的中点更新闪烁按钮
    bool currentHalf = fastProgress < 0.5f;
    if (currentHalf != isFirstHalf) {
        uint8_t exclude[10];
        uint8_t excludeCount = starButtons1Count + starButtons2Count;
        memcpy(exclude, currentStarButtons1, starButtons1Count);
        memcpy(exclude + starButtons1Count, currentStarButtons2, starButtons2Count);
        
        if (currentHalf) { // 开始新的周期
            uint8_t numStars = 2 + (rand() % 2); // 2-3个
            starButtons1Count = selectRandomButtons(NUM_LED, numStars, exclude, excludeCount, currentStarButtons1);
        } else { // 在周期中点更新第二组按钮
            uint8_t numStars = 2 + (rand() % 2);
            starButtons2Count = selectRandomButtons(NUM_LED, numStars, exclude, excludeCount, currentStarButtons2);
        }
        isFirstHalf = currentHalf;
    }
    
    // 检查当前按钮是否在闪烁列表中
    bool inGroup1 = false, inGroup2 = false;
    for (uint8_t i = 0; i < starButtons1Count; i++) {
        if (currentStarButtons1[i] == params.index) {
            inGroup1 = true;
            break;
        }
    }
    for (uint8_t i = 0; i < starButtons2Count; i++) {
        if (currentStarButtons2[i] == params.index) {
            inGroup2 = true;
            break;
        }
    }
    
    if (!inGroup1 && !inGroup2) {
        RGBColor color = params.backColor1;
        color.r = (uint8_t)(color.r * params.brightness / 100);
        color.g = (uint8_t)(color.g * params.brightness / 100);
        color.b = (uint8_t)(color.b * params.brightness / 100);
        return color;
    }
    
    // 计算渐变进度
    float fadeInOut = 0.0f;
    
    if (inGroup1) {
        float cycleProgress = fastProgress * 2.0f;
        fadeInOut = sinf(cycleProgress * M_PI / 2.0f);
    }
    
    if (inGroup2) {
        float cycleProgress = fmodf(fastProgress + 0.5f, 1.0f) * 2.0f;
        fadeInOut = sinf(cycleProgress * M_PI / 2.0f);
    }
    
    RGBColor result = lerpColor(params.backColor1, params.backColor2, fadeInOut);
    result.r = (uint8_t)(result.r * params.brightness / 100);
    result.g = (uint8_t)(result.g * params.brightness / 100);
    result.b = (uint8_t)(result.b * params.brightness / 100);
    
    return result;
}

// 流光动画
RGBColor flowingAnimation(const LedAnimationParams& params) {
    if (!params.colorEnabled) {
        return params.defaultBackColor;
    }
    
    if (params.pressed) {
        RGBColor color = params.frontColor;
        color.r = (uint8_t)(color.r * params.brightness / 100);
        color.g = (uint8_t)(color.g * params.brightness / 100);
        color.b = (uint8_t)(color.b * params.brightness / 100);
        return color;
    }
    
    // 确保边界值已计算
    calculateBoundaries();
    
    // 流光参数
    float bandWidth = 140.0f; // 光带宽度，与TypeScript版本保持一致
    
    // 当前流光中心位置
    float centerX = cachedMinX + (cachedMaxX - cachedMinX) * params.progress * 1.6f;
    float btnX = HITBOX_LED_POS_LIST[params.index].x;
    
    // 计算距离中心的归一化距离
    float dist = fabsf(btnX - centerX);
    
    // 使用更平滑的渐变算法，避免闪烁
    float t = 0.0f;
    if (dist <= bandWidth) {
        // 使用平滑的渐变函数，确保在边界处没有突变
        float normalizedDist = dist / bandWidth; // 0~1
        // 使用 smoothstep 函数创建更平滑的过渡
        t = 1.0f - (normalizedDist * normalizedDist * (3.0f - 2.0f * normalizedDist));
    }
    
    // 渐变色
    RGBColor result = lerpColor(params.backColor1, params.backColor2, t);
    result.r = (uint8_t)(result.r * params.brightness / 100);
    result.g = (uint8_t)(result.g * params.brightness / 100);
    result.b = (uint8_t)(result.b * params.brightness / 100);
    
    return result;
}

// 涟漪动画
RGBColor rippleAnimation(const LedAnimationParams& params) {
    if (!params.colorEnabled) {
        return params.defaultBackColor;
    }
    
    if (params.pressed) {
        RGBColor color = params.frontColor;
        color.r = (uint8_t)(color.r * params.brightness / 100);
        color.g = (uint8_t)(color.g * params.brightness / 100);
        color.b = (uint8_t)(color.b * params.brightness / 100);
        return color;
    }
    
    float t = 0.0f;
    
    // 处理涟漪效果
    for (uint8_t i = 0; i < params.global.rippleCount && i < 5; i++) {
        uint8_t centerIndex = params.global.rippleCenters[i];
        float progress = params.global.rippleProgress[i];
        
        float centerX = HITBOX_LED_POS_LIST[centerIndex].x;
        float centerY = HITBOX_LED_POS_LIST[centerIndex].y;
        float btnX = HITBOX_LED_POS_LIST[params.index].x;
        float btnY = HITBOX_LED_POS_LIST[params.index].y;
        
        // 计算最大距离
        float maxDist = 0.0f;
        for (uint8_t j = 0; j < NUM_LED; j++) {
            float dx = HITBOX_LED_POS_LIST[j].x - centerX;
            float dy = HITBOX_LED_POS_LIST[j].y - centerY;
            float dist = sqrtf(dx * dx + dy * dy);
            if (dist > maxDist) maxDist = dist;
        }
        
        float rippleRadius = progress * maxDist * 1.1f;
        float rippleWidth = 80.0f;
        float dx = btnX - centerX;
        float dy = btnY - centerY;
        float dist = sqrtf(dx * dx + dy * dy);
        
        if (fabsf(rippleRadius - dist) < rippleWidth) {
            float tt = cosf((fabsf(rippleRadius - dist) / rippleWidth) * M_PI / 2.0f);
            t = fmaxf(t, tt);
        }
    }
    
    RGBColor result = lerpColor(params.backColor1, params.backColor2, t);
    result.r = (uint8_t)(result.r * params.brightness / 100);
    result.g = (uint8_t)(result.g * params.brightness / 100);
    result.b = (uint8_t)(result.b * params.brightness / 100);
    
    return result;
}

// 变换动画
RGBColor transformAnimation(const LedAnimationParams& params) {
    if (!params.colorEnabled) {
        return params.defaultBackColor;
    }
    
    if (params.pressed) {
        RGBColor color = params.frontColor;
        color.r = (uint8_t)(color.r * params.brightness / 100);
        color.g = (uint8_t)(color.g * params.brightness / 100);
        color.b = (uint8_t)(color.b * params.brightness / 100);
        return color;
    }
    
    // 检测新的动画周期开始
    if (params.progress < lastTransformProgress && lastTransformProgress > 0.8f) {
        transformCycleCount++;
        memset(transformPassedPositions, 0, sizeof(transformPassedPositions));
    }
    lastTransformProgress = params.progress;
    
    // 确保边界值已计算
    calculateBoundaries();
    
    // 流光参数
    float bandWidth = 140.0f;
    float centerX = cachedMinX + (cachedMaxX - cachedMinX) * params.progress * 1.6f;
    float btnX = HITBOX_LED_POS_LIST[params.index].x;
    
    // 记录流光已经经过的按钮
    if (centerX > btnX + bandWidth / 2.0f) {
        transformPassedPositions[params.index] = 1;
    }
    
    // 计算该按钮被经过的总次数
    bool hasBeenPassed = transformPassedPositions[params.index] == 1;
    uint32_t totalPasses = transformCycleCount + (hasBeenPassed ? 1 : 0);
    bool isOddPasses = (totalPasses % 2) == 1;
    
    // 确定该按钮当前应该显示的基础颜色（经过后永久改变）
    RGBColor buttonBaseColor = isOddPasses ? params.backColor2 : params.backColor1;
    RGBColor buttonAltColor = isOddPasses ? params.backColor1 : params.backColor2;
    
    // 计算渐变区域
    float leftEdge = centerX - bandWidth / 2.0f;
    float rightEdge = centerX + bandWidth / 2.0f;
    
    RGBColor color;
    
    if (btnX < leftEdge) {
        // 左侧区域：使用该按钮的基础颜色
        color = buttonBaseColor;
    } else if (btnX > rightEdge) {
        // 右侧区域：使用该按钮的基础颜色
        color = buttonBaseColor;
    } else {
        // 渐变区域：从替代颜色渐变到基础颜色
        float t = (btnX - leftEdge) / bandWidth; // 0~1
        // 使用 smoothstep 函数创建更平滑的过渡
        float smoothT = t * t * (3.0f - 2.0f * t);
        color = lerpColor(buttonAltColor, buttonBaseColor, smoothT);
    }
    
    color.r = (uint8_t)(color.r * params.brightness / 100);
    color.g = (uint8_t)(color.g * params.brightness / 100);
    color.b = (uint8_t)(color.b * params.brightness / 100);
    
    return color;
}

// 获取动画算法函数
LedAnimationAlgorithm getLedAnimation(LEDEffect effect) {
    switch (effect) {
        case LEDEffect::STATIC:
            return staticAnimation;
        case LEDEffect::BREATHING:
            return breathingAnimation;
        case LEDEffect::STAR:
            return starAnimation;
        case LEDEffect::FLOWING:
            return flowingAnimation;
        case LEDEffect::RIPPLE:
            return rippleAnimation;
        case LEDEffect::TRANSFORM:
            return transformAnimation;
        default:
            return staticAnimation;
    }
} 