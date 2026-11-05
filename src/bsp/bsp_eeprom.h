/**
 * @file bsp_eeprom.h
 * @brief AT24C64 (8 KB, 32-byte pages) on I2C1 PB8/PB9, address 0xA0 (Spec §8).
 *        Thread-safe (mutex). Call from task context only.
 */
#ifndef BSP_EEPROM_H
#define BSP_EEPROM_H

#include <stdbool.h>
#include <stdint.h>

bool bsp_eeprom_init(void);
bool bsp_eeprom_read(uint16_t addr, void *buf, uint16_t len);
bool bsp_eeprom_write(uint16_t addr, const void *buf, uint16_t len);

#endif
