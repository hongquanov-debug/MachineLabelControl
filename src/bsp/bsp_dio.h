/**
 * @file bsp_dio.h
 * @brief Module_LI32 (4x 74HCT165) and Module_LO24 (3x 74HCT595 + ULN2803A), Spec §5.
 *
 * DI: 40 bits are shifted in. Bytes 0..3 = 32 inputs, byte 4 ("5th byte") is the
 *     serial input of the last 74HCT165 (tied low on the module). Reading 0xFF
 *     there means the IDC-10 cable is disconnected (ALM_20).
 * DO: 24-bit output image, committed atomically (RCLK latch).
 *
 * Bit numbering: index = byte * 8 + bit, matching the tables of Spec §5.
 */
#ifndef BSP_DIO_H
#define BSP_DIO_H

#include <stdbool.h>
#include <stdint.h>

#include "app_config.h"   /* DIO_DI_EXT_BYTES */

#define DIO_DI_COUNT 32U
#define DIO_DO_COUNT 24U

/** @p initial_do = output image latched before /OE is enabled (safe state). */
void bsp_dio_init(uint32_t initial_do);

/**
 * Shift in all inputs.
 *  @p raw   = bytes 0..3 (LI32, bit index layout)
 *  @p ext   = extension byte (safety on PCB v1), 0 if DIO_DI_EXT_BYTES == 0
 *  @p check = byte after the last chip (SER tied low): 0x00 = OK, 0xFF = cable off
 */
void bsp_dio_scan(uint32_t *raw, uint8_t *ext, uint8_t *check);

/** Update one output in the image (ISR safe). Takes effect at next commit. */
void bsp_dio_do_write(uint8_t index, bool on);
bool bsp_dio_do_get(uint8_t index);
uint32_t bsp_dio_do_image(void);

/** Shift the output image to the 74HCT595 chain and latch it (task/ISR safe). */
void bsp_dio_do_commit(void);

/** Clear all outputs and commit immediately. */
void bsp_dio_do_all_off(void);

#endif /* BSP_DIO_H */
