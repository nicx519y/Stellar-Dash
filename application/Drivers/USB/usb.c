#include "usb.h"
#include "tusb.h"
#include "stm32h7xx_hal.h"


void USB_clock_init() {
    /**
     * USB Clock
     * 
     */
    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

    // 确保HSI48时钟开启 (USB需要48MHz时钟)
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48;
    RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }
    
    // 配置USB时钟源为HSI48
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USB;
    PeriphClkInitStruct.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

}

void USB_DEVICE_init() {

    GPIO_InitTypeDef GPIO_InitStruct;

    #if BOARD_TUD_RHPORT == 0
    // Despite being call USB2_OTG
    // OTG_FS is marked as RHPort0 by TinyUSB to be consistent across stm32 port
    // PA9 VUSB, PA10 ID, PA11 DM, PA12 DP

    // Configure DM DP Pins
    GPIO_InitStruct.Pin = GPIO_PIN_11 | GPIO_PIN_12;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Alternate = GPIO_AF10_OTG2_HS;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    // This for ID line debug
    // GPIO_InitStruct.Pin = GPIO_PIN_10;
    // GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
    // GPIO_InitStruct.Pull = GPIO_PULLUP;
    // GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    // GPIO_InitStruct.Alternate = GPIO_AF10_OTG2_HS;
    // HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    // https://community.st.com/s/question/0D50X00009XkYZLSA3/stm32h7-nucleo-usb-fs-cdc
    // TODO: Board init actually works fine without this line.
    HAL_PWREx_EnableUSBVoltageDetector();
    __HAL_RCC_USB2_OTG_FS_CLK_ENABLE();

    #if OTG_FS_VBUS_SENSE
    // Configure VBUS Pin
    GPIO_InitStruct.Pin = GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    // Enable VBUS sense (B device) via pin PA9
    USB_OTG_FS->GCCFG |= USB_OTG_GCCFG_VBDEN;
    #else
    // Disable VBUS sense (B device) via pin PA9
    USB_OTG_FS->GCCFG &= ~USB_OTG_GCCFG_VBDEN;

    // B-peripheral session valid override enable
    USB_OTG_FS->GOTGCTL |= USB_OTG_GOTGCTL_BVALOEN;
    USB_OTG_FS->GOTGCTL |= USB_OTG_GOTGCTL_BVALOVAL;
    #endif // vbus sense

    #elif BOARD_TUD_RHPORT == 1
    // Despite being call USB2_OTG
    // OTG_HS is marked as RHPort1 by TinyUSB to be consistent across stm32 port

    struct {
        GPIO_TypeDef* port;
        uint32_t pin;
    } const ulpi_pins[] =
    {
        ULPI_PINS
    };

    for (uint8_t i=0; i < sizeof(ulpi_pins)/sizeof(ulpi_pins[0]); i++)
    {
        GPIO_InitStruct.Pin       = ulpi_pins[i].pin;
        GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull      = GPIO_NOPULL;
        GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF10_OTG2_HS;
        HAL_GPIO_Init(ulpi_pins[i].port, &GPIO_InitStruct);
    }

    // Enable USB HS & ULPI Clocks
    __HAL_RCC_USB1_OTG_HS_ULPI_CLK_ENABLE();
    __HAL_RCC_USB1_OTG_HS_CLK_ENABLE();

    #if OTG_HS_VBUS_SENSE
    #error OTG HS VBUS Sense enabled is not implemented
    #else
    // No VBUS sense
    USB_OTG_HS->GCCFG &= ~USB_OTG_GCCFG_VBDEN;

    // B-peripheral session valid override enable
    USB_OTG_HS->GOTGCTL |= USB_OTG_GOTGCTL_BVALOEN;
    USB_OTG_HS->GOTGCTL |= USB_OTG_GOTGCTL_BVALOVAL;
    #endif

    // Force device mode
    USB_OTG_HS->GUSBCFG &= ~USB_OTG_GUSBCFG_FHMOD;
    USB_OTG_HS->GUSBCFG |= USB_OTG_GUSBCFG_FDMOD;

    HAL_PWREx_EnableUSBVoltageDetector();

    // For waveshare openh743 ULPI PHY reset walkaround
    board_stm32h7_post_init();
    #endif // rhport = 1
}



/* USB OTG HS clock enable */
#define __HAL_RCC_USB_OTG_HS_CLK_ENABLE()  do { \
                                                 __IO uint32_t tmpreg; \
                                                 SET_BIT(RCC->AHB1ENR, RCC_AHB1ENR_USB2OTGHSEN); \
                                                 /* Delay after an RCC peripheral clock enabling */ \
                                                 tmpreg = READ_BIT(RCC->AHB1ENR, RCC_AHB1ENR_USB2OTGHSEN); \
                                                 UNUSED(tmpreg); \
                                               } while(0U)

/* USB Host initialization function */
void USB_HOST_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  
  /* 使能 GPIO 时钟 */
  __HAL_RCC_GPIOB_CLK_ENABLE();
  
  /* 使能 USB OTG_HS 时钟 - 使用兼容的宏 */
  __HAL_RCC_USB_OTG_HS_CLK_ENABLE();  // 使用我们自定义的宏
  printf("USB OTG_HS clock enabled\r\n");
  
  /* STM32H7系列需要启用USB电压检测器 */
  HAL_PWREx_EnableUSBVoltageDetector();
  printf("USB Voltage Detector enabled\r\n");
  
  /* 配置 USB D- (PB14) 和 D+ (PB15) 引脚 */
  
  /* 配置 D- 引脚 (PB14) */
  GPIO_InitStruct.Pin = GPIO_PIN_14;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF12_OTG2_FS; /* STM32H7xx的USB OTG_HS (full-speed) */
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
  
  /* 配置 D+ 引脚 (PB15) */
  GPIO_InitStruct.Pin = GPIO_PIN_15;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF12_OTG2_FS; /* STM32H7xx的USB OTG_HS (full-speed) */
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
  
  /* 配置 ID 引脚 为 OTG_HS (可选) */
  /* 请注意：如果只使用 Host 模式，可以不配置 ID 引脚，但最好设置为输出并拉高 */
  GPIO_InitStruct.Pin = GPIO_PIN_12;  /* 如果你有 ID 引脚，通常是 PB12 */
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET); /* 拉高以强制 Host 模式 */
  
  /* 先复位USB外设再配置 */
  __HAL_RCC_USB_OTG_HS_FORCE_RESET();
  HAL_Delay(10);
  __HAL_RCC_USB_OTG_HS_RELEASE_RESET();
  HAL_Delay(10);
  
