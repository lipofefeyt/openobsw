/**
 * @file src/hal/stm32h7/lcd_dash.c
 * @brief Diff renderer for the status dashboard on the ST7735R LCD.
 *
 * Keeps a shadow copy of what is on the panel. Each render redraws only the
 * cells whose character changed, or the whole row when its style changed.
 * A typical 1 s update touches a few dozen cells (OBT, uptime, WDG digits)
 * instead of all 260, which keeps SPI bursts short and avoids flicker.
 *
 * The grid is 26 x 6 px = 156 px wide; the 4 px strip at the right edge is
 * filled with the row background so bars span the full 160 px.
 */

#include "obsw/hal/stm32h7/lcd_dash.h"

#include "obsw/hal/stm32h7/lcd.h"

#include <stdbool.h>
#include <stdint.h>

#define GRID_W_PX ((uint16_t)(OBSW_DASH_COLS * LCD_CHAR_W))
#define STRIP_W_PX ((uint16_t)(LCD_W - GRID_W_PX))

static obsw_dash_frame_t s_shadow;
static bool s_drawn;   /* panel matches s_shadow */
static bool s_cleared; /* first full clear done */

typedef struct {
    uint16_t fg;
    uint16_t bg;
} colours_t;

static colours_t style_colours(obsw_dash_style_t style)
{
    colours_t c = {LCD_WHITE, LCD_BLACK};
    switch (style) {
    case OBSW_DASH_STYLE_DIM:
        c.fg = LCD_GREY;
        break;
    case OBSW_DASH_STYLE_WARN:
        c.fg = LCD_YELLOW;
        break;
    case OBSW_DASH_STYLE_ALARM:
        c.fg = LCD_RED;
        break;
    case OBSW_DASH_STYLE_TITLE:
        c.fg = LCD_BLACK;
        c.bg = LCD_CYAN;
        break;
    case OBSW_DASH_STYLE_BAR_OK:
        c.fg = LCD_BLACK;
        c.bg = LCD_GREEN;
        break;
    case OBSW_DASH_STYLE_BAR_WARN:
        c.fg = LCD_BLACK;
        c.bg = LCD_YELLOW;
        break;
    case OBSW_DASH_STYLE_BAR_ALARM:
        c.fg = LCD_WHITE;
        c.bg = LCD_RED;
        break;
    case OBSW_DASH_STYLE_NORMAL:
    default:
        break;
    }
    return c;
}

void lcd_dash_invalidate(void)
{
    s_drawn = false;
}

void lcd_dash_render(const obsw_dash_frame_t *frame)
{
    if (!frame)
        return;

    if (!s_cleared) {
        /* Known background before the first diff (cheap, once) */
        lcd_clear(LCD_BLACK);
        s_cleared = true;
        s_drawn   = false;
    }

    for (uint8_t r = 0; r < OBSW_DASH_ROWS; r++) {
        bool row_dirty = !s_drawn || (frame->style[r] != s_shadow.style[r]);
        colours_t c    = style_colours(frame->style[r]);
        uint16_t y     = (uint16_t)(r * LCD_CHAR_H);

        for (uint8_t col = 0; col < OBSW_DASH_COLS; col++) {
            char ch = frame->text[r][col];
            if (row_dirty || ch != s_shadow.text[r][col]) {
                lcd_draw_char((uint16_t)(col * LCD_CHAR_W), y, ch, c.fg, c.bg);
                s_shadow.text[r][col] = ch;
            }
        }
        if (row_dirty) {
            lcd_fill_rect(GRID_W_PX, y, STRIP_W_PX, LCD_CHAR_H, c.bg);
            s_shadow.style[r] = frame->style[r];
        }
    }
    s_drawn = true;
}
