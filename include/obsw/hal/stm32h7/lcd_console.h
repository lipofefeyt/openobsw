#ifndef OBSW_HAL_STM32H7_LCD_CONSOLE_H
#define OBSW_HAL_STM32H7_LCD_CONSOLE_H

#include <stdint.h>
#include "obsw/hal/stm32h7/lcd.h"

/**
 * @file obsw/hal/stm32h7/lcd_console.h
 * @brief Scrolling text console on the ST7735R LCD.
 *
 * Capacity: LCD_COLS × LCD_ROWS characters (26 × 10 at 6×8 px/cell).
 * Handles \\n (newline) and \\r (carriage return). Auto-scrolls when the
 * cursor reaches the bottom row. Thread-safe via taskENTER/EXIT_CRITICAL
 * per character — safe both before and after the FreeRTOS scheduler starts.
 *
 * Call obsw_spi4_init() and lcd_init() before lcd_console_init().
 */

void lcd_console_init(void);
void lcd_console_clear(void);
void lcd_console_puts(const char *s);
void lcd_console_set_colours(uint16_t fg, uint16_t bg);

/* Row LCD_ROWS-1 (row 9) is a fixed status bar — never scrolled over.
 * The string is padded / truncated to exactly LCD_COLS characters. */
void lcd_console_set_status(const char *s, uint16_t fg, uint16_t bg);

#endif /* OBSW_HAL_STM32H7_LCD_CONSOLE_H */
