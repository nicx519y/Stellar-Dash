#include "stm32h7xx_hal.h"
#include "board_cfg.h"
#include "board.h"
#include "qspi-w25q64.h"
#include "usart.h"
#include "adc.h"
#include "dma.h"
#include "bdma.h"
#include "tim.h"
#include "pwm-ws2812b.h"
#include "utils.h"
#include "board_power.hpp"

UART_HandleTypeDef UartHandle;

void SystemClock_Config(void);
void PeriphCommonClock_Config(void);

void board_init(void)
{
    SystemClock_Config();
    PeriphCommonClock_Config();

    // Enable All GPIOs clocks
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
#ifdef __HAL_RCC_GPIOI_CLK_ENABLE
    __HAL_RCC_GPIOI_CLK_ENABLE();
#endif
    __HAL_RCC_GPIOJ_CLK_ENABLE();

    BoardPower_Initialize();

#if APPLICATION_SERIAL_PRINT || APPLICATION_STARTUP_LOG
    USART1_Init(); // USART for debug
    APP_STAGE("A01", "reset reached; HAL, wake hold, caches, clocks, GPIO and USART1 ready");
    APP_STAGE("A02", "power policy initialized: MAIN_POWER_EN=on LCD_EN=on optional rails=off");
#endif

    // 验证时钟配置
    APP_DBG("board init: SYSCLK: %lu", HAL_RCC_GetSysClockFreq());
    APP_DBG("board init: HCLK: %lu", HAL_RCC_GetHCLKFreq());
    APP_DBG("board init: PCLK1: %lu", HAL_RCC_GetPCLK1Freq());
    APP_DBG("board init: PCLK2: %lu", HAL_RCC_GetPCLK2Freq());
    APP_DBG("DBGMCU REVID: 0x%lx", HAL_GetREVID());

    int8_t qspi_init_result = QSPI_W25Qxx_Init();
    if (qspi_init_result == QSPI_W25Qxx_OK) {
        APP_STAGE("A03", "QSPI initialization complete");
    } else {
        APP_STAGE_ERROR("A03", "QSPI initialization failed: %d",
                        qspi_init_result);
    }

    // QSPI_W25Qxx_Test(0x00500000);

    MX_DMA_Init();
    APP_DBG("board init: MX_DMA_Init success.");

    MX_TIM2_Init(); // RF/USB report scheduler timer, reconfigured by ReportScheduler at runtime
    APP_DBG("board init: MX_TIM2_Init success.");

    MX_BDMA_Init();

    APP_DBG("board init: MX_BDMA_Init success.");
    APP_STAGE("A04", "TIM2, DMA and BDMA initialized");

    MX_ADC1_Init();

    APP_DBG("board init: MX_ADC1_Init success.");

    MX_ADC2_Init();

    APP_DBG("board init: MX_ADC2_Init success.");

    MX_ADC3_Init();

    APP_DBG("board init: MX_ADC3_Init success.");
    APP_STAGE("A05", "ADC1, ADC2 and ADC3 initialized");

#ifdef HAS_LED
    WS2812B_Init();
    APP_DBG("board init: WS2812B_Init success.");
    APP_STAGE("A06", "WS2812B peripheral initialized");
#endif // HAS_LED
}

/**
 * @brief System Clock Configuration
 * @retval None
 */
void SystemClock_Config(void)
{

    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    // 更新 SystemCoreClock 变量
    SystemCoreClockUpdate();

    /** Supply configuration update enable */
    HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

    /** Configure the main internal regulator output voltage */
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY))
    {
    }

    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

    while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY))
    {
    }

    /** Configure LSE Drive Capability */
    HAL_PWR_EnableBkUpAccess();

    /** Initializes the RCC Oscillators according to the specified parameters
     * in the RCC_OscInitTypeDef structure.
     */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM = 5;
    RCC_OscInitStruct.PLL.PLLN = 192;
    RCC_OscInitStruct.PLL.PLLP = 2;
    RCC_OscInitStruct.PLL.PLLQ = 2;
    RCC_OscInitStruct.PLL.PLLR = 2;
    RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
    RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
    RCC_OscInitStruct.PLL.PLLFRACN = 0;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    /** Initializes the CPU, AHB and APB buses clocks */
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_D3PCLK1 | RCC_CLOCKTYPE_D1PCLK1;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
    RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
    {
        Error_Handler();
    }

    

}

