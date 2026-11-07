/**
 * @file src/hal/stm32h7/lcd.c
 * @brief ST7735R driver for WeAct STM32H750 built-in 160×80 LCD.
 *
 * GPIO (GPIOE — clock already enabled by spi.c):
 *   CS  PE11  active-low chip select
 *   DC  PE13  HIGH=data / LOW=command
 *   BL  PE10  ACTIVE-LOW: LOW = backlight on (confirmed on the bench)
 *
 * LCD RST is wired to board NRST (hardware reset) — not a software GPIO.
 * PE3 is the USER LED and is NOT driven by this driver.
 *
 * Panel offsets (internal GRAM origin):  x+1, y+26
 * MADCTL 0x78: MX+MV+ML+BGR → landscape, 160×80 visible area.
 *
 * Init sequence derived from WeAct official ST7735 BSP driver
 * (SDK/HAL/STM32H750/03-LCD_Test/Drivers/BSP/ST7735/st7735.c).
 */

#include "obsw/hal/stm32h7/lcd.h"
#include "obsw/hal/stm32h7/spi.h"
#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* GPIO                                                                 */
/* ------------------------------------------------------------------ */

#define GPIOE_BASE    0x58021000UL
#define GPIOE_MODER   (*(volatile uint32_t *)(GPIOE_BASE + 0x00U))
#define GPIOE_OSPEEDR (*(volatile uint32_t *)(GPIOE_BASE + 0x08U))
#define GPIOE_BSRR    (*(volatile uint32_t *)(GPIOE_BASE + 0x18U))

/* Atomic set/clear via BSRR: upper 16 bits clear, lower 16 bits set */
#define PE_SET(n)   (GPIOE_BSRR = (1U << (n)))
#define PE_CLR(n)   (GPIOE_BSRR = (1U << ((n) + 16U)))

#define CS_LOW      PE_CLR(11)
#define CS_HIGH     PE_SET(11)
#define DC_CMD      PE_CLR(13)
#define DC_DATA     PE_SET(13)
/* Backlight is active-low on the WeAct H750 board. Driving PE10 HIGH turns
 * it OFF; that inverted polarity was the long-standing "blank LCD" bug,
 * previously mistaken for an ST7735R power-supervisor reset. */
#define BL_ON       PE_CLR(10)
#define BL_OFF      PE_SET(10)
/* PE3 is the USER LED, not LCD RST. LCD RST is tied to board NRST. */

/* ------------------------------------------------------------------ */
/* ST7735R command bytes                                                */
/* ------------------------------------------------------------------ */

#define ST_SWRESET  0x01U
#define ST_SLPOUT   0x11U
#define ST_NORON    0x13U
#define ST_INVOFF   0x20U
#define ST_INVON    0x21U
#define ST_DISPON   0x29U
#define ST_CASET    0x2AU
#define ST_RASET    0x2BU
#define ST_RAMWR    0x2CU
#define ST_MADCTL   0x36U
#define ST_COLMOD   0x3AU
#define ST_FRMCTR1  0xB1U
#define ST_FRMCTR2  0xB2U
#define ST_FRMCTR3  0xB3U
#define ST_INVCTR   0xB4U
#define ST_PWCTR1   0xC0U
#define ST_PWCTR2   0xC1U
#define ST_PWCTR3   0xC2U
#define ST_PWCTR4   0xC3U
#define ST_PWCTR5   0xC4U
#define ST_VMCTR1   0xC5U
#define ST_GMCTRP1  0xE0U
#define ST_GMCTRN1  0xE1U

/* Panel-specific offsets into GRAM */
#define X_OFFSET    1U
#define Y_OFFSET    26U

/* ------------------------------------------------------------------ */
/* 5×7 ASCII font (0x20–0x7E), column-major, bit0=top row             */
/* ------------------------------------------------------------------ */

