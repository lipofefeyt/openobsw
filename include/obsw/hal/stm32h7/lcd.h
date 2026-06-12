#ifndef OBSW_HAL_STM32H7_LCD_H
#define OBSW_HAL_STM32H7_LCD_H

#include <stdint.h>

/**
 * @file obsw/hal/stm32h7/lcd.h
 * @brief ST7735R LCD driver for WeAct STM32H750 (160×80, RGB565).
 *
 * Pins (GPIOE, GPIO outputs):
 *   CS  PE11  (active low)
 *   DC  PE13  (HIGH=data, LOW=command)
 *   BL  PE10  (HIGH=backlight on)
 *
 * LCD RST = board NRST (hardware reset only — not a software GPIO).
 * PE3 is the USER LED; this driver does not drive it.
 *
 * Call obsw_spi4_init() before lcd_init().
 */

/* Display geometry */
#define LCD_W   160U
#define LCD_H    80U

/* RGB565 colour helpers */
#define LCD_RGB(r, g, b) \
    ((uint16_t)(((r) & 0x1FU) << 11) | (uint16_t)(((g) & 0x3FU) << 5) | ((b) & 0x1FU))

#define LCD_BLACK   0x0000U
#define LCD_WHITE   0xFFFFU
#define LCD_RED     0xF800U
#define LCD_GREEN   0x07E0U
#define LCD_BLUE    0x001FU
#define LCD_YELLOW  0xFFE0U
#define LCD_CYAN    0x07FFU
#define LCD_MAGENTA 0xF81FU
#define LCD_ORANGE  0xFD20U
#define LCD_GREY    0x7BEFU

/* Character cell size (5×7 font + 1 px spacing each side) */
#define LCD_CHAR_W  6U
#define LCD_CHAR_H  8U
#define LCD_COLS   (LCD_W / LCD_CHAR_W)   /* 26 chars/line */
#define LCD_ROWS   (LCD_H / LCD_CHAR_H)   /* 10 lines       */

/* lcd_init() configures the ST7735R but does NOT enable the backlight.
 * Call lcd_backlight_on() once after the first lcd_init() from main().
 * FDIR re-init calls lcd_init() without touching BL to avoid the current
 * spike that would re-trigger the ST7735R internal power supervisor. */
void lcd_init(void);
void lcd_backlight_on(void);
void lcd_backlight_off(void);
void lcd_clear(uint16_t colour);
void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t colour);
void lcd_draw_char(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg);
void lcd_draw_string(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg);
/* Diagnostic: fill entire 162×132 GRAM (no offset) — confirms DISPON + SPI */
void lcd_fill_gram_raw(uint16_t colour);
/* Re-issue SLPOUT+DISPON in case the panel lost its display-on state */
void lcd_slpout_dispon(void);

#ifdef OBSW_FREERTOS
/* Like lcd_slpout_dispon() but uses vTaskDelay for the mandatory panel
 * delays (120+10+100 ms) so the CPU enters WFI instead of DWT-spinning.
 * Lower system current during init keeps the supply above the ST7735R
 * power-supervisor threshold.  Must be called from a FreeRTOS task. */
void lcd_slpout_dispon_yield(void);
#endif

#endif /* OBSW_HAL_STM32H7_LCD_H */