/**
 * @brief Peripherals Common Clock Configuration
 * @retval None
 */
void PeriphCommonClock_Config(void)
{
    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

    /** Initializes the peripherals clock
     */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USART1 | RCC_PERIPHCLK_ADC;
    PeriphClkInitStruct.Usart16ClockSelection = RCC_USART16CLKSOURCE_D2PCLK2;
    PeriphClkInitStruct.PLL3.PLL3M = 5;
    PeriphClkInitStruct.PLL3.PLL3N = 36;
    PeriphClkInitStruct.PLL3.PLL3P = 2;
    PeriphClkInitStruct.PLL3.PLL3Q = 4;
    PeriphClkInitStruct.PLL3.PLL3R = 4;
    PeriphClkInitStruct.PLL3.PLL3RGE = RCC_PLL3VCIRANGE_2;
    PeriphClkInitStruct.PLL3.PLL3VCOSEL = RCC_PLL3VCOMEDIUM;
    PeriphClkInitStruct.PLL3.PLL3FRACN = 0;
    PeriphClkInitStruct.AdcClockSelection = RCC_ADCCLKSOURCE_PLL3;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
 * @brief 获取当前槽的基地址
 * @note 通过检查当前代码运行的地址来判断处于哪个槽
 * @retval 当前槽的基地址 (0x90000000 for 槽A, 0x902B0000 for 槽B)
 */
uint32_t get_current_slot_base_address(void)
{
    // 使用链接器定义的Flash起始地址符号
    extern uint32_t _flash_start;  // 链接器脚本中定义的Flash起始地址
    uint32_t flash_start_address = (uint32_t)&_flash_start;
    
    // 双槽地址范围定义
    #define SLOT_A_BASE         0x90000000   // 槽A起始地址
    #define SLOT_A_END          0x902AFFFF   // 槽A结束地址  
    #define SLOT_B_BASE         0x902B0000   // 槽B起始地址
    #define SLOT_B_END          0x9055FFFF   // 槽B结束地址
    
    // 判断Flash起始地址在哪个槽范围内
    if (flash_start_address >= SLOT_A_BASE && flash_start_address <= SLOT_A_END) {
        // 当前固件在槽A
        APP_DBG("Current slot: A (Flash start: 0x%08X)", flash_start_address);
        return SLOT_A_BASE;
    } else if (flash_start_address >= SLOT_B_BASE && flash_start_address <= SLOT_B_END) {
        // 当前固件在槽B
        APP_DBG("Current slot: B (Flash start: 0x%08X)", flash_start_address);
        return SLOT_B_BASE;
    } else {
        // 未知地址，根据地址值智能判断
        if (flash_start_address == 0x90000000) {
            APP_DBG("Detected Slot A by exact match (0x%08X)", flash_start_address);
            return SLOT_A_BASE;
        } else if (flash_start_address == 0x902B0000) {
            APP_DBG("Detected Slot B by exact match (0x%08X)", flash_start_address);
            return SLOT_B_BASE;
        } else {
            // 如果都不匹配，根据地址大小判断更可能的槽
            if (flash_start_address < 0x90200000) {
                APP_DBG("Flash address 0x%08X < 0x90200000, assuming Slot A", flash_start_address);
                return SLOT_A_BASE;
            } else {
                APP_DBG("Flash address 0x%08X >= 0x90200000, assuming Slot B", flash_start_address);
                return SLOT_B_BASE;
            }
        }
    }
}
