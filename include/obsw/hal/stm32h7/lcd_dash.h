#ifndef OBSW_HAL_STM32H7_LCD_DASH_H
#define OBSW_HAL_STM32H7_LCD_DASH_H

/**
 * @file obsw/hal/stm32h7/lcd_dash.h
 * @brief Renders an obsw_dash_frame_t on the ST7735R LCD, redrawing only
 *        the character cells that changed since the previous frame.
 *
 * Single owner: only the FDIR task may call these functions (or any other
 * lcd_* function) once the scheduler is running. No locking is done.
 *
 * Call obsw_spi4_init() and lcd_init() before the first lcd_dash_render().
 */

#include "obsw/ui/dashboard.h"

/** Draw @p frame. The first call clears the panel and draws every cell. */
void lcd_dash_render(const obsw_dash_frame_t *frame);

/**
 * Force the next lcd_dash_render() to redraw every cell (without a full
 * clear). Use as a self-heal in case panel GRAM was lost.
 */
void lcd_dash_invalidate(void);

#endif /* OBSW_HAL_STM32H7_LCD_DASH_H */
