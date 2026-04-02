#include "st7789.h"
#include <string.h>

#include "stm32h7xx_hal_gpio_ex.h"

#define ST7789_CMD_SWRESET 0x01u
#define ST7789_CMD_SLPOUT  0x11u
#define ST7789_CMD_NORON   0x13u
#define ST7789_CMD_INVOFF  0x20u
#define ST7789_CMD_INVON   0x21u
#define ST7789_CMD_DISPON  0x29u
#define ST7789_CMD_CASET   0x2Au
#define ST7789_CMD_RASET   0x2Bu
#define ST7789_CMD_RAMWR   0x2Cu
#define ST7789_CMD_MADCTL  0x36u
#define ST7789_CMD_COLMOD  0x3Au

#define ST7789_SPI_TIMEOUT_MS 1000u

static inline void st7789_gpio_write(GPIO_TypeDef* port, uint16_t pin, GPIO_PinState state)
{
    HAL_GPIO_WritePin(port, pin, state);
}

static inline void st7789_cs_low(void)  { st7789_gpio_write(ST7789_CS_PORT, ST7789_CS_PIN, GPIO_PIN_RESET); }
static inline void st7789_cs_high(void) { st7789_gpio_write(ST7789_CS_PORT, ST7789_CS_PIN, GPIO_PIN_SET); }
static inline void st7789_dc_cmd(void)  { st7789_gpio_write(ST7789_DC_PORT, ST7789_DC_PIN, GPIO_PIN_RESET); }
static inline void st7789_dc_data(void) { st7789_gpio_write(ST7789_DC_PORT, ST7789_DC_PIN, GPIO_PIN_SET); }
static inline void st7789_scl_low(void) { st7789_gpio_write(ST7789_SCL_PORT, ST7789_SCL_PIN, GPIO_PIN_RESET); }
static inline void st7789_scl_high(void){ st7789_gpio_write(ST7789_SCL_PORT, ST7789_SCL_PIN, GPIO_PIN_SET); }
static inline void st7789_sda_low(void) { st7789_gpio_write(ST7789_SDA_PORT, ST7789_SDA_PIN, GPIO_PIN_RESET); }
static inline void st7789_sda_high(void){ st7789_gpio_write(ST7789_SDA_PORT, ST7789_SDA_PIN, GPIO_PIN_SET); }

static inline void st7789_spi_delay(void)
{
    __NOP();
    __NOP();
    __NOP();
    __NOP();
}

static void st7789_bitbang_write_byte(uint8_t b)
{
    for (uint8_t i = 0; i < 8; i++)
    {
        if (b & 0x80u) st7789_sda_high(); else st7789_sda_low();
        st7789_spi_delay();
        st7789_scl_high();
        st7789_spi_delay();
        st7789_scl_low();
        b <<= 1;
    }
}

static void st7789_write_bytes(ST7789_Handle* lcd, const uint8_t* data, size_t len)
{
    if (len == 0) return;

    for (size_t i = 0; i < len; i++)
    {
        st7789_bitbang_write_byte(data[i]);
    }
}

static void st7789_write_cmd(ST7789_Handle* lcd, uint8_t cmd)
{
    st7789_cs_low();
    st7789_dc_cmd();
    st7789_write_bytes(lcd, &cmd, 1);
    st7789_cs_high();
}

static void st7789_write_cmd_data(ST7789_Handle* lcd, uint8_t cmd, const uint8_t* data, size_t len)
{
    st7789_cs_low();
    st7789_dc_cmd();
    st7789_write_bytes(lcd, &cmd, 1);
    if (len > 0)
    {
        st7789_dc_data();
        st7789_write_bytes(lcd, data, len);
    }
    st7789_cs_high();
}

static uint16_t st7789_width(const ST7789_Handle* lcd)
{
    return lcd ? lcd->cfg.width : ST7789_WIDTH;
}

static uint16_t st7789_height(const ST7789_Handle* lcd)
{
    return lcd ? lcd->cfg.height : ST7789_HEIGHT;
}

static void st7789_set_address_window(ST7789_Handle* lcd, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint16_t xs = (uint16_t)(x0 + lcd->cfg.x_offset);
    uint16_t xe = (uint16_t)(x1 + lcd->cfg.x_offset);
    uint16_t ys = (uint16_t)(y0 + lcd->cfg.y_offset);
    uint16_t ye = (uint16_t)(y1 + lcd->cfg.y_offset);

    uint8_t caset[4] = {
        (uint8_t)(xs >> 8), (uint8_t)(xs & 0xFFu),
        (uint8_t)(xe >> 8), (uint8_t)(xe & 0xFFu)
    };

    uint8_t raset[4] = {
        (uint8_t)(ys >> 8), (uint8_t)(ys & 0xFFu),
        (uint8_t)(ye >> 8), (uint8_t)(ye & 0xFFu)
    };

    st7789_write_cmd_data(lcd, ST7789_CMD_CASET, caset, sizeof(caset));
    st7789_write_cmd_data(lcd, ST7789_CMD_RASET, raset, sizeof(raset));

    st7789_cs_low();
    st7789_dc_cmd();
    {
        uint8_t cmd = ST7789_CMD_RAMWR;
        st7789_write_bytes(lcd, &cmd, 1);
    }
    st7789_dc_data();
}

static void st7789_end_pixels(void)
{
    st7789_cs_high();
}

static inline uint16_t st7789_rgb888_to_565(uint32_t rgb888)
{
    uint8_t r = (uint8_t)((rgb888 >> 16) & 0xFFu);
    uint8_t g = (uint8_t)((rgb888 >> 8) & 0xFFu);
    uint8_t b = (uint8_t)(rgb888 & 0xFFu);

    return (uint16_t)(((uint16_t)(r & 0xF8u) << 8) | ((uint16_t)(g & 0xFCu) << 3) | (uint16_t)(b >> 3));
}

static uint16_t st7789_fb0[ST7789_WIDTH * ST7789_HEIGHT] __attribute__((section(".DMA_Section"), aligned(32)));
static uint16_t st7789_fb1[ST7789_WIDTH * ST7789_HEIGHT] __attribute__((section(".DMA_Section"), aligned(32)));

static inline uint32_t st7789_fb_index(uint16_t x, uint16_t y)
{
    return (uint32_t)y * (uint32_t)ST7789_WIDTH + (uint32_t)x;
}

static void st7789_fb_clear(ST7789_Handle* lcd, uint32_t rgb888)
{
    if (!lcd || !lcd->framebuffer_enabled || !lcd->fb_back) return;
    uint16_t c = st7789_rgb888_to_565(rgb888);
    uint32_t pixels = (uint32_t)ST7789_WIDTH * (uint32_t)ST7789_HEIGHT;
    for (uint32_t i = 0; i < pixels; i++) {
        lcd->fb_back[i] = c;
    }
}

static void st7789_fb_fill_rect(ST7789_Handle* lcd, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t rgb888)
{
    if (!lcd || !lcd->framebuffer_enabled || !lcd->fb_back) return;
    if (w == 0 || h == 0) return;
    if (x >= ST7789_WIDTH || y >= ST7789_HEIGHT) return;

    uint16_t x1 = (uint16_t)(x + w - 1u);
    uint16_t y1 = (uint16_t)(y + h - 1u);
    if (x1 >= ST7789_WIDTH) x1 = (uint16_t)(ST7789_WIDTH - 1u);
    if (y1 >= ST7789_HEIGHT) y1 = (uint16_t)(ST7789_HEIGHT - 1u);

    uint16_t c = st7789_rgb888_to_565(rgb888);
    for (uint16_t yy = y; yy <= y1; yy++) {
        uint32_t row = (uint32_t)yy * (uint32_t)ST7789_WIDTH;
        for (uint16_t xx = x; xx <= x1; xx++) {
            lcd->fb_back[row + xx] = c;
        }
    }
}

static void st7789_fb_put_pixel(ST7789_Handle* lcd, uint16_t x, uint16_t y, uint32_t rgb888)
{
    if (!lcd || !lcd->framebuffer_enabled || !lcd->fb_back) return;
    if (x >= ST7789_WIDTH || y >= ST7789_HEIGHT) return;
    lcd->fb_back[st7789_fb_index(x, y)] = st7789_rgb888_to_565(rgb888);
}

