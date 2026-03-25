#include "st7789.h"

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

    GPIO_InitStruct.Pin = ST7789_BL_PIN;
    HAL_GPIO_Init(ST7789_BL_PORT, &GPIO_InitStruct);

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
    st7789_gpio_write(ST7789_BL_PORT, ST7789_BL_PIN, GPIO_PIN_RESET);
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

    if (lcd->cfg.rotation == ST7789_ROTATION_0 || lcd->cfg.rotation == ST7789_ROTATION_180)
    {
        lcd->cfg.width = ST7789_WIDTH;
        lcd->cfg.height = ST7789_HEIGHT;
    }
    else
    {
        lcd->cfg.width = ST7789_HEIGHT;
        lcd->cfg.height = ST7789_WIDTH;
    }
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

    if (lcd->cfg.bl_htim)
    {
        uint32_t arr = __HAL_TIM_GET_AUTORELOAD(lcd->cfg.bl_htim);
        uint32_t ccr = (arr * (uint32_t)percent) / 100u;
        __HAL_TIM_SET_COMPARE(lcd->cfg.bl_htim, lcd->cfg.bl_tim_channel, ccr);
        (void)HAL_TIM_PWM_Start(lcd->cfg.bl_htim, lcd->cfg.bl_tim_channel);
        return;
    }

    st7789_gpio_write(ST7789_BL_PORT, ST7789_BL_PIN, (percent == 0) ? GPIO_PIN_RESET : GPIO_PIN_SET);
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
    c.bl_htim = NULL;
    c.bl_tim_channel = 0;

    if (cfg) c = *cfg;
    lcd->cfg = c;
    lcd->inited = false;

    st7789_gpio_init();

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
    ST7789_FillRect(lcd, 0, 0, st7789_width(lcd), st7789_height(lcd), rgb888);
}

void ST7789_FillRect(ST7789_Handle* lcd, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t rgb888)
{
    if (!lcd || !lcd->inited) return;
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
    if (x >= st7789_width(lcd) || y >= st7789_height(lcd)) return;

    st7789_set_address_window(lcd, x, y, x, y);
    st7789_write_color_repeat(lcd, rgb888, 1);
    st7789_end_pixels();
}

static int st7789_abs_i(int v) { return (v < 0) ? -v : v; }

void ST7789_DrawLine(ST7789_Handle* lcd, int x0, int y0, int x1, int y1, uint32_t rgb888)
{
    if (!lcd || !lcd->inited) return;

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
    if (w == 0 || h == 0) return;

    ST7789_DrawLine(lcd, x, y, (int)(x + w - 1u), y, rgb888);
    ST7789_DrawLine(lcd, x, (int)(y + h - 1u), (int)(x + w - 1u), (int)(y + h - 1u), rgb888);
    ST7789_DrawLine(lcd, x, y, x, (int)(y + h - 1u), rgb888);
    ST7789_DrawLine(lcd, (int)(x + w - 1u), y, (int)(x + w - 1u), (int)(y + h - 1u), rgb888);
}

void ST7789_DrawCircle(ST7789_Handle* lcd, int x0, int y0, int r, uint32_t rgb888)
{
    if (!lcd || !lcd->inited) return;
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

void ST7789_DrawChar(ST7789_Handle* lcd, uint16_t x, uint16_t y, char c, uint32_t fg_rgb888, uint32_t bg_rgb888, uint8_t scale)
{
    if (!lcd || !lcd->inited) return;
    if (scale == 0) scale = 1;

    if (c < 32 || c > 126) c = '?';
    const uint8_t* glyph = st7789_font5x7[(uint8_t)(c - 32)];

    uint16_t w = (uint16_t)(6u * scale);
    uint16_t h = (uint16_t)(8u * scale);

    ST7789_FillRect(lcd, x, y, w, h, bg_rgb888);

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
    if (scale == 0) scale = 1;

    uint16_t cx = x;
    uint16_t cy = y;

    uint16_t step_x = (uint16_t)(6u * scale);
    uint16_t step_y = (uint16_t)(8u * scale);

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