static const uint8_t font5x7[95][5] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00 }, /* 0x20 ' ' */
    { 0x00, 0x00, 0x5F, 0x00, 0x00 }, /* 0x21 '!' */
    { 0x00, 0x07, 0x00, 0x07, 0x00 }, /* 0x22 '"' */
    { 0x14, 0x7F, 0x14, 0x7F, 0x14 }, /* 0x23 '#' */
    { 0x24, 0x2A, 0x7F, 0x2A, 0x12 }, /* 0x24 '$' */
    { 0x23, 0x13, 0x08, 0x64, 0x62 }, /* 0x25 '%' */
    { 0x36, 0x49, 0x55, 0x22, 0x50 }, /* 0x26 '&' */
    { 0x00, 0x05, 0x03, 0x00, 0x00 }, /* 0x27 '\'' */
    { 0x00, 0x1C, 0x22, 0x41, 0x00 }, /* 0x28 '(' */
    { 0x00, 0x41, 0x22, 0x1C, 0x00 }, /* 0x29 ')' */
    { 0x14, 0x08, 0x3E, 0x08, 0x14 }, /* 0x2A '*' */
    { 0x08, 0x08, 0x3E, 0x08, 0x08 }, /* 0x2B '+' */
    { 0x00, 0x50, 0x30, 0x00, 0x00 }, /* 0x2C ',' */
    { 0x08, 0x08, 0x08, 0x08, 0x08 }, /* 0x2D '-' */
    { 0x00, 0x60, 0x60, 0x00, 0x00 }, /* 0x2E '.' */
    { 0x20, 0x10, 0x08, 0x04, 0x02 }, /* 0x2F '/' */
    { 0x3E, 0x51, 0x49, 0x45, 0x3E }, /* 0x30 '0' */
    { 0x00, 0x42, 0x7F, 0x40, 0x00 }, /* 0x31 '1' */
    { 0x42, 0x61, 0x51, 0x49, 0x46 }, /* 0x32 '2' */
    { 0x21, 0x41, 0x45, 0x4B, 0x31 }, /* 0x33 '3' */
    { 0x18, 0x14, 0x12, 0x7F, 0x10 }, /* 0x34 '4' */
    { 0x27, 0x45, 0x45, 0x45, 0x39 }, /* 0x35 '5' */
    { 0x3C, 0x4A, 0x49, 0x49, 0x30 }, /* 0x36 '6' */
    { 0x01, 0x71, 0x09, 0x05, 0x03 }, /* 0x37 '7' */
    { 0x36, 0x49, 0x49, 0x49, 0x36 }, /* 0x38 '8' */
    { 0x06, 0x49, 0x49, 0x29, 0x1E }, /* 0x39 '9' */
    { 0x00, 0x36, 0x36, 0x00, 0x00 }, /* 0x3A ':' */
    { 0x00, 0x56, 0x36, 0x00, 0x00 }, /* 0x3B ';' */
    { 0x08, 0x14, 0x22, 0x41, 0x00 }, /* 0x3C '<' */
    { 0x14, 0x14, 0x14, 0x14, 0x14 }, /* 0x3D '=' */
    { 0x00, 0x41, 0x22, 0x14, 0x08 }, /* 0x3E '>' */
    { 0x02, 0x01, 0x51, 0x09, 0x06 }, /* 0x3F '?' */
    { 0x32, 0x49, 0x79, 0x41, 0x3E }, /* 0x40 '@' */
    { 0x7E, 0x11, 0x11, 0x11, 0x7E }, /* 0x41 'A' */
    { 0x7F, 0x49, 0x49, 0x49, 0x36 }, /* 0x42 'B' */
    { 0x3E, 0x41, 0x41, 0x41, 0x22 }, /* 0x43 'C' */
    { 0x7F, 0x41, 0x41, 0x22, 0x1C }, /* 0x44 'D' */
    { 0x7F, 0x49, 0x49, 0x49, 0x41 }, /* 0x45 'E' */
    { 0x7F, 0x09, 0x09, 0x09, 0x01 }, /* 0x46 'F' */
    { 0x3E, 0x41, 0x49, 0x49, 0x7A }, /* 0x47 'G' */
    { 0x7F, 0x08, 0x08, 0x08, 0x7F }, /* 0x48 'H' */
    { 0x00, 0x41, 0x7F, 0x41, 0x00 }, /* 0x49 'I' */
    { 0x20, 0x40, 0x41, 0x3F, 0x01 }, /* 0x4A 'J' */
    { 0x7F, 0x08, 0x14, 0x22, 0x41 }, /* 0x4B 'K' */
    { 0x7F, 0x40, 0x40, 0x40, 0x40 }, /* 0x4C 'L' */
    { 0x7F, 0x02, 0x0C, 0x02, 0x7F }, /* 0x4D 'M' */
    { 0x7F, 0x04, 0x08, 0x10, 0x7F }, /* 0x4E 'N' */
    { 0x3E, 0x41, 0x41, 0x41, 0x3E }, /* 0x4F 'O' */
    { 0x7F, 0x09, 0x09, 0x09, 0x06 }, /* 0x50 'P' */
    { 0x3E, 0x41, 0x51, 0x21, 0x5E }, /* 0x51 'Q' */
    { 0x7F, 0x09, 0x19, 0x29, 0x46 }, /* 0x52 'R' */
    { 0x46, 0x49, 0x49, 0x49, 0x31 }, /* 0x53 'S' */
    { 0x01, 0x01, 0x7F, 0x01, 0x01 }, /* 0x54 'T' */
    { 0x3F, 0x40, 0x40, 0x40, 0x3F }, /* 0x55 'U' */
    { 0x1F, 0x20, 0x40, 0x20, 0x1F }, /* 0x56 'V' */
    { 0x3F, 0x40, 0x38, 0x40, 0x3F }, /* 0x57 'W' */
    { 0x63, 0x14, 0x08, 0x14, 0x63 }, /* 0x58 'X' */
    { 0x07, 0x08, 0x70, 0x08, 0x07 }, /* 0x59 'Y' */
    { 0x61, 0x51, 0x49, 0x45, 0x43 }, /* 0x5A 'Z' */
    { 0x00, 0x7F, 0x41, 0x41, 0x00 }, /* 0x5B '[' */
    { 0x02, 0x04, 0x08, 0x10, 0x20 }, /* 0x5C '\' */
    { 0x00, 0x41, 0x41, 0x7F, 0x00 }, /* 0x5D ']' */
    { 0x04, 0x02, 0x01, 0x02, 0x04 }, /* 0x5E '^' */
    { 0x40, 0x40, 0x40, 0x40, 0x40 }, /* 0x5F '_' */
    { 0x00, 0x01, 0x02, 0x04, 0x00 }, /* 0x60 '`' */
    { 0x20, 0x54, 0x54, 0x54, 0x78 }, /* 0x61 'a' */
    { 0x7F, 0x48, 0x44, 0x44, 0x38 }, /* 0x62 'b' */
    { 0x38, 0x44, 0x44, 0x44, 0x20 }, /* 0x63 'c' */
    { 0x38, 0x44, 0x44, 0x48, 0x7F }, /* 0x64 'd' */
    { 0x38, 0x54, 0x54, 0x54, 0x18 }, /* 0x65 'e' */
    { 0x08, 0x7E, 0x09, 0x01, 0x02 }, /* 0x66 'f' */
    { 0x0C, 0x52, 0x52, 0x52, 0x3E }, /* 0x67 'g' */
    { 0x7F, 0x08, 0x04, 0x04, 0x78 }, /* 0x68 'h' */
    { 0x00, 0x44, 0x7D, 0x40, 0x00 }, /* 0x69 'i' */
    { 0x20, 0x40, 0x44, 0x3D, 0x00 }, /* 0x6A 'j' */
    { 0x7F, 0x10, 0x28, 0x44, 0x00 }, /* 0x6B 'k' */
    { 0x00, 0x41, 0x7F, 0x40, 0x00 }, /* 0x6C 'l' */
    { 0x7C, 0x04, 0x18, 0x04, 0x7C }, /* 0x6D 'm' */
    { 0x7C, 0x08, 0x04, 0x04, 0x78 }, /* 0x6E 'n' */
    { 0x38, 0x44, 0x44, 0x44, 0x38 }, /* 0x6F 'o' */
    { 0x7C, 0x14, 0x14, 0x14, 0x08 }, /* 0x70 'p' */
    { 0x08, 0x14, 0x14, 0x18, 0x7C }, /* 0x71 'q' */
    { 0x7C, 0x08, 0x04, 0x04, 0x08 }, /* 0x72 'r' */
    { 0x48, 0x54, 0x54, 0x54, 0x20 }, /* 0x73 's' */
    { 0x04, 0x3F, 0x44, 0x40, 0x20 }, /* 0x74 't' */
    { 0x3C, 0x40, 0x40, 0x20, 0x7C }, /* 0x75 'u' */
    { 0x1C, 0x20, 0x40, 0x20, 0x1C }, /* 0x76 'v' */
    { 0x3C, 0x40, 0x30, 0x40, 0x3C }, /* 0x77 'w' */
    { 0x44, 0x28, 0x10, 0x28, 0x44 }, /* 0x78 'x' */
    { 0x0C, 0x50, 0x50, 0x50, 0x3C }, /* 0x79 'y' */
    { 0x44, 0x64, 0x54, 0x4C, 0x44 }, /* 0x7A 'z' */
    { 0x00, 0x08, 0x36, 0x41, 0x00 }, /* 0x7B '{' */
    { 0x00, 0x00, 0x7F, 0x00, 0x00 }, /* 0x7C '|' */
    { 0x00, 0x41, 0x36, 0x08, 0x00 }, /* 0x7D '}' */
    { 0x10, 0x08, 0x08, 0x10, 0x08 }, /* 0x7E '~' */
};

