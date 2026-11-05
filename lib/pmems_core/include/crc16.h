/**
 * @file crc16.h
 * @brief CRC16 Modbus (poly 0xA001 reflected, init 0xFFFF).
 *        Used by: Vision protocol (Spec §2.2), Modbus RTU (§7), Recipe (§8).
 */
#ifndef CRC16_H
#define CRC16_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint16_t crc16_modbus(const uint8_t *data, size_t len);

/** Continue a CRC over additional data (start with crc = 0xFFFF). */
uint16_t crc16_modbus_update(uint16_t crc, const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* CRC16_H */
