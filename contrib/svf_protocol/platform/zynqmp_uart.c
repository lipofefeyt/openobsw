/**
 * @file zynqmp_uart.c
 * @brief SVF I/O implementation for ZynqMP Cadence UART.
 *
 * Use this when building obsw_zynqmp (bare-metal, Renode socket mode).
 * SVF connects via TCP socket to Renode uart0 HostInterface.
 */

#include "../obsw_svf_protocol.h"
#include <stdint.h>

/* Cadence UART base address (uart0) */
#define XUARTPS_BASE        0xFF000000UL
#define XUARTPS_SR_OFFSET   0x2CU
#define XUARTPS_FIFO_OFFSET 0x30U
#define XUARTPS_SR_TXFULL   0x00000010U
#define XUARTPS_SR_RXEMPTY  0x00000002U

static inline uint32_t uart_read_reg(uint32_t offset)
{
    return *(volatile uint32_t *)(XUARTPS_BASE + offset);
}

static inline void uart_write_reg(uint32_t offset, uint32_t val)
{
    *(volatile uint32_t *)(XUARTPS_BASE + offset) = val;
}

int svf_io_read(uint8_t *buf, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        while (uart_read_reg(XUARTPS_SR_OFFSET) & XUARTPS_SR_RXEMPTY)
            ;
        buf[i] = (uint8_t)(uart_read_reg(XUARTPS_FIFO_OFFSET) & 0xFFU);
    }
    return 0;
}

int svf_io_write(const uint8_t *buf, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        while (uart_read_reg(XUARTPS_SR_OFFSET) & XUARTPS_SR_TXFULL)
            ;
        uart_write_reg(XUARTPS_FIFO_OFFSET, (uint32_t)buf[i]);
    }
    return 0;
}

void svf_io_flush(void)
{
    /* Cadence UART has no explicit flush — TX FIFO drains automatically */
}