/* ------------------------------------------------------------------ */
/* Low-level helpers                                                    */
/* ------------------------------------------------------------------ */

static void lcd_delay_ms(uint32_t ms)
{
    /* DWT cycle counter — exact at 64 MHz, no calibration needed.
     * TRCENA enables the trace unit; CYCCNTENA starts the free-running
     * counter.  Both are safe to set from software without a debugger. */
    volatile uint32_t *demcr   = (volatile uint32_t *)0xE000EDFCU;
    volatile uint32_t *dwtctrl = (volatile uint32_t *)0xE0001000U;
    volatile uint32_t *cyccnt  = (volatile uint32_t *)0xE0001004U;
    *demcr   |= (1U << 24);  /* TRCENA */
    *dwtctrl |= (1U << 0);   /* CYCCNTENA */
    uint32_t start  = *cyccnt;
    uint32_t target = ms * 64000U;  /* 64 MHz → 64 000 cycles/ms */
    while ((*cyccnt - start) < target)
        ;
}

static void lcd_cmd(uint8_t cmd)
{
    CS_LOW; DC_CMD;
    obsw_spi4_write(&cmd, 1);
    CS_HIGH;
}

static void lcd_data(const uint8_t *buf, uint16_t len)
{
    CS_LOW; DC_DATA;
    obsw_spi4_write(buf, len);
    CS_HIGH;
}

