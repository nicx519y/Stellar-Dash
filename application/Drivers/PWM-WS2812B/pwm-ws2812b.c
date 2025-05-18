#include "pwm-ws2812b.h"
#include "board_cfg.h"


/* WS2812B data protocol
|-------------------------------------------|
|T0H | 0.4us      | +-150ns
|T1H | 0.8us      | +-150ns
|T0L | 0.85us     | +-150ns
|T1L | 0.45us     | +-150ns
|RES | above 50us |
|-------------------------------------------
*/

// #define HIGH_CCR_CODE 183 // 1/240 * 183 = 0.76us; 1/240 * (300 - 183) = 0.49us;
// #define LOW_CCR_CODE 83 // 1/240 * 83 = 0.35us; 1/240 * (300 - 83) = 0.90us;
// #define DMA_BUFFER_LEN (((NUM_LED % 2 == 0) ? (NUM_LED + 4) : (NUM_LED + 5)) * 24) * NUM_LEDs_PER_ADC_BUTTON //RES = 4 * 24 * 300 * 1/240 = 120us > 50us

/* WS2812B-Mini-V3J data protocol
|-------------------------------------------|
|T0H | 220ns ~ 380ns
|T1H | 580ns ~ 1us
|T0L | 580ns ~ 1us
|T1L | 580ns ~ 1us
|RES | above 280us |
|-------------------------------------------
*/

#define HIGH_CCR_CODE 140 // 1/240MHz * 140 = 583.3ns (T1H); 1/240MHz * (300-140) = 666.7ns (T1L)
#define LOW_CCR_CODE   60 // 1/240MHz * 60 = 250ns (T0H); 1/240MHz * (300-60) = 1000ns (T0L)
#define DMA_BUFFER_LEN (((NUM_LED % 2 == 0) ? (NUM_LED + 10) : (NUM_LED + 11)) * 24) * NUM_LEDs_PER_ADC_BUTTON //RES = 10 * 24 * 300 * 1/240 = 300us > 280us

#define LED_DEFAULT_BRIGHTNESS 128

static bool WS2812B_IsInitialized = false;

static WS2812B_StateTypeDef WS2812B_State = WS2812B_STOP;

static uint8_t LED_Colors[NUM_LED * 3];

static uint8_t LED_Brightness[NUM_LED];

static __attribute__((section(".DMA_Section"))) uint32_t DMA_LED_Buffer[DMA_BUFFER_LEN];

void clearDCache(void *addr, uint32_t size)
{
	uint32_t alignedAddr = (uint32_t)addr & ~(32u - 1);
	uint32_t alignedSize = ((size + 31) & ~31);

	SCB_CleanInvalidateDCache_by_Addr((uint32_t *)alignedAddr, alignedSize);
}

void LEDDataToDMABuffer(const uint16_t start, const uint16_t length)
{
	// 检查缓冲区对齐
    if(((uint32_t)DMA_LED_Buffer & 0x1F) != 0) {
        APP_ERR("pwm-ws2812b: Error: DMA buffer not 32-byte aligned");
        return;
    }
	if(start + length > NUM_LED) {
		return;
	}

	uint16_t i, j, k;
	uint16_t len = (start + length) * 3;
	
	// printf("LEDDataToDMABuffer start: %d, length: %d", start, length);

	for(j = start * 3; j < len; j += 3)
    {
        double_t brightness = (double_t)LED_Brightness[j / 3] / 255.0;
        uint32_t color = RGBToHex((uint8_t)round(LED_Colors[j] * brightness), 
                                 (uint8_t)round(LED_Colors[j + 1] * brightness), 
                                 (uint8_t)round(LED_Colors[j + 2] * brightness));

        for(k = 0; k < NUM_LEDs_PER_ADC_BUTTON; k++) { // 每个BUTTON有NUM_LEDs_PER_ADC_BUTTON个LED，连续NUM_LEDs_PER_ADC_BUTTON个LED颜色一致
            for(i = 0; i < 24; i++) { // 每个LED有24个bit
                // 修复：修正 DMA buffer 索引计算
                uint32_t dma_idx = (j / 3 + k) * 24 + i;
                if(0x800000 & (color << i)) {
                    DMA_LED_Buffer[dma_idx] = HIGH_CCR_CODE;
                } else {
                    DMA_LED_Buffer[dma_idx] = LOW_CCR_CODE;
                }
            }
        }
    }

	// printf("LEDDataToDMABuffer end: %d, length: %d", start, length);

	clearDCache(DMA_LED_Buffer, sizeof(DMA_LED_Buffer));

	// printf("LEDDataToDMABuffer SCB_CleanInvalidateDCache_by_Addr end: %d, length: %d", start, length);
}

