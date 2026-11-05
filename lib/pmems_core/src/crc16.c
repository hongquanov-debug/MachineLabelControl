#include "crc16.h"

uint16_t crc16_modbus_update(uint16_t crc, const uint8_t *data, size_t len)
{
    while (len--) {
        crc ^= *data++;
        for (int i = 0; i < 8; i++) {
            crc = (crc & 1U) ? (uint16_t)((crc >> 1) ^ 0xA001U) : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

uint16_t crc16_modbus(const uint8_t *data, size_t len)
{
    return crc16_modbus_update(0xFFFFU, data, len);
}
