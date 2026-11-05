/**
 * @file modbus_pdu.h
 * @brief Transport-independent Modbus helpers (Spec §7).
 *
 *  - Slave side : PDU processing for FC03/04/06/16, wrapped for RTU (HMI, §7.1)
 *                 and TCP/MBAP (SCADA via W5500, §7.3) using the same register map.
 *  - Master side: RTU frame builders/parsers for the VFD M1 (§7.2).
 */
#ifndef MODBUS_PDU_H
#define MODBUS_PDU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MB_FC_READ_HOLDING     0x03U
#define MB_FC_READ_INPUT       0x04U
#define MB_FC_WRITE_SINGLE     0x06U
#define MB_FC_WRITE_MULTIPLE   0x10U

#define MB_EX_NONE             0x00U
#define MB_EX_ILLEGAL_FUNCTION 0x01U
#define MB_EX_ILLEGAL_ADDRESS  0x02U
#define MB_EX_ILLEGAL_VALUE    0x03U
#define MB_EX_DEVICE_FAILURE   0x04U

#define MB_RTU_MAX_ADU         256U
#define MB_TCP_MAX_ADU         260U

/** Register map callbacks. Addresses are 0-based (40001 -> 0, 30001 -> 0). */
typedef struct {
    uint16_t holding_count;
    uint16_t input_count;
    uint8_t (*read_holding)(void *ctx, uint16_t addr, uint16_t *val);
    uint8_t (*write_holding)(void *ctx, uint16_t addr, uint16_t val);
    uint8_t (*read_input)(void *ctx, uint16_t addr, uint16_t *val);
    void *ctx;
} MbMap_t;

/** Process request PDU (function code + data). Returns response PDU length. */
size_t mb_pdu_process(const MbMap_t *map, const uint8_t *req, size_t req_len,
                      uint8_t *resp, size_t resp_cap);

bool mb_rtu_crc_ok(const uint8_t *frame, size_t len);

/** Slave RTU: returns response length, 0 if no reply must be sent. */
size_t mb_rtu_slave_process(const MbMap_t *map, uint8_t slave_id,
                            const uint8_t *frame, size_t len,
                            uint8_t *resp, size_t resp_cap);

/** Modbus TCP: process one ADU (MBAP + PDU). Returns response length or 0. */
size_t mb_tcp_process(const MbMap_t *map, const uint8_t *adu, size_t len,
                      uint8_t *resp, size_t resp_cap);

/* ---- Master (RTU) ---- */
size_t mb_rtu_build_read(uint8_t slave, uint8_t fc, uint16_t addr, uint16_t qty, uint8_t *out);
size_t mb_rtu_build_write_single(uint8_t slave, uint16_t addr, uint16_t val, uint8_t *out);

/**
 * Parse read response. Returns 0 on success, >0 Modbus exception code,
 * <0 on framing / CRC / length error.
 */
int mb_rtu_parse_read_resp(uint8_t slave, uint8_t fc, const uint8_t *frame, size_t len,
                           uint16_t *regs, uint16_t qty);

/** Parse FC06 echo response. Same return convention. */
int mb_rtu_parse_write_single_resp(uint8_t slave, uint16_t addr, uint16_t val,
                                   const uint8_t *frame, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_PDU_H */