//   /* 复位 USB OTG HS 外设 */
//   __HAL_RCC_USB2_OTG_HS_FORCE_RESET();
//   HAL_Delay(10);
//   __HAL_RCC_USB2_OTG_HS_RELEASE_RESET();
//   HAL_Delay(10);
  
  // 启用USB主机模式
  USB_OTG_HS->GUSBCFG &= ~USB_OTG_GUSBCFG_FDMOD;  // 清除设备模式
  USB_OTG_HS->GUSBCFG |= USB_OTG_GUSBCFG_FHMOD;   // 设置主机模式
  
  // 启用VBUS驱动
  USB_OTG_HS->GCCFG &= ~USB_OTG_GCCFG_VBDEN;     // 禁用VBUS检测
  USB_OTG_HS->GOTGCTL |= USB_OTG_GOTGCTL_BVALOEN;
  USB_OTG_HS->GOTGCTL |= USB_OTG_GOTGCTL_BVALOVAL;
  
  // USB主机初始化已在TinyUSB库中完成
  // 我们需要在项目中添加必要的回调函数
  
  // 启用USB中断
  HAL_NVIC_SetPriority(OTG_HS_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(OTG_HS_IRQn);
  
  // 读取并打印PB14和PB15的状态
  GPIO_PinState pb14_state = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_14);
  GPIO_PinState pb15_state = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_15);
  printf("PB14(D-) state: %d, PB15(D+) state: %d\r\n", pb14_state, pb15_state);
  
  printf("USB Host initialization complete\r\n");
}


/* 
 * USB Host HID 设备挂载回调
 */
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len)
{
  (void)desc_report;
  (void)desc_len;
  
  uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);
  
  if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD)
  {
    // 键盘设备连接
    printf("HID Keyboard connected: dev_addr = %d, instance = %d\r\n", dev_addr, instance);
    tuh_hid_receive_report(dev_addr, instance);
  }
  else if (itf_protocol == HID_ITF_PROTOCOL_MOUSE)
  {
    // 鼠标设备连接
    printf("HID Mouse connected: dev_addr = %d, instance = %d\r\n", dev_addr, instance);
    tuh_hid_receive_report(dev_addr, instance);
  }
  else
  {
    // 其他HID设备连接
    printf("Generic HID device connected: dev_addr = %d, instance = %d\r\n", dev_addr, instance);
    tuh_hid_receive_report(dev_addr, instance);
  }
}

/*
 * USB Host HID 设备报告接收回调
 */
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len)
{
  uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);
  
  if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD)
  {
    // 处理键盘报告
    printf("HID Keyboard report received, len = %d\r\n", len);
    // 这里可以添加键盘报告处理代码
  }
  else if (itf_protocol == HID_ITF_PROTOCOL_MOUSE)
  {
    // 处理鼠标报告
    printf("HID Mouse report received, len = %d\r\n", len);
    // 这里可以添加鼠标报告处理代码
  }
  else
  {
    // 处理其他HID设备报告
    printf("Generic HID report received, len = %d\r\n", len);
    // 打印报告内容（调试用）
    printf("  Report data: ");
    for (uint16_t i = 0; i < len && i < 8; i++) {
      printf("%02X ", report[i]);
    }
    if (len > 8) printf("...");
    printf("\r\n");
  }
  
  // 继续接收报告
  tuh_hid_receive_report(dev_addr, instance);
}

/*
 * USB Host HID 设备卸载回调
 */
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
  uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);
  
  if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD)
  {
    // 键盘设备断开
    printf("HID Keyboard disconnected\r\n");
  }
  else if (itf_protocol == HID_ITF_PROTOCOL_MOUSE)
  {
    // 鼠标设备断开
    printf("HID Mouse disconnected\r\n");
  }
  else
  {
    // 其他HID设备断开
    printf("Generic HID device disconnected\r\n");
  }
}

void tuh_mount_cb(uint8_t dev_addr)
{
  printf("USB Device connected: address = %d\r\n", dev_addr);
}

void tuh_umount_cb(uint8_t dev_addr)
{
  printf("USB Device disconnected: address = %d\r\n", dev_addr);
}

/**
 * 检测并打印USB主机接口状态
 * 包括引脚状态和USB控制器状态寄存器
 */
void USB_HOST_StatusCheck(void)
{
  // 读取PB14和PB15引脚状态
  GPIO_PinState pb14_state = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_14);
  GPIO_PinState pb15_state = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_15);
  printf("PB14(D-) state: %d, PB15(D+) state: %d\r\n", pb14_state, pb15_state);
  
  // 确保Vbus供电
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_2, GPIO_PIN_SET);
  printf("VBUS Power enabled\r\n");
  
  // 检查是否有设备连接
  if (tuh_mounted(1)) {
    printf("USB device mounted on address 1\r\n");
  } else {
    printf("No USB device mounted\r\n");
  }
}