static void st7789_flush_full_rgb565_be(ST7789_Handle* lcd, const uint16_t* buf)
{
    if (!lcd || !lcd->inited || !buf) return;
    st7789_set_address_window(lcd, 0, 0, (uint16_t)(ST7789_WIDTH - 1u), (uint16_t)(ST7789_HEIGHT - 1u));

    uint8_t out[256];
    uint32_t pixels_total = (uint32_t)ST7789_WIDTH * (uint32_t)ST7789_HEIGHT;
    uint32_t idx = 0;
    while (idx < pixels_total) {
        uint32_t chunkPixels = (pixels_total - idx);
        if (chunkPixels > (sizeof(out) / 2u)) chunkPixels = (sizeof(out) / 2u);
        for (uint32_t i = 0; i < chunkPixels; i++) {
            uint16_t c = buf[idx + i];
            out[i * 2u] = (uint8_t)(c >> 8);
            out[i * 2u + 1u] = (uint8_t)(c & 0xFFu);
        }
        st7789_write_bytes(lcd, out, chunkPixels * 2u);
        idx += chunkPixels;
    }
    st7789_end_pixels();
}

static void st7789_write_color_repeat(ST7789_Handle* lcd, uint32_t rgb888, uint32_t pixel_count)
{
    if (pixel_count == 0) return;

    if (lcd->cfg.color_mode == ST7789_COLOR_MODE_RGB565)
    {
        uint16_t c = st7789_rgb888_to_565(rgb888);
        uint8_t hi = (uint8_t)(c >> 8);
        uint8_t lo = (uint8_t)(c & 0xFFu);
        uint8_t buf[128];

        for (size_t i = 0; i < sizeof(buf); i += 2)
        {
            buf[i] = hi;
            buf[i + 1] = lo;
        }

        uint32_t bytes_total = pixel_count * 2u;
        while (bytes_total)
        {
            uint32_t chunk = bytes_total;
            if (chunk > sizeof(buf)) chunk = sizeof(buf);
            st7789_write_bytes(lcd, buf, chunk);
            bytes_total -= chunk;
        }
        return;
    }

    uint8_t r = (uint8_t)((rgb888 >> 16) & 0xFFu);
    uint8_t g = (uint8_t)((rgb888 >> 8) & 0xFFu);
    uint8_t b = (uint8_t)(rgb888 & 0xFFu);
    uint8_t r6 = (uint8_t)(r & 0xFCu);
    uint8_t g6 = (uint8_t)(g & 0xFCu);
    uint8_t b6 = (uint8_t)(b & 0xFCu);

    uint8_t buf[192];
    for (size_t i = 0; i < sizeof(buf); i += 3)
    {
        buf[i] = r6;
        buf[i + 1] = g6;
        buf[i + 2] = b6;
    }

    uint32_t bytes_total = pixel_count * 3u;
    while (bytes_total)
    {
        uint32_t chunk = bytes_total;
        if (chunk > sizeof(buf)) chunk = sizeof(buf);
        st7789_write_bytes(lcd, buf, chunk);
        bytes_total -= chunk;
    }
}