static void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t d[4];
    x0 += X_OFFSET; x1 += X_OFFSET;
    y0 += Y_OFFSET; y1 += Y_OFFSET;

    d[0] = x0 >> 8; d[1] = x0 & 0xFF;
    d[2] = x1 >> 8; d[3] = x1 & 0xFF;
    lcd_cmd(ST_CASET); lcd_data(d, 4);

    d[0] = y0 >> 8; d[1] = y0 & 0xFF;
    d[2] = y1 >> 8; d[3] = y1 & 0xFF;
    lcd_cmd(ST_RASET); lcd_data(d, 4);
}

/* ------------------------------------------------------------------ */
/* Init                                                                 */
/* ------------------------------------------------------------------ */

static void lcd_gpio_init(void)
{
    /* PE10 (BL), PE11 (CS), PE13 (DC) → GPIO outputs.
     * PE3 is the USER LED — not driven here.
     * LCD RST = board NRST (hardware), not software-controllable.
     * GPIOE clock already enabled by obsw_spi4_init(). */
    BL_OFF;   /* latch ODR before the pin becomes an output: no stale flash */
    GPIOE_MODER &= ~((3U << 20) | (3U << 22) | (3U << 26));
    GPIOE_MODER |=  ((1U << 20) | (1U << 22) | (1U << 26));
    GPIOE_OSPEEDR |= (3U << 20) | (3U << 22) | (3U << 26);

    CS_HIGH; DC_DATA;
    /* Backlight stays off until the caller has drawn something
     * (lcd_backlight_on()); GRAM holds the previous run's picture. */
}

/* ------------------------------------------------------------------ */
/* Panel configuration                                                  */
/* ------------------------------------------------------------------ */

/* One register write: command byte plus up to 16 parameter bytes. */
typedef struct {
    uint8_t cmd;
    uint8_t len;
    uint8_t data[16];
} lcd_reg_t;

