/**
 * @file src/hal/stm32h7/lcd_console.c
 * @brief Scrolling text console for the ST7735R LCD (160×80).
 *
 * State: a static LCD_ROWS × LCD_COLS character buffer mirrors exactly
 * what is on screen. On scroll, the buffer shifts up one row and the
 * last row is blanked; the screen is then redrawn from the buffer.
 *
 * Thread safety: taskENTER/EXIT_CRITICAL wraps each individual character
 * draw (~200 µs at 4 MHz SPI). Scroll redraws happen as a series of
 * individually-locked character writes so interrupts are not blocked for
 * the full redraw duration (~62 ms for 260 chars). Works correctly both
 * before and after the FreeRTOS scheduler starts.
 */

#include "obsw/hal/stm32h7/lcd_console.h"
#include "obsw/hal/stm32h7/lcd.h"
#include <string.h>

#ifdef OBSW_FREERTOS
#  include "FreeRTOS.h"
#  include "task.h"
#  define CON_LOCK()   taskENTER_CRITICAL()
#  define CON_UNLOCK() taskEXIT_CRITICAL()
#else
#  define CON_LOCK()   ((void)0)
#  define CON_UNLOCK() ((void)0)
#endif

/* ------------------------------------------------------------------ */
/* State                                                                */
/* ------------------------------------------------------------------ */

static char     g_buf[LCD_ROWS][LCD_COLS];
static uint8_t  g_row;
static uint8_t  g_col;
static uint16_t g_fg;
static uint16_t g_bg;

/* ------------------------------------------------------------------ */
/* Internal helpers                                                     */
/* ------------------------------------------------------------------ */

static void draw_char_locked(uint8_t col, uint8_t row, char ch)
{
    CON_LOCK();
    lcd_draw_char((uint16_t)(col * LCD_CHAR_W),
                  (uint16_t)(row * LCD_CHAR_H),
                  ch, g_fg, g_bg);
    CON_UNLOCK();
}

static void console_scroll(void)
{
    /* Shift buffer up one row, blank the last row */
    memmove(g_buf[0], g_buf[1], (LCD_ROWS - 1U) * LCD_COLS);
    memset(g_buf[LCD_ROWS - 1U], ' ', LCD_COLS);

    /* Redraw from buffer — each char individually locked */
    for (uint8_t r = 0; r < LCD_ROWS; r++)
        for (uint8_t c = 0; c < LCD_COLS; c++)
            draw_char_locked(c, r, g_buf[r][c]);

    g_row = LCD_ROWS - 1U;
    g_col = 0;
}

static void console_putchar(char ch)
{
    if (ch == '\r') {
        g_col = 0;
        return;
    }

    if (ch == '\n') {
        /* Pad the rest of the current row with spaces */
        while (g_col < LCD_COLS) {
            g_buf[g_row][g_col] = ' ';
            draw_char_locked(g_col, g_row, ' ');
            g_col++;
        }
        g_col = 0;
        g_row++;
        if (g_row >= LCD_ROWS)
            console_scroll();
        return;
    }

    /* Printable character */
    if (g_col >= LCD_COLS) {
        g_col = 0;
        g_row++;
        if (g_row >= LCD_ROWS)
            console_scroll();
    }

    g_buf[g_row][g_col] = ch;
    draw_char_locked(g_col, g_row, ch);
    g_col++;
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

void lcd_console_init(void)
{
    g_fg  = LCD_GREEN;
    g_bg  = LCD_BLACK;
    g_row = 0;
    g_col = 0;
    memset(g_buf, ' ', sizeof(g_buf));
    lcd_clear(g_bg);
}

void lcd_console_clear(void)
{
    g_row = 0;
    g_col = 0;
    memset(g_buf, ' ', sizeof(g_buf));
    CON_LOCK();
    lcd_clear(g_bg);
    CON_UNLOCK();
}

void lcd_console_set_colours(uint16_t fg, uint16_t bg)
{
    g_fg = fg;
    g_bg = bg;
}

void lcd_console_puts(const char *s)
{
    while (*s)
        console_putchar(*s++);
}