static void st7789_gpio_init(void)
{
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOJ_CLK_ENABLE();
    __HAL_RCC_GPIOK_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

    GPIO_InitStruct.Pin = ST7789_CS_PIN;
    HAL_GPIO_Init(ST7789_CS_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = ST7789_DC_PIN;
    HAL_GPIO_Init(ST7789_DC_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = ST7789_SCL_PIN;
    HAL_GPIO_Init(ST7789_SCL_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = ST7789_SDA_PIN;
    HAL_GPIO_Init(ST7789_SDA_PORT, &GPIO_InitStruct);

    st7789_cs_high();
    st7789_dc_data();
    st7789_scl_low();
    st7789_sda_high();
}

static void st7789_backlight_gpio_init_off(void)
{
    __HAL_RCC_GPIOH_CLK_ENABLE();
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Pin = ST7789_BL_PIN;
    HAL_GPIO_Init(ST7789_BL_PORT, &GPIO_InitStruct);
    st7789_gpio_write(ST7789_BL_PORT, ST7789_BL_PIN, GPIO_PIN_RESET);
}

static TIM_HandleTypeDef st7789_bl_htim12;
static bool st7789_bl_tim12_inited = false;

static void st7789_backlight_tim12_init(void)
{
    if (st7789_bl_tim12_inited) return;

    __HAL_RCC_TIM12_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = ST7789_BL_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF2_TIM12;
    HAL_GPIO_Init(ST7789_BL_PORT, &GPIO_InitStruct);

    uint32_t timclk = HAL_RCC_GetPCLK1Freq() * 2u;

    const uint32_t targetHz = 20000u;
    const uint32_t period = 999u;
    uint64_t presc64 = (uint64_t)timclk / ((uint64_t)targetHz * (uint64_t)(period + 1u));
    if (presc64 == 0) presc64 = 1;
    if (presc64 > 65536ull) presc64 = 65536ull;
    uint32_t prescaler = (uint32_t)(presc64 - 1ull);

    memset(&st7789_bl_htim12, 0, sizeof(st7789_bl_htim12));
    st7789_bl_htim12.Instance = TIM12;
    st7789_bl_htim12.Init.Prescaler = prescaler;
    st7789_bl_htim12.Init.CounterMode = TIM_COUNTERMODE_UP;
    st7789_bl_htim12.Init.Period = period;
    st7789_bl_htim12.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    st7789_bl_htim12.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    (void)HAL_TIM_PWM_Init(&st7789_bl_htim12);

    TIM_OC_InitTypeDef sConfigOC = {0};
    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 0;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    (void)HAL_TIM_PWM_ConfigChannel(&st7789_bl_htim12, &sConfigOC, TIM_CHANNEL_1);
    (void)HAL_TIM_PWM_Start(&st7789_bl_htim12, TIM_CHANNEL_1);

    st7789_bl_tim12_inited = true;
}

static uint8_t st7789_madctl_for_rotation(ST7789_Rotation rot)
{
    uint8_t madctl = 0x00u;

    switch (rot)
    {
        case ST7789_ROTATION_0: madctl = 0x00u; break;
        case ST7789_ROTATION_90: madctl = 0x60u; break;
        case ST7789_ROTATION_180: madctl = 0xC0u; break;
        case ST7789_ROTATION_270: madctl = 0xA0u; break;
        default: madctl = 0x00u; break;
    }

    madctl |= 0x00u;
    return madctl;
}

static void st7789_apply_rotation(ST7789_Handle* lcd)
{
    uint8_t madctl = st7789_madctl_for_rotation(lcd->cfg.rotation);
    st7789_write_cmd_data(lcd, ST7789_CMD_MADCTL, &madctl, 1);
    lcd->cfg.width = ST7789_WIDTH;
    lcd->cfg.height = ST7789_HEIGHT;
}

static void st7789_apply_color_mode(ST7789_Handle* lcd)
{
    uint8_t colmod = (lcd->cfg.color_mode == ST7789_COLOR_MODE_RGB565) ? 0x55u : 0x66u;
    st7789_write_cmd_data(lcd, ST7789_CMD_COLMOD, &colmod, 1);
    HAL_Delay(10);
}

static void st7789_apply_inversion(ST7789_Handle* lcd)
{
    st7789_write_cmd(lcd, lcd->cfg.invert ? ST7789_CMD_INVON : ST7789_CMD_INVOFF);
    HAL_Delay(10);
}

uint32_t ST7789_RGB(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

bool ST7789_IsInited(const ST7789_Handle* lcd)
{
    return lcd && lcd->inited;
}

bool ST7789_IsFrameBlocked(const ST7789_Handle* lcd)
{
    return lcd ? lcd->frame_blocked : true;
}

bool ST7789_FrameBegin(ST7789_Handle* lcd)
{
    if (!lcd || !lcd->inited) return false;
    if (lcd->cfg.fps == 0) {
        lcd->frame_blocked = false;
        return true;
    }

    uint32_t intervalMs = 1000u / (uint32_t)lcd->cfg.fps;
    if (intervalMs == 0) intervalMs = 1;
    uint32_t nowMs = HAL_GetTick();
    if (lcd->last_frame_ms != 0 && (uint32_t)(nowMs - lcd->last_frame_ms) < intervalMs) {
        lcd->frame_blocked = true;
        return false;
    }
    lcd->last_frame_ms = nowMs;
    lcd->frame_blocked = false;
    return true;
}

void ST7789_FrameEnd(ST7789_Handle* lcd)
{
    if (!lcd || !lcd->inited) return;
    if (lcd->frame_blocked) return;
    if (!lcd->framebuffer_enabled) return;
    if (!lcd->fb_back) return;
    st7789_flush_full_rgb565_be(lcd, lcd->fb_back);
    uint16_t* tmp = lcd->fb_front;
    lcd->fb_front = lcd->fb_back;
    lcd->fb_back = tmp;
}

void ST7789_AttachBacklightPWM(ST7789_Handle* lcd, TIM_HandleTypeDef* htim, uint32_t channel)
{
    if (!lcd) return;
    lcd->cfg.bl_htim = htim;
    lcd->cfg.bl_tim_channel = channel;
}

void ST7789_SetOffset(ST7789_Handle* lcd, uint16_t x_offset, uint16_t y_offset)
{
    if (!lcd) return;
    lcd->cfg.x_offset = x_offset;
    lcd->cfg.y_offset = y_offset;
}

void ST7789_SetRotation(ST7789_Handle* lcd, ST7789_Rotation rotation)
{
    if (!lcd) return;
    lcd->cfg.rotation = rotation;
    if (lcd->inited) st7789_apply_rotation(lcd);
}

void ST7789_SetBacklight(ST7789_Handle* lcd, uint8_t percent)
{
    if (!lcd) return;
    if (percent > 100) percent = 100;
    uint8_t hwPercent = (uint8_t)(100u - percent);

    if (lcd->cfg.bl_htim)
    {
        uint32_t arr = __HAL_TIM_GET_AUTORELOAD(lcd->cfg.bl_htim);
        uint32_t ccr = (arr * (uint32_t)hwPercent) / 100u;
        __HAL_TIM_SET_COMPARE(lcd->cfg.bl_htim, lcd->cfg.bl_tim_channel, ccr);
        (void)HAL_TIM_PWM_Start(lcd->cfg.bl_htim, lcd->cfg.bl_tim_channel);
        return;
    }

    st7789_gpio_write(ST7789_BL_PORT, ST7789_BL_PIN, (percent == 0) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void ST7789_Init(ST7789_Handle* lcd, const ST7789_Config* cfg)
{
    if (!lcd) return;

    ST7789_Config c = {0};
    c.width = ST7789_WIDTH;
    c.height = ST7789_HEIGHT;
    c.x_offset = 0;
    c.y_offset = 0;
    c.color_mode = ST7789_COLOR_MODE_RGB666;
    c.rotation = ST7789_ROTATION_0;
    c.invert = true;
    c.fps = ST7789_DEFAULT_FPS;
    c.use_framebuffer = false;
    c.bl_htim = NULL;
    c.bl_tim_channel = 0;

    if (cfg) c = *cfg;
    lcd->cfg = c;
    lcd->inited = false;
    lcd->frame_blocked = false;
    lcd->last_frame_ms = 0;
    lcd->framebuffer_enabled = lcd->cfg.use_framebuffer;
    lcd->fb_front = NULL;
    lcd->fb_back = NULL;

    if (lcd->framebuffer_enabled)
    {
        lcd->cfg.color_mode = ST7789_COLOR_MODE_RGB565;
        lcd->fb_front = st7789_fb0;
        lcd->fb_back = st7789_fb1;
        memset(st7789_fb0, 0, sizeof(st7789_fb0));
        memset(st7789_fb1, 0, sizeof(st7789_fb1));
    }

    st7789_gpio_init();
    if (lcd->cfg.bl_htim == NULL)
    {
        st7789_backlight_tim12_init();
        lcd->cfg.bl_htim = &st7789_bl_htim12;
        lcd->cfg.bl_tim_channel = TIM_CHANNEL_1;
    }
    else
    {
        st7789_backlight_gpio_init_off();
    }

    st7789_write_cmd(lcd, ST7789_CMD_SWRESET);
    HAL_Delay(150);
    st7789_write_cmd(lcd, ST7789_CMD_SLPOUT);
    HAL_Delay(120);

    st7789_apply_color_mode(lcd);
    st7789_apply_rotation(lcd);
    st7789_apply_inversion(lcd);

    st7789_write_cmd(lcd, ST7789_CMD_NORON);
    HAL_Delay(10);
    st7789_write_cmd(lcd, ST7789_CMD_DISPON);
    HAL_Delay(120);

    lcd->inited = true;
    ST7789_SetBacklight(lcd, 100);
    ST7789_FillScreen(lcd, 0x000000u);
}

void ST7789_FillScreen(ST7789_Handle* lcd, uint32_t rgb888)
{
    if (!lcd || !lcd->inited) return;
    if (lcd->frame_blocked) return;
    if (lcd->framebuffer_enabled)
    {
        st7789_fb_clear(lcd, rgb888);
        return;
    }
    ST7789_FillRect(lcd, 0, 0, st7789_width(lcd), st7789_height(lcd), rgb888);
}

void ST7789_FillRect(ST7789_Handle* lcd, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t rgb888)
{
    if (!lcd || !lcd->inited) return;
    if (lcd->frame_blocked) return;
    if (lcd->framebuffer_enabled)
    {
        st7789_fb_fill_rect(lcd, x, y, w, h, rgb888);
        return;
    }
    if (w == 0 || h == 0) return;
    if (x >= st7789_width(lcd) || y >= st7789_height(lcd)) return;

    uint16_t x1 = (uint16_t)(x + w - 1u);
    uint16_t y1 = (uint16_t)(y + h - 1u);

    if (x1 >= st7789_width(lcd)) x1 = (uint16_t)(st7789_width(lcd) - 1u);
    if (y1 >= st7789_height(lcd)) y1 = (uint16_t)(st7789_height(lcd) - 1u);

    st7789_set_address_window(lcd, x, y, x1, y1);
    uint32_t pixels = (uint32_t)(x1 - x + 1u) * (uint32_t)(y1 - y + 1u);
    st7789_write_color_repeat(lcd, rgb888, pixels);
    st7789_end_pixels();
}

void ST7789_DrawPixel(ST7789_Handle* lcd, uint16_t x, uint16_t y, uint32_t rgb888)
{
    if (!lcd || !lcd->inited) return;
    if (lcd->frame_blocked) return;
    if (lcd->framebuffer_enabled)
    {
        st7789_fb_put_pixel(lcd, x, y, rgb888);
        return;
    }
    if (x >= st7789_width(lcd) || y >= st7789_height(lcd)) return;

    st7789_set_address_window(lcd, x, y, x, y);
    st7789_write_color_repeat(lcd, rgb888, 1);
    st7789_end_pixels();
}

static int st7789_abs_i(int v) { return (v < 0) ? -v : v; }

void ST7789_DrawLine(ST7789_Handle* lcd, int x0, int y0, int x1, int y1, uint32_t rgb888)
{
    if (!lcd || !lcd->inited) return;
    if (lcd->frame_blocked) return;

    int dx = st7789_abs_i(x1 - x0);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = -st7789_abs_i(y1 - y0);
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    while (1)
    {
        if (x0 >= 0 && y0 >= 0 && (uint16_t)x0 < st7789_width(lcd) && (uint16_t)y0 < st7789_height(lcd))
        {
            ST7789_DrawPixel(lcd, (uint16_t)x0, (uint16_t)y0, rgb888);
        }
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void ST7789_DrawRect(ST7789_Handle* lcd, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t rgb888)
{
    if (!lcd || !lcd->inited) return;
    if (lcd->frame_blocked) return;
    if (w == 0 || h == 0) return;

    ST7789_DrawLine(lcd, x, y, (int)(x + w - 1u), y, rgb888);
    ST7789_DrawLine(lcd, x, (int)(y + h - 1u), (int)(x + w - 1u), (int)(y + h - 1u), rgb888);
    ST7789_DrawLine(lcd, x, y, x, (int)(y + h - 1u), rgb888);
    ST7789_DrawLine(lcd, (int)(x + w - 1u), y, (int)(x + w - 1u), (int)(y + h - 1u), rgb888);
}

void ST7789_DrawCircle(ST7789_Handle* lcd, int x0, int y0, int r, uint32_t rgb888)
{
    if (!lcd || !lcd->inited) return;
    if (lcd->frame_blocked) return;
    if (r <= 0) return;

    int x = r;
    int y = 0;
    int err = 1 - x;

    while (x >= y)
    {
        ST7789_DrawPixel(lcd, (uint16_t)(x0 + x), (uint16_t)(y0 + y), rgb888);
        ST7789_DrawPixel(lcd, (uint16_t)(x0 + y), (uint16_t)(y0 + x), rgb888);
        ST7789_DrawPixel(lcd, (uint16_t)(x0 - y), (uint16_t)(y0 + x), rgb888);
        ST7789_DrawPixel(lcd, (uint16_t)(x0 - x), (uint16_t)(y0 + y), rgb888);
        ST7789_DrawPixel(lcd, (uint16_t)(x0 - x), (uint16_t)(y0 - y), rgb888);
        ST7789_DrawPixel(lcd, (uint16_t)(x0 - y), (uint16_t)(y0 - x), rgb888);
        ST7789_DrawPixel(lcd, (uint16_t)(x0 + y), (uint16_t)(y0 - x), rgb888);
        ST7789_DrawPixel(lcd, (uint16_t)(x0 + x), (uint16_t)(y0 - y), rgb888);

        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}

void ST7789_FillCircle(ST7789_Handle* lcd, int x0, int y0, int r, uint32_t rgb888)
{
    if (!lcd || !lcd->inited) return;
    if (lcd->frame_blocked) return;
    if (r <= 0) return;

    int x = r;
    int y = 0;
    int err = 1 - x;

    while (x >= y)
    {
        ST7789_DrawLine(lcd, x0 - x, y0 + y, x0 + x, y0 + y, rgb888);
        ST7789_DrawLine(lcd, x0 - x, y0 - y, x0 + x, y0 - y, rgb888);
        ST7789_DrawLine(lcd, x0 - y, y0 + x, x0 + y, y0 + x, rgb888);
        ST7789_DrawLine(lcd, x0 - y, y0 - x, x0 + y, y0 - x, rgb888);

        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}

static const uint8_t st7789_font5x7[95][5] = {
    {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},
    {0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},
    {0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},{0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
    {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
    {0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},{0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
    {0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},
    {0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},
    {0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
    {0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
    {0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
    {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},
    {0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},{0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},
    {0x38,0x44,0x44,0x48,0x7F},{0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x0C,0x52,0x52,0x52,0x3E},
    {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x44,0x3D,0x00},{0x7F,0x10,0x28,0x44,0x00},
    {0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},{0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},
    {0x7C,0x14,0x14,0x14,0x08},{0x08,0x14,0x14,0x18,0x7C},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
    {0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},{0x3C,0x40,0x30,0x40,0x3C},
    {0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},{0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},
    {0x00,0x00,0x7F,0x00,0x00},{0x00,0x41,0x36,0x08,0x00},{0x10,0x08,0x08,0x10,0x08}
};

static uint16_t st7789_char_cell_w(uint8_t scale)
{
    if (scale == 0) scale = 1;
    if (scale == 3) return 9;
    return (uint16_t)(6u * scale);
}

static uint16_t st7789_char_cell_h(uint8_t scale)
{
    if (scale == 0) scale = 1;
    if (scale == 3) return 12;
    return (uint16_t)(8u * scale);
}

void ST7789_DrawChar(ST7789_Handle* lcd, uint16_t x, uint16_t y, char c, uint32_t fg_rgb888, uint32_t bg_rgb888, uint8_t scale)
{
    if (!lcd || !lcd->inited) return;
    if (lcd->frame_blocked) return;
    if (scale == 0) scale = 1;

    if (c < 32 || c > 126) c = '?';
    const uint8_t* glyph = st7789_font5x7[(uint8_t)(c - 32)];

    uint16_t w = st7789_char_cell_w(scale);
    uint16_t h = st7789_char_cell_h(scale);

    ST7789_FillRect(lcd, x, y, w, h, bg_rgb888);

    if (scale == 3)
    {
        for (uint8_t dy = 0; dy < 12u; dy++)
        {
            uint8_t sy = (uint8_t)(((uint16_t)dy * 2u) / 3u);
            if (sy >= 7u) continue;
            for (uint8_t dx = 0; dx < 9u; dx++)
            {
                uint8_t sx = (uint8_t)(((uint16_t)dx * 2u) / 3u);
                if (sx >= 5u) continue;
                if (glyph[sx] & (uint8_t)(1u << sy))
                {
                    ST7789_DrawPixel(lcd, (uint16_t)(x + dx), (uint16_t)(y + dy), fg_rgb888);
                }
            }
        }
        return;
    }

    for (uint8_t col = 0; col < 5; col++)
    {
        uint8_t line = glyph[col];
        for (uint8_t row = 0; row < 7; row++)
        {
            if (line & 0x01u)
            {
                ST7789_FillRect(lcd, (uint16_t)(x + col * scale), (uint16_t)(y + row * scale), scale, scale, fg_rgb888);
            }
            line >>= 1;
        }
    }
}

void ST7789_DrawString(ST7789_Handle* lcd, uint16_t x, uint16_t y, const char* s, uint32_t fg_rgb888, uint32_t bg_rgb888, uint8_t scale)
{
    if (!lcd || !lcd->inited || !s) return;
    if (lcd->frame_blocked) return;
    if (scale == 0) scale = 1;

    uint16_t cx = x;
    uint16_t cy = y;

    uint16_t step_x = st7789_char_cell_w(scale);
    uint16_t step_y = st7789_char_cell_h(scale);

    while (*s)
    {
        char c = *s++;
        if (c == '\n')
        {
            cx = x;
            cy = (uint16_t)(cy + step_y);
            continue;
        }
        if (c == '\r') continue;

        if (cx + step_x > st7789_width(lcd))
        {
            cx = x;
            cy = (uint16_t)(cy + step_y);
        }
        if (cy + step_y > st7789_height(lcd))
        {
            break;
        }

        ST7789_DrawChar(lcd, cx, cy, c, fg_rgb888, bg_rgb888, scale);
        cx = (uint16_t)(cx + step_x);
    }
}

static uint16_t st7789_min_u16(uint16_t a, uint16_t b)
{
    return (a < b) ? a : b;
}

void ST7789_DrawBitmap(ST7789_Handle* lcd, uint16_t x, uint16_t y, uint16_t w, uint16_t h, const void* pixels, ST7789_BitmapFormat format, uint32_t stride_bytes)
{
    if (!lcd || !lcd->inited || !pixels) return;
    if (lcd->frame_blocked) return;
    if (w == 0 || h == 0) return;
    if (x >= st7789_width(lcd) || y >= st7789_height(lcd)) return;

    uint16_t draw_w = st7789_min_u16(w, (uint16_t)(st7789_width(lcd) - x));
    uint16_t draw_h = st7789_min_u16(h, (uint16_t)(st7789_height(lcd) - y));

    if (stride_bytes == 0)
    {
        switch (format)
        {
            case ST7789_BITMAP_RGB565_BE:
            case ST7789_BITMAP_RGB565_LE:
                stride_bytes = (uint32_t)w * 2u;
                break;
            case ST7789_BITMAP_RGB888:
                stride_bytes = (uint32_t)w * 3u;
                break;
            case ST7789_BITMAP_ARGB8888:
                stride_bytes = (uint32_t)w * 4u;
                break;
            default:
                return;
        }
    }

    if (lcd->framebuffer_enabled)
    {
        const uint8_t* src = (const uint8_t*)pixels;
        for (uint16_t row = 0; row < draw_h; row++)
        {
            const uint8_t* row_ptr = src + (size_t)row * (size_t)stride_bytes;
            for (uint16_t col = 0; col < draw_w; col++)
            {
                uint16_t c565 = 0;
                uint32_t rgb = 0;
                switch (format)
                {
                    case ST7789_BITMAP_RGB565_BE:
                        c565 = (uint16_t)(((uint16_t)row_ptr[col * 2u] << 8) | (uint16_t)row_ptr[col * 2u + 1u]);
                        break;
                    case ST7789_BITMAP_RGB565_LE:
                        c565 = (uint16_t)(((uint16_t)row_ptr[col * 2u + 1u] << 8) | (uint16_t)row_ptr[col * 2u]);
                        break;
                    case ST7789_BITMAP_RGB888:
                        rgb = ((uint32_t)row_ptr[col * 3u] << 16) | ((uint32_t)row_ptr[col * 3u + 1u] << 8) | (uint32_t)row_ptr[col * 3u + 2u];
                        c565 = st7789_rgb888_to_565(rgb);
                        break;
                    case ST7789_BITMAP_ARGB8888:
                        rgb = ((uint32_t)row_ptr[col * 4u + 1u] << 16) | ((uint32_t)row_ptr[col * 4u + 2u] << 8) | (uint32_t)row_ptr[col * 4u + 3u];
                        c565 = st7789_rgb888_to_565(rgb);
                        break;
                    default:
                        return;
                }
                uint16_t px = (uint16_t)(x + col);
                uint16_t py = (uint16_t)(y + row);
                if (px < ST7789_WIDTH && py < ST7789_HEIGHT)
                {
                    lcd->fb_back[st7789_fb_index(px, py)] = c565;
                }
            }
        }
        return;
    }

    st7789_set_address_window(lcd, x, y, (uint16_t)(x + draw_w - 1u), (uint16_t)(y + draw_h - 1u));

    const uint8_t* src = (const uint8_t*)pixels;
    uint8_t out[192];
    size_t out_len = 0;

    for (uint16_t row = 0; row < draw_h; row++)
    {
        const uint8_t* row_ptr = src + (size_t)row * (size_t)stride_bytes;

        for (uint16_t col = 0; col < draw_w; col++)
        {
            uint8_t r = 0, g = 0, b = 0;
            uint16_t c565 = 0;

            switch (format)
            {
                case ST7789_BITMAP_RGB565_BE:
                {
                    const uint8_t* p = row_ptr + (size_t)col * 2u;
                    c565 = (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
                    break;
                }
                case ST7789_BITMAP_RGB565_LE:
                {
                    const uint8_t* p = row_ptr + (size_t)col * 2u;
                    c565 = (uint16_t)(((uint16_t)p[1] << 8) | (uint16_t)p[0]);
                    break;
                }
                case ST7789_BITMAP_RGB888:
                {
                    const uint8_t* p = row_ptr + (size_t)col * 3u;
                    r = p[0];
                    g = p[1];
                    b = p[2];
                    c565 = st7789_rgb888_to_565(((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b);
                    break;
                }
                case ST7789_BITMAP_ARGB8888:
                {
                    const uint8_t* p = row_ptr + (size_t)col * 4u;
                    r = p[1];
                    g = p[2];
                    b = p[3];
                    c565 = st7789_rgb888_to_565(((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b);
                    break;
                }
                default:
                    st7789_end_pixels();
                    return;
            }

            if (format == ST7789_BITMAP_RGB565_BE || format == ST7789_BITMAP_RGB565_LE)
            {
                uint8_t r5 = (uint8_t)((c565 >> 11) & 0x1Fu);
                uint8_t g6 = (uint8_t)((c565 >> 5) & 0x3Fu);
                uint8_t b5 = (uint8_t)(c565 & 0x1Fu);
                r = (uint8_t)((r5 << 3) | (r5 >> 2));
                g = (uint8_t)((g6 << 2) | (g6 >> 4));
                b = (uint8_t)((b5 << 3) | (b5 >> 2));
            }

            if (lcd->cfg.color_mode == ST7789_COLOR_MODE_RGB565)
            {
                if (out_len + 2 > sizeof(out))
                {
                    st7789_write_bytes(lcd, out, out_len);
                    out_len = 0;
                }
                out[out_len++] = (uint8_t)(c565 >> 8);
                out[out_len++] = (uint8_t)(c565 & 0xFFu);
            }
            else
            {
                if (out_len + 3 > sizeof(out))
                {
                    st7789_write_bytes(lcd, out, out_len);
                    out_len = 0;
                }
                out[out_len++] = (uint8_t)(r & 0xFCu);
                out[out_len++] = (uint8_t)(g & 0xFCu);
                out[out_len++] = (uint8_t)(b & 0xFCu);
            }
        }
    }

    if (out_len)
    {
        st7789_write_bytes(lcd, out, out_len);
    }
    st7789_end_pixels();
}

void ST7789_DrawBitmap1BPP(ST7789_Handle* lcd, uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t* bits, uint32_t stride_bytes, uint32_t fg_rgb888, uint32_t bg_rgb888)
{
    if (!lcd || !lcd->inited || !bits) return;
    if (lcd->frame_blocked) return;
    if (w == 0 || h == 0) return;
    if (x >= st7789_width(lcd) || y >= st7789_height(lcd)) return;

    uint16_t draw_w = st7789_min_u16(w, (uint16_t)(st7789_width(lcd) - x));
    uint16_t draw_h = st7789_min_u16(h, (uint16_t)(st7789_height(lcd) - y));

    if (stride_bytes == 0)
    {
        stride_bytes = ((uint32_t)w + 7u) / 8u;
    }

    if (lcd->framebuffer_enabled)
    {
        uint16_t fg565 = st7789_rgb888_to_565(fg_rgb888);
        uint16_t bg565 = st7789_rgb888_to_565(bg_rgb888);
        for (uint16_t row = 0; row < draw_h; row++)
        {
            const uint8_t* row_ptr = bits + (size_t)row * (size_t)stride_bytes;
            for (uint16_t col = 0; col < draw_w; col++)
            {
                uint8_t byte = row_ptr[col >> 3];
                uint8_t bit = (uint8_t)((byte >> (7u - (col & 7u))) & 0x01u);
                uint16_t c = bit ? fg565 : bg565;
                uint16_t px = (uint16_t)(x + col);
                uint16_t py = (uint16_t)(y + row);
                if (px < ST7789_WIDTH && py < ST7789_HEIGHT)
                {
                    lcd->fb_back[st7789_fb_index(px, py)] = c;
                }
            }
        }
        return;
    }

    st7789_set_address_window(lcd, x, y, (uint16_t)(x + draw_w - 1u), (uint16_t)(y + draw_h - 1u));

    uint16_t fg565 = st7789_rgb888_to_565(fg_rgb888);
    uint16_t bg565 = st7789_rgb888_to_565(bg_rgb888);
    uint8_t out[192];
    size_t out_len = 0;

    for (uint16_t row = 0; row < draw_h; row++)
    {
        const uint8_t* row_ptr = bits + (size_t)row * (size_t)stride_bytes;
        for (uint16_t col = 0; col < draw_w; col++)
        {
            uint8_t byte = row_ptr[col >> 3];
            uint8_t bit = (uint8_t)((byte >> (7u - (col & 7u))) & 0x01u);
            uint16_t c = bit ? fg565 : bg565;
            uint8_t r5 = (uint8_t)((c >> 11) & 0x1Fu);
            uint8_t g6 = (uint8_t)((c >> 5) & 0x3Fu);
            uint8_t b5 = (uint8_t)(c & 0x1Fu);
            uint8_t r = (uint8_t)((r5 << 3) | (r5 >> 2));
            uint8_t g = (uint8_t)((g6 << 2) | (g6 >> 4));
            uint8_t b = (uint8_t)((b5 << 3) | (b5 >> 2));

            if (lcd->cfg.color_mode == ST7789_COLOR_MODE_RGB565)
            {
                if (out_len + 2 > sizeof(out))
                {
                    st7789_write_bytes(lcd, out, out_len);
                    out_len = 0;
                }
                out[out_len++] = (uint8_t)(c >> 8);
                out[out_len++] = (uint8_t)(c & 0xFFu);
            }
            else
            {
                if (out_len + 3 > sizeof(out))
                {
                    st7789_write_bytes(lcd, out, out_len);
                    out_len = 0;
                }
                out[out_len++] = (uint8_t)(r & 0xFCu);
                out[out_len++] = (uint8_t)(g & 0xFCu);
                out[out_len++] = (uint8_t)(b & 0xFCu);
            }
        }
    }

    if (out_len)
    {
        st7789_write_bytes(lcd, out, out_len);
    }
    st7789_end_pixels();
}

typedef struct
{
    const uint8_t* data;
    size_t len;
    size_t pos;
} st7789_gif_stream_t;

typedef struct
{
    bool transparent;
    uint8_t transparent_index;
    uint16_t delay_cs;
    uint8_t disposal;
} st7789_gif_gce_t;

typedef struct
{
    st7789_gif_stream_t* s;
    uint8_t subblock_remaining;
    bool terminator_seen;
    uint32_t datum;
    uint8_t bits;
} st7789_gif_lzw_reader_t;

static uint16_t st7789_gif_prefix[4096];
static uint8_t st7789_gif_suffix[4096];
static uint8_t st7789_gif_stack[4096];
static uint8_t st7789_gif_gct[256 * 3];
static uint8_t st7789_gif_lct[256 * 3];
static uint8_t st7789_gif_line_idx[ST7789_WIDTH];

static bool st7789_gif_read_u8(st7789_gif_stream_t* s, uint8_t* out)
{
    if (!s || s->pos >= s->len) return false;
    *out = s->data[s->pos++];
    return true;
}

static bool st7789_gif_read_u16le(st7789_gif_stream_t* s, uint16_t* out)
{
    uint8_t lo = 0, hi = 0;
    if (!st7789_gif_read_u8(s, &lo)) return false;
    if (!st7789_gif_read_u8(s, &hi)) return false;
    *out = (uint16_t)((uint16_t)lo | ((uint16_t)hi << 8));
    return true;
}

static bool st7789_gif_skip(st7789_gif_stream_t* s, size_t n)
{
    if (!s) return false;
    if (s->pos + n > s->len) return false;
    s->pos += n;
    return true;
}

static bool st7789_gif_skip_subblocks(st7789_gif_stream_t* s)
{
    uint8_t sz = 0;
    while (1)
    {
        if (!st7789_gif_read_u8(s, &sz)) return false;
        if (sz == 0) return true;
        if (!st7789_gif_skip(s, sz)) return false;
    }
}

static bool st7789_gif_read_color_table(st7789_gif_stream_t* s, uint8_t* pal, uint16_t count)
{
    if (count > 256) return false;
    if (!s || !pal) return false;
    for (uint16_t i = 0; i < count; i++)
    {
        uint8_t r = 0, g = 0, b = 0;
        if (!st7789_gif_read_u8(s, &r)) return false;
        if (!st7789_gif_read_u8(s, &g)) return false;
        if (!st7789_gif_read_u8(s, &b)) return false;
        pal[i * 3u + 0u] = r;
        pal[i * 3u + 1u] = g;
        pal[i * 3u + 2u] = b;
    }
    return true;
}

static bool st7789_gif_read_gce(st7789_gif_stream_t* s, st7789_gif_gce_t* gce)
{
    uint8_t block_size = 0;
    if (!st7789_gif_read_u8(s, &block_size)) return false;
    if (block_size != 4) return false;

    uint8_t packed = 0;
    uint16_t delay_cs = 0;
    uint8_t trans = 0;
    uint8_t terminator = 0;

    if (!st7789_gif_read_u8(s, &packed)) return false;
    if (!st7789_gif_read_u16le(s, &delay_cs)) return false;
    if (!st7789_gif_read_u8(s, &trans)) return false;
    if (!st7789_gif_read_u8(s, &terminator)) return false;
    if (terminator != 0) return false;

    if (gce)
    {
        gce->transparent = (packed & 0x01u) ? true : false;
        gce->transparent_index = trans;
        gce->delay_cs = delay_cs;
        gce->disposal = (uint8_t)((packed >> 2) & 0x07u);
    }

    return true;
}

static bool st7789_gif_lzw_next_data_byte(st7789_gif_lzw_reader_t* r, uint8_t* out)
{
    if (!r || !out) return false;
    if (r->terminator_seen) return false;
    if (r->subblock_remaining == 0)
    {
        uint8_t sz = 0;
        if (!st7789_gif_read_u8(r->s, &sz)) return false;
        if (sz == 0)
        {
            r->terminator_seen = true;
            return false;
        }
        r->subblock_remaining = sz;
    }

    if (!st7789_gif_read_u8(r->s, out)) return false;
    r->subblock_remaining--;
    return true;
}

static bool st7789_gif_lzw_drain_to_terminator(st7789_gif_lzw_reader_t* r)
{
    if (!r) return false;
    if (r->terminator_seen) return true;

    if (r->subblock_remaining)
    {
        if (!st7789_gif_skip(r->s, r->subblock_remaining)) return false;
        r->subblock_remaining = 0;
    }

    if (!st7789_gif_skip_subblocks(r->s)) return false;
    r->terminator_seen = true;
    return true;
}

static int st7789_gif_lzw_read_code(st7789_gif_lzw_reader_t* r, int code_size)
{
    while (r->bits < (uint8_t)code_size)
    {
        uint8_t byte = 0;
        if (!st7789_gif_lzw_next_data_byte(r, &byte))
        {
            return -1;
        }
        r->datum |= ((uint32_t)byte << r->bits);
        r->bits = (uint8_t)(r->bits + 8u);
    }

    int code_mask = (1 << code_size) - 1;
    int code = (int)(r->datum & (uint32_t)code_mask);
    r->datum >>= code_size;
    r->bits = (uint8_t)(r->bits - (uint8_t)code_size);
    return code;
}

static void st7789_gif_palette_color(const uint8_t* pal, uint16_t count, uint8_t index, uint8_t* r, uint8_t* g, uint8_t* b)
{
    if (!pal || count == 0 || index >= count)
    {
        *r = 0;
        *g = 0;
        *b = 0;
        return;
    }
    *r = pal[index * 3u + 0u];
    *g = pal[index * 3u + 1u];
    *b = pal[index * 3u + 2u];
}

static void st7789_gif_write_scanline(ST7789_Handle* lcd, uint16_t x, uint16_t y, const uint8_t* idx, uint16_t w, const uint8_t* pal, uint16_t pal_count, const st7789_gif_gce_t* gce, uint32_t bg_rgb888)
{
    if (w == 0) return;

    uint8_t bg_r = (uint8_t)((bg_rgb888 >> 16) & 0xFFu);
    uint8_t bg_g = (uint8_t)((bg_rgb888 >> 8) & 0xFFu);
    uint8_t bg_b = (uint8_t)(bg_rgb888 & 0xFFu);
    uint16_t bg565 = st7789_rgb888_to_565(bg_rgb888);

    st7789_set_address_window(lcd, x, y, (uint16_t)(x + w - 1u), y);

    uint8_t out[192];
    size_t out_len = 0;

    for (uint16_t i = 0; i < w; i++)
    {
        uint8_t r = 0, g = 0, b = 0;
        uint8_t px = idx[i];

        if (gce && gce->transparent && px == gce->transparent_index)
        {
            r = bg_r;
            g = bg_g;
            b = bg_b;
        }
        else
        {
            st7789_gif_palette_color(pal, pal_count, px, &r, &g, &b);
        }

        if (lcd->cfg.color_mode == ST7789_COLOR_MODE_RGB565)
        {
            uint16_t c = (gce && gce->transparent && px == gce->transparent_index) ? bg565 : st7789_rgb888_to_565(((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b);
            if (out_len + 2 > sizeof(out))
            {
                st7789_write_bytes(lcd, out, out_len);
                out_len = 0;
            }
            out[out_len++] = (uint8_t)(c >> 8);
            out[out_len++] = (uint8_t)(c & 0xFFu);
        }
        else
        {
            if (out_len + 3 > sizeof(out))
            {
                st7789_write_bytes(lcd, out, out_len);
                out_len = 0;
            }
            out[out_len++] = (uint8_t)(r & 0xFCu);
            out[out_len++] = (uint8_t)(g & 0xFCu);
            out[out_len++] = (uint8_t)(b & 0xFCu);
        }
    }

    if (out_len)
    {
        st7789_write_bytes(lcd, out, out_len);
    }

    st7789_end_pixels();
}

static bool st7789_gif_decode_image(ST7789_Handle* lcd, uint16_t base_x, uint16_t base_y, st7789_gif_stream_t* s, uint16_t left, uint16_t top, uint16_t w, uint16_t h, bool interlaced, const uint8_t* pal, uint16_t pal_count, const st7789_gif_gce_t* gce, uint32_t bg_rgb888, uint16_t* out_delay_ms, uint8_t* out_disposal)
{
    uint8_t lzw_min = 0;
    if (!st7789_gif_read_u8(s, &lzw_min)) return false;
    if (lzw_min < 2 || lzw_min > 8) return false;

    if (w > ST7789_WIDTH) return false;

    st7789_gif_lzw_reader_t r = {0};
    r.s = s;
    r.subblock_remaining = 0;
    r.terminator_seen = false;
    r.datum = 0;
    r.bits = 0;

    int clear_code = 1 << lzw_min;
    int end_code = clear_code + 1;
    int code_size = lzw_min + 1;
    int next_code = end_code + 1;
    int code = 0;
    int old_code = -1;
    int in_code = 0;
    uint8_t first = 0;

    for (int i = 0; i < clear_code; i++)
    {
        st7789_gif_prefix[i] = 0xFFFFu;
        st7789_gif_suffix[i] = (uint8_t)i;
    }

    uint16_t row_actual = 0;
    uint8_t pass = 0;
    uint16_t interlace_row = 0;
    if (interlaced)
    {
        pass = 0;
        interlace_row = 0;
        row_actual = 0;
    }
    else
    {
        row_actual = 0;
    }

    uint16_t col = 0;
    uint16_t rows_written = 0;

    while (rows_written < h)
    {
        code = st7789_gif_lzw_read_code(&r, code_size);
        if (code < 0) break;

        if (code == clear_code)
        {
            code_size = lzw_min + 1;
            next_code = end_code + 1;
            old_code = -1;
            continue;
        }
        if (code == end_code)
        {
            break;
        }

        if (old_code == -1)
        {
            uint8_t px = (uint8_t)code;
            st7789_gif_line_idx[col++] = px;
            old_code = code;
            first = (uint8_t)code;

            if (col == w)
            {
                uint16_t screen_y = (uint16_t)(base_y + top + row_actual);
                uint16_t screen_x = (uint16_t)(base_x + left);
                if (screen_x < st7789_width(lcd) && screen_y < st7789_height(lcd))
                {
                    uint16_t vis_w = st7789_min_u16(w, (uint16_t)(st7789_width(lcd) - screen_x));
                    st7789_gif_write_scanline(lcd, screen_x, screen_y, st7789_gif_line_idx, vis_w, pal, pal_count, gce, bg_rgb888);
                }

                col = 0;
                rows_written++;

                if (interlaced)
                {
                    static const uint8_t start[4] = {0, 4, 2, 1};
                    static const uint8_t step[4] = {8, 8, 4, 2};

                    interlace_row = (uint16_t)(interlace_row + step[pass]);
                    while (pass < 3 && interlace_row >= h)
                    {
                        pass++;
                        interlace_row = start[pass];
                    }
                    row_actual = interlace_row;
                }
                else
                {
                    row_actual++;
                }
            }
            continue;
        }

        in_code = code;
        int stack_top = 0;

        if (code >= next_code)
        {
            st7789_gif_stack[stack_top++] = first;
            code = old_code;
        }

        while (code >= clear_code)
        {
            st7789_gif_stack[stack_top++] = st7789_gif_suffix[code];
            code = st7789_gif_prefix[code];
        }

        first = (uint8_t)code;
        st7789_gif_stack[stack_top++] = first;

        if (next_code < 4096)
        {
            st7789_gif_prefix[next_code] = (uint16_t)old_code;
            st7789_gif_suffix[next_code] = first;
            next_code++;
            if (next_code == (1 << code_size) && code_size < 12)
            {
                code_size++;
            }
        }

        old_code = in_code;

        while (stack_top)
        {
            uint8_t px = st7789_gif_stack[--stack_top];
            st7789_gif_line_idx[col++] = px;
            if (col == w)
            {
                uint16_t screen_y = (uint16_t)(base_y + top + row_actual);
                uint16_t screen_x = (uint16_t)(base_x + left);
                if (screen_x < st7789_width(lcd) && screen_y < st7789_height(lcd))
                {
                    uint16_t vis_w = st7789_min_u16(w, (uint16_t)(st7789_width(lcd) - screen_x));
                    st7789_gif_write_scanline(lcd, screen_x, screen_y, st7789_gif_line_idx, vis_w, pal, pal_count, gce, bg_rgb888);
                }

                col = 0;
                rows_written++;

                if (interlaced)
                {
                    static const uint8_t start[4] = {0, 4, 2, 1};
                    static const uint8_t step[4] = {8, 8, 4, 2};

                    interlace_row = (uint16_t)(interlace_row + step[pass]);
                    while (pass < 3 && interlace_row >= h)
                    {
                        pass++;
                        interlace_row = start[pass];
                    }
                    row_actual = interlace_row;
                }
                else
                {
                    row_actual++;
                }

                if (rows_written >= h) break;
            }
        }
    }

    if (!st7789_gif_lzw_drain_to_terminator(&r)) return false;
    if (rows_written != h) return false;

    if (out_delay_ms)
    {
        uint16_t delay_cs = gce ? gce->delay_cs : 0;
        uint16_t delay_ms = (uint16_t)(delay_cs * 10u);
        if (delay_ms == 0) delay_ms = 10;
        *out_delay_ms = delay_ms;
    }

    if (out_disposal)
    {
        *out_disposal = gce ? gce->disposal : 0;
    }

    return true;
}

static bool st7789_gif_has_signature(const uint8_t* gif, size_t gif_len)
{
    if (!gif || gif_len < 6) return false;
    if (gif[0] != 'G' || gif[1] != 'I' || gif[2] != 'F') return false;
    if (gif[3] != '8') return false;
    if (gif[4] != '7' && gif[4] != '9') return false;
    if (gif[5] != 'a') return false;
    return true;
}

bool ST7789_GIF_GetCanvasSize(const uint8_t* gif, size_t gif_len, uint16_t* out_w, uint16_t* out_h)
{
    if (!st7789_gif_has_signature(gif, gif_len)) return false;

    st7789_gif_stream_t s = {0};
    s.data = gif;
    s.len = gif_len;
    s.pos = 6;

    uint16_t w = 0, h = 0;
    uint8_t packed = 0;
    uint8_t bg = 0;
    uint8_t aspect = 0;

    if (!st7789_gif_read_u16le(&s, &w)) return false;
    if (!st7789_gif_read_u16le(&s, &h)) return false;
    if (!st7789_gif_read_u8(&s, &packed)) return false;
    if (!st7789_gif_read_u8(&s, &bg)) return false;
    if (!st7789_gif_read_u8(&s, &aspect)) return false;

    if (out_w) *out_w = w;
    if (out_h) *out_h = h;

    return true;
}

bool ST7789_GIF_RenderFirstFrame(ST7789_Handle* lcd, uint16_t x, uint16_t y, const uint8_t* gif, size_t gif_len, uint32_t bg_rgb888)
{
    if (!lcd || !lcd->inited) return false;
    if (!st7789_gif_has_signature(gif, gif_len)) return false;

    st7789_gif_stream_t s = {0};
    s.data = gif;
    s.len = gif_len;
    s.pos = 6;

    uint16_t canvas_w = 0, canvas_h = 0;
    uint8_t packed = 0;
    uint8_t bg_index = 0;
    uint8_t aspect = 0;

    if (!st7789_gif_read_u16le(&s, &canvas_w)) return false;
    if (!st7789_gif_read_u16le(&s, &canvas_h)) return false;
    if (!st7789_gif_read_u8(&s, &packed)) return false;
    if (!st7789_gif_read_u8(&s, &bg_index)) return false;
    if (!st7789_gif_read_u8(&s, &aspect)) return false;

    bool gct_flag = (packed & 0x80u) ? true : false;
    uint16_t gct_count = 0;
    if (gct_flag)
    {
        uint8_t sz = (uint8_t)(packed & 0x07u);
        gct_count = (uint16_t)(1u << (sz + 1u));
        if (!st7789_gif_read_color_table(&s, st7789_gif_gct, gct_count)) return false;
    }

    st7789_gif_gce_t gce = {0};
    gce.transparent = false;
    gce.transparent_index = 0;
    gce.delay_cs = 0;
    gce.disposal = 0;

    while (s.pos < s.len)
    {
        uint8_t sep = 0;
        if (!st7789_gif_read_u8(&s, &sep)) return false;

        if (sep == 0x3Bu)
        {
            return false;
        }
        else if (sep == 0x21u)
        {
            uint8_t label = 0;
            if (!st7789_gif_read_u8(&s, &label)) return false;
            if (label == 0xF9u)
            {
                if (!st7789_gif_read_gce(&s, &gce)) return false;
            }
            else
            {
                if (!st7789_gif_skip_subblocks(&s)) return false;
            }
        }
        else if (sep == 0x2Cu)
        {
            uint16_t left = 0, top = 0, w = 0, h = 0;
            uint8_t ipacked = 0;
            if (!st7789_gif_read_u16le(&s, &left)) return false;
            if (!st7789_gif_read_u16le(&s, &top)) return false;
            if (!st7789_gif_read_u16le(&s, &w)) return false;
            if (!st7789_gif_read_u16le(&s, &h)) return false;
            if (!st7789_gif_read_u8(&s, &ipacked)) return false;

            bool lct_flag = (ipacked & 0x80u) ? true : false;
            bool interlaced = (ipacked & 0x40u) ? true : false;
            uint16_t lct_count = 0;
            const uint8_t* pal = st7789_gif_gct;
            uint16_t pal_count = gct_count;

            if (lct_flag)
            {
                uint8_t sz = (uint8_t)(ipacked & 0x07u);
                lct_count = (uint16_t)(1u << (sz + 1u));
                if (!st7789_gif_read_color_table(&s, st7789_gif_lct, lct_count)) return false;
                pal = st7789_gif_lct;
                pal_count = lct_count;
            }

            uint16_t delay_ms = 0;
            uint8_t disposal = 0;
            bool ok = st7789_gif_decode_image(lcd, x, y, &s, left, top, w, h, interlaced, pal, pal_count, &gce, bg_rgb888, &delay_ms, &disposal);
            (void)canvas_w;
            (void)canvas_h;
            (void)bg_index;
            (void)aspect;
            (void)delay_ms;
            (void)disposal;
            return ok;
        }
        else
        {
            return false;
        }
    }

    return false;
}

bool ST7789_GIF_Play(ST7789_Handle* lcd, uint16_t x, uint16_t y, const uint8_t* gif, size_t gif_len, uint32_t bg_rgb888, uint16_t repeat)
{
    if (!lcd || !lcd->inited) return false;
    if (!st7789_gif_has_signature(gif, gif_len)) return false;
    if (repeat == 0) repeat = 1;

    for (uint16_t rep = 0; rep < repeat; rep++)
    {
        st7789_gif_stream_t s = {0};
        s.data = gif;
        s.len = gif_len;
        s.pos = 6;

        uint16_t canvas_w = 0, canvas_h = 0;
        uint8_t packed = 0;
        uint8_t bg_index = 0;
        uint8_t aspect = 0;

        if (!st7789_gif_read_u16le(&s, &canvas_w)) return false;
        if (!st7789_gif_read_u16le(&s, &canvas_h)) return false;
        if (!st7789_gif_read_u8(&s, &packed)) return false;
        if (!st7789_gif_read_u8(&s, &bg_index)) return false;
        if (!st7789_gif_read_u8(&s, &aspect)) return false;

        bool gct_flag = (packed & 0x80u) ? true : false;
        uint16_t gct_count = 0;
        if (gct_flag)
        {
            uint8_t sz = (uint8_t)(packed & 0x07u);
            gct_count = (uint16_t)(1u << (sz + 1u));
            if (!st7789_gif_read_color_table(&s, st7789_gif_gct, gct_count)) return false;
        }

        st7789_gif_gce_t gce = {0};
        gce.transparent = false;
        gce.transparent_index = 0;
        gce.delay_cs = 0;
        gce.disposal = 0;

        while (s.pos < s.len)
        {
            uint8_t sep = 0;
            if (!st7789_gif_read_u8(&s, &sep)) return false;

            if (sep == 0x3Bu)
            {
                break;
            }
            else if (sep == 0x21u)
            {
                uint8_t label = 0;
                if (!st7789_gif_read_u8(&s, &label)) return false;
                if (label == 0xF9u)
                {
                    if (!st7789_gif_read_gce(&s, &gce)) return false;
                }
                else
                {
                    if (!st7789_gif_skip_subblocks(&s)) return false;
                }
            }
            else if (sep == 0x2Cu)
            {
                uint16_t left = 0, top = 0, w = 0, h = 0;
                uint8_t ipacked = 0;
                if (!st7789_gif_read_u16le(&s, &left)) return false;
                if (!st7789_gif_read_u16le(&s, &top)) return false;
                if (!st7789_gif_read_u16le(&s, &w)) return false;
                if (!st7789_gif_read_u16le(&s, &h)) return false;
                if (!st7789_gif_read_u8(&s, &ipacked)) return false;

                bool lct_flag = (ipacked & 0x80u) ? true : false;
                bool interlaced = (ipacked & 0x40u) ? true : false;
                uint16_t lct_count = 0;
                const uint8_t* pal = st7789_gif_gct;
                uint16_t pal_count = gct_count;

                if (lct_flag)
                {
                    uint8_t sz = (uint8_t)(ipacked & 0x07u);
                    lct_count = (uint16_t)(1u << (sz + 1u));
                    if (!st7789_gif_read_color_table(&s, st7789_gif_lct, lct_count)) return false;
                    pal = st7789_gif_lct;
                    pal_count = lct_count;
                }

                uint16_t delay_ms = 0;
                uint8_t disposal = 0;
                if (!st7789_gif_decode_image(lcd, x, y, &s, left, top, w, h, interlaced, pal, pal_count, &gce, bg_rgb888, &delay_ms, &disposal))
                {
                    return false;
                }

                HAL_Delay(delay_ms);

                if (disposal == 2)
                {
                    uint16_t fx = (uint16_t)(x + left);
                    uint16_t fy = (uint16_t)(y + top);
                    if (fx < st7789_width(lcd) && fy < st7789_height(lcd))
                    {
                        uint16_t fw = st7789_min_u16(w, (uint16_t)(st7789_width(lcd) - fx));
                        uint16_t fh = st7789_min_u16(h, (uint16_t)(st7789_height(lcd) - fy));
                        ST7789_FillRect(lcd, fx, fy, fw, fh, bg_rgb888);
                    }
                }

                gce.transparent = false;
                gce.transparent_index = 0;
                gce.delay_cs = 0;
                gce.disposal = 0;

                (void)canvas_w;
                (void)canvas_h;
                (void)bg_index;
                (void)aspect;
            }
            else
            {
                return false;
            }
        }
    }

    return true;
}

static const uint8_t* st7789_assets_base = (const uint8_t*)0x905B0000u;

const void* ST7789_Assets_GetBaseAddress(void)
{
    return st7789_assets_base;
}

void ST7789_Assets_SetBaseAddress(const void* base)
{
    if (base) st7789_assets_base = (const uint8_t*)base;
}

static uint16_t st7789_u16le(const uint8_t* p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t st7789_u32le(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool st7789_assets_header_ok(const uint8_t* base, uint32_t* out_count, uint32_t* out_index_size)
{
    if (!base) return false;
    if (base[0] != 'H' || base[1] != 'I' || base[2] != 'M' || base[3] != 'G') return false;
    uint16_t ver = st7789_u16le(base + 4);
    if (ver != 1u) return false;
    uint32_t count = st7789_u32le(base + 16);
    uint32_t index_size = st7789_u32le(base + 20);
    if (count > 4096u) return false;
    if ((index_size & 0x0Fu) != 0u) return false;
    if (out_count) *out_count = count;
    if (out_index_size) *out_index_size = index_size;
    return true;
}

bool ST7789_Assets_Find(const char* name, ST7789_AssetInfo* out)
{
    if (!name || !out) return false;

    const uint8_t* base = st7789_assets_base;
    uint32_t count = 0;
    uint32_t index_size = 0;
    if (!st7789_assets_header_ok(base, &count, &index_size)) return false;

    const uint8_t* index = base + 64;
    for (uint32_t i = 0; i < count; i++)
    {
        const uint8_t* e = index + i * 64u;
        char nbuf[33];
        memcpy(nbuf, e, 32);
        nbuf[32] = '\0';
        for (int j = 0; j < 32; j++)
        {
            if (nbuf[j] == '\0') break;
            if ((unsigned char)nbuf[j] < 0x20) { nbuf[j] = '\0'; break; }
        }

        if (strncmp(nbuf, name, 32) != 0) continue;

        uint8_t type = e[32];
        uint32_t offset = st7789_u32le(e + 36);
        uint32_t size = st7789_u32le(e + 40);
        uint16_t w = st7789_u16le(e + 44);
        uint16_t h = st7789_u16le(e + 46);

        out->type = (ST7789_AssetType)type;
        out->data = base + offset;
        out->size = size;
        out->width = w;
        out->height = h;
        return true;
    }

    (void)index_size;
    return false;
}

bool ST7789_Assets_Draw(ST7789_Handle* lcd, uint16_t x, uint16_t y, const char* name, uint32_t bg_rgb888)
{
    if (!lcd || !lcd->inited) return false;

    ST7789_AssetInfo info = {0};
    if (!ST7789_Assets_Find(name, &info)) return false;

    if (info.type == ST7789_ASSET_TYPE_GIF)
    {
        return ST7789_GIF_RenderFirstFrame(lcd, x, y, (const uint8_t*)info.data, (size_t)info.size, bg_rgb888);
    }

    if (info.type == ST7789_ASSET_TYPE_RGB565LE)
    {
        ST7789_DrawBitmap(lcd, x, y, info.width, info.height, info.data, ST7789_BITMAP_RGB565_LE, (uint32_t)info.width * 2u);
        return true;
    }

    return false;
}

bool ST7789_Assets_Play(ST7789_Handle* lcd, uint16_t x, uint16_t y, const char* name, uint32_t bg_rgb888, uint16_t repeat)
{
    if (!lcd || !lcd->inited) return false;

    ST7789_AssetInfo info = {0};
    if (!ST7789_Assets_Find(name, &info)) return false;

    if (info.type == ST7789_ASSET_TYPE_GIF)
    {
        return ST7789_GIF_Play(lcd, x, y, (const uint8_t*)info.data, (size_t)info.size, bg_rgb888, repeat);
    }

    return ST7789_Assets_Draw(lcd, x, y, name, bg_rgb888);
}