/* Full register configuration written after every SLPOUT. Values from the
 * WeAct ST7735 BSP. A panel reset (power-on/NRST or SWRESET) returns all
 * of these to defaults, so every wake path re-sends the whole table. */
static const lcd_reg_t s_panel_cfg[] = {
    {ST_FRMCTR1, 3U,  {0x01, 0x2C, 0x2D}},                     /* frame rate, normal  */
    {ST_FRMCTR2, 3U,  {0x01, 0x2C, 0x2D}},                     /* frame rate, idle    */
    {ST_FRMCTR3, 6U,  {0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D}},   /* frame rate, partial */
    {ST_INVCTR,  1U,  {0x07}},                                 /* inversion control   */
    {ST_PWCTR1,  3U,  {0xA2, 0x02, 0x84}},
    {ST_PWCTR2,  1U,  {0xC5}},
    {ST_PWCTR3,  2U,  {0x0A, 0x00}},
    {ST_PWCTR4,  2U,  {0x8A, 0x2A}},
    {ST_PWCTR5,  2U,  {0x8A, 0xEE}},
    {ST_VMCTR1,  1U,  {0x0E}},
    {ST_INVON,   0U,  {0}},       /* this 160x80 panel needs inversion ON for true RGB565 */
    {ST_COLMOD,  1U,  {0x05}},    /* 16-bit RGB565 */
    {ST_MADCTL,  1U,  {0x78}},    /* landscape: MX+MV+ML+BGR */
    {ST_GMCTRP1, 16U, {0x02, 0x1C, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2D,
                       0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10}},
    {ST_GMCTRN1, 16U, {0x03, 0x1D, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D,
                       0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10}},
};

typedef void (*lcd_delay_fn_t)(uint32_t ms);

/* SLPOUT → full register table → NORON → DISPON. A zero delay skips the
 * wait. Same byte sequence on every path; only the waits differ. */
static void lcd_wake(lcd_delay_fn_t delay, uint32_t slpout_ms,
                     uint32_t noron_ms, uint32_t dispon_ms)
{
    lcd_cmd(ST_SLPOUT);
    if (slpout_ms) delay(slpout_ms);

    for (size_t i = 0; i < sizeof(s_panel_cfg) / sizeof(s_panel_cfg[0]); i++) {
        lcd_cmd(s_panel_cfg[i].cmd);
        if (s_panel_cfg[i].len) lcd_data(s_panel_cfg[i].data, s_panel_cfg[i].len);
    }

    lcd_cmd(ST_NORON);
    if (noron_ms) delay(noron_ms);
    lcd_cmd(ST_DISPON);
    if (dispon_ms) delay(dispon_ms);
}

void lcd_init(void)
{
    lcd_gpio_init();

    /* No software RST pin — LCD RST is board NRST (hardware reset on power-on).
     * Brief delay to let the panel stabilise after power-up. */
    lcd_delay_ms(120);

    /* Software reset (twice, as per WeAct BSP) */
    lcd_cmd(ST_SWRESET); lcd_delay_ms(120);
    lcd_cmd(ST_SWRESET); lcd_delay_ms(120);

    lcd_wake(lcd_delay_ms, 120U, 10U, 100U);
    /* Backlight is left off and GRAM is not cleared: the caller clears
     * the screen, then calls lcd_backlight_on(). */
}

void lcd_backlight_on(void)
{
    GPIOE_MODER = (GPIOE_MODER & ~(3U << 20)) | (1U << 20);
    BL_ON;
}

void lcd_backlight_off(void)
{
    BL_OFF;
}

/* ------------------------------------------------------------------ */
/* Drawing primitives                                                   */
/* ------------------------------------------------------------------ */

void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t colour)
{
    if (!w || !h) return;
    lcd_set_window(x, y, (uint16_t)(x + w - 1U), (uint16_t)(y + h - 1U));
    lcd_cmd(ST_RAMWR);

    /* Fill pixel data in 32-byte chunks (16 pixels) */
    uint8_t chunk[32];
    uint8_t hi = (uint8_t)(colour >> 8), lo = (uint8_t)(colour & 0xFFU);
    for (uint8_t i = 0; i < 32U; i += 2U) { chunk[i] = hi; chunk[i+1] = lo; }

    CS_LOW; DC_DATA;
    uint32_t total = (uint32_t)w * h * 2U;
    while (total >= 32U) { obsw_spi4_write(chunk, 32); total -= 32U; }
    if (total) obsw_spi4_write(chunk, (uint16_t)total);
    CS_HIGH;
}