void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
	// APP_DBG("PWM-WS2812B-PulseFinished...");

	uint16_t start = DMA_BUFFER_LEN / 2 / 24 / NUM_LEDs_PER_ADC_BUTTON;

	uint16_t length = NUM_LED - start;

	if(length > 0) {
		LEDDataToDMABuffer(start, length);
	}

	// HAL_TIM_PWM_Stop_DMA(&htim4, TIM_CHANNEL_1);
}

void HAL_TIM_PWM_PulseFinishedHalfCpltCallback(TIM_HandleTypeDef *htim)
{
	// APP_DBG("PWM-WS2812B-PulseFinishedHalfCplt...");

	uint16_t length = DMA_BUFFER_LEN / 2 / 24 / NUM_LEDs_PER_ADC_BUTTON;

	(length < NUM_LED) ? LEDDataToDMABuffer(0, length): LEDDataToDMABuffer(0, NUM_LED);

}

void HAL_TIM_ErrorCallback(TIM_HandleTypeDef *htim)
{
	APP_ERR("PWM-WS2812B-ErrorCallback...");
}

void WS2812B_Init(void)
{
	if(WS2812B_IsInitialized) {
		APP_DBG("WS2812B_Init already initialized");
		return;
	}

	// 初始化灯效开关引脚 PC12
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	__HAL_RCC_GPIOC_CLK_ENABLE();
	GPIO_InitStruct.Pin = WS2812B_ENABLE_SWITCH_PIN;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(WS2812B_ENABLE_SWITCH_PORT, &GPIO_InitStruct);
	// 初始化灯效开关引脚为低电平
	HAL_GPIO_WritePin(WS2812B_ENABLE_SWITCH_PORT, WS2812B_ENABLE_SWITCH_PIN, GPIO_PIN_RESET);

	WS2812B_IsInitialized = true;

	APP_DBG("WS2812B_Init start...");

	memset(DMA_LED_Buffer, 0, DMA_BUFFER_LEN * sizeof(uint32_t)); // 清空DMA缓冲区

	APP_DBG("WS2812B_Init memset DMA_LED_Buffer end...");

	memset(&LED_Brightness, LED_DEFAULT_BRIGHTNESS, sizeof(LED_Brightness)); // 设置LED亮度

	APP_DBG("WS2812B_Init memset LED_Brightness end...");

	LEDDataToDMABuffer(0, NUM_LED);

	APP_DBG("WS2812B_Init LEDDataToDMABuffer end...");

	if(HAL_TIM_Base_GetState(&htim4) != HAL_TIM_STATE_READY) {
		APP_DBG("WS2812B_Init MX_TIM4_Init start...");
		MX_TIM4_Init();
	}

	APP_DBG("WS2812B_Init end...");
}

WS2812B_StateTypeDef WS2812B_Start()
{
	if(WS2812B_State != WS2812B_STOP) {
		return WS2812B_State;
	}

	// 打开灯效开关
	HAL_GPIO_WritePin(WS2812B_ENABLE_SWITCH_PORT, WS2812B_ENABLE_SWITCH_PIN, GPIO_PIN_SET);

	HAL_StatusTypeDef state = HAL_TIM_PWM_Start_DMA(&htim4, TIM_CHANNEL_1, (uint32_t *)&DMA_LED_Buffer, DMA_BUFFER_LEN);

	if(state == HAL_OK) {
		WS2812B_State = WS2812B_RUNNING;
		APP_DBG("WS2812B_Start success");
	} else {
		WS2812B_State = WS2812B_ERROR;
		APP_ERR("WS2812B_Start failure");
	}
	return WS2812B_State;
}