void lcd_clear(uint16_t colour)
{
    lcd_fill_rect(0, 0, LCD_W, LCD_H, colour);
}

void lcd_fill_gram_raw(uint16_t colour)
{
    /* Fill all 162×132 GRAM cells — no offset, ignores MADCTL.
     * Use this to confirm the panel is alive: if ANY cell is visible,
     * the panel is in DISPON mode and the SPI link works. */
    uint8_t d[4];
    d[0]=0; d[1]=0; d[2]=0; d[3]=161U;
    lcd_cmd(ST_CASET); lcd_data(d, 4);
    d[0]=0; d[1]=0; d[2]=0; d[3]=131U;
    lcd_cmd(ST_RASET); lcd_data(d, 4);
    lcd_cmd(ST_RAMWR);

    uint8_t hi = (uint8_t)(colour >> 8), lo = (uint8_t)(colour & 0xFFU);
    uint8_t chunk[32];
    for (uint8_t i = 0; i < 32U; i += 2U) { chunk[i] = hi; chunk[i+1] = lo; }

    CS_LOW; DC_DATA;
    uint32_t total = 162U * 132U * 2U;   /* 42768 bytes */
    while (total >= 32U) { obsw_spi4_write(chunk, 32); total -= 32U; }
    if (total) obsw_spi4_write(chunk, (uint16_t)total);
    CS_HIGH;
}

void lcd_slpout_dispon(void)
{
    /* Full register restore without SWRESET (GRAM kept). Blocking DWT waits. */
    lcd_wake(lcd_delay_ms, 120U, 10U, 100U);
}

#ifdef OBSW_FREERTOS
#include "FreeRTOS.h"
#include "task.h"

static void lcd_task_delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

/* As lcd_slpout_dispon() but the CPU yields during the panel waits.
 * Task context only. */
void lcd_slpout_dispon_yield(void)
{
    lcd_wake(lcd_task_delay_ms, 120U, 10U, 100U);
}

/* Short re-wake: 10 ms after SLPOUT, no wait after NORON/DISPON. Used by the
 * optional OBSW_LCD_KEEPALIVE FDIR path. */
void lcd_slpout_dispon_fast(void)
{
    lcd_wake(lcd_task_delay_ms, 10U, 0U, 0U);
}
#endif /* OBSW_FREERTOS */

void lcd_draw_char(uint16_t x, uint16_t y, char ch, uint16_t fg, uint16_t bg)
{
    if ((uint8_t)ch < 0x20U || (uint8_t)ch > 0x7EU) ch = '?';
    const uint8_t *glyph = font5x7[(uint8_t)ch - 0x20U];

    /* Build 6×8 pixel block (5+1 cols, 7+1 rows) into a byte buffer */
    uint8_t buf[LCD_CHAR_W * LCD_CHAR_H * 2U];
    uint16_t idx = 0;

    for (uint8_t row = 0; row < LCD_CHAR_H; row++) {
        for (uint8_t col = 0; col < LCD_CHAR_W; col++) {
            uint16_t c = (col < 5U && row < 7U && (glyph[col] & (1U << row))) ? fg : bg;
            buf[idx++] = (uint8_t)(c >> 8);
            buf[idx++] = (uint8_t)(c & 0xFFU);
        }
    }

    lcd_set_window(x, y,
                   (uint16_t)(x + LCD_CHAR_W - 1U),
                   (uint16_t)(y + LCD_CHAR_H - 1U));
    lcd_cmd(ST_RAMWR);
    CS_LOW; DC_DATA;
    obsw_spi4_write(buf, sizeof(buf));
    CS_HIGH;
}

void lcd_draw_string(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg)
{
    while (*s) {
        if (x + LCD_CHAR_W > LCD_W) { x = 0; y += LCD_CHAR_H; }
        if (y + LCD_CHAR_H > LCD_H) break;
        lcd_draw_char(x, y, *s++, fg, bg);
        x += LCD_CHAR_W;
    }
}