WS2812B_StateTypeDef WS2812B_Stop()
{
    if(WS2812B_State != WS2812B_RUNNING) {
        return WS2812B_State;
    }

	// 关闭灯效开关
	HAL_GPIO_WritePin(WS2812B_ENABLE_SWITCH_PORT, WS2812B_ENABLE_SWITCH_PIN, GPIO_PIN_RESET);

    HAL_StatusTypeDef state = HAL_TIM_PWM_Stop_DMA(&htim4, TIM_CHANNEL_1);

    if(state == HAL_OK) {
		WS2812B_State = WS2812B_STOP;
    } else {
		WS2812B_State = WS2812B_ERROR;
    }
    return WS2812B_State;
}

void WS2812B_SetAllLEDBrightness(const uint8_t brightness)
{
    memset(&LED_Brightness, brightness, sizeof(LED_Brightness));
	clearDCache(LED_Brightness, sizeof(LED_Brightness));
}

void WS2812B_SetAllLEDColor(const uint8_t r, const uint8_t g, const uint8_t b)
{
    int length = NUM_LED * 3;
    for(int i = 0; i < length; i += 3) {
        LED_Colors[i] = r;
        LED_Colors[i + 1] = g;
        LED_Colors[i + 2] = b;
    }
	clearDCache(LED_Colors, sizeof(LED_Colors));
}

void WS2812B_SetLEDBrightness(const uint8_t brightness, const uint16_t index)
{
	if(index < NUM_LED) {
		LED_Brightness[index] = brightness;
		clearDCache(&LED_Brightness[index], sizeof(uint8_t));
	}
}

void WS2812B_SetLEDColor(const uint8_t r, const uint8_t g, const uint8_t b, const uint16_t index)
{
	if(index < NUM_LED) {
		uint16_t idx = index * 3;
		LED_Colors[idx] = r;
		LED_Colors[idx + 1] = g;
		LED_Colors[idx + 2] = b;
		clearDCache(&LED_Colors[idx], 3 * sizeof(uint8_t));
	}
}

void WS2812B_SetLEDBrightnessByMask(
  const uint8_t fontBrightness,
  const uint8_t backgroundBrightness,
  const uint32_t mask
)
{
	uint8_t len = NUM_LED > 32 ? 32 : NUM_LED;

	for(uint8_t i = 0; i < len; i ++) {
		if((mask >> i & 1) == 1) {
			LED_Brightness[i] = fontBrightness;
		} else {
			LED_Brightness[i] = backgroundBrightness;
		}
	}

	clearDCache(LED_Brightness, NUM_LED);
}

/**
 * @brief 根据mask设置frontColor和backgroundColor
 * example: mask = 100100010000100 
 * 从右侧开始 0是backgroundColor, 1是frontColor
 * mask 是一个 32位整型，也就是说 led 总数不能超过32个
 * @param frontColor 
 * @param backgroundColor 
 * @param mask 
 */
void WS2812B_SetLEDColorByMask(
	const struct RGBColor frontColor, 
    const struct RGBColor backgroundColor, 
	const uint32_t mask)
{
	uint8_t len = NUM_LED > 32 ? 32 : NUM_LED;
	uint16_t idx;

	for(uint8_t i = 0; i < len; i ++) {
		idx = i * 3;
		if((mask >> i & 1) == 1) {
			LED_Colors[idx] = frontColor.r;
			LED_Colors[idx + 1] = frontColor.g;
			LED_Colors[idx + 2] = frontColor.b;
		} else {
			LED_Colors[idx] = backgroundColor.r;
			LED_Colors[idx + 1] = backgroundColor.g;
			LED_Colors[idx + 2] = backgroundColor.b;
		}
	}

	clearDCache(LED_Colors, NUM_LED * 3);
}

WS2812B_StateTypeDef WS2812B_GetState()
{
	return WS2812B_State;
}

void WS2812B_Test()
{
	uint8_t r = 171;
	uint8_t g = 21;
	uint8_t b = 176;

	WS2812B_Init();
	WS2812B_SetAllLEDBrightness(80);
	WS2812B_SetAllLEDColor(r, g, b);
	WS2812B_Start();

	APP_DBG("Hex: %x", RGBToHex(r, g, b));
}	


