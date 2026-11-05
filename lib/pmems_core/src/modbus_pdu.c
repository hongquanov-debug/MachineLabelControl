#include "modbus_pdu.h"

#include <string.h>

#include "crc16.h"

static inline uint16_t rd16(const uint8_t *p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }
static inline void     wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

static size_t exception(uint8_t fc, uint8_t code, uint8_t *resp)
{
    resp[0] = (uint8_t)(fc | 0x80U);
    resp[1] = code;
    return 2;
}

size_t mb_pdu_process(const MbMap_t *map, const uint8_t *req, size_t req_len,
                      uint8_t *resp, size_t resp_cap)
{
    if (req_len < 1 || resp_cap < 5) {
        return 0;
    }
    const uint8_t fc = req[0];

    switch (fc) {
    case MB_FC_READ_HOLDING:
    case MB_FC_READ_INPUT: {
        if (req_len != 5) {
            return exception(fc, MB_EX_ILLEGAL_VALUE, resp);
        }
        uint16_t addr = rd16(&req[1]);
        uint16_t qty  = rd16(&req[3]);
        uint16_t limit = (fc == MB_FC_READ_HOLDING) ? map->holding_count : map->input_count;
        if (qty < 1 || qty > 125 || (size_t)(2 + 2U * qty) > resp_cap) {
            return exception(fc, MB_EX_ILLEGAL_VALUE, resp);
        }
        if ((uint32_t)addr + qty > limit) {
            return exception(fc, MB_EX_ILLEGAL_ADDRESS, resp);
        }
        resp[0] = fc;
        resp[1] = (uint8_t)(qty * 2U);
        for (uint16_t i = 0; i < qty; i++) {
            uint16_t v  = 0;
            uint8_t  ex = (fc == MB_FC_READ_HOLDING) ? map->read_holding(map->ctx, (uint16_t)(addr + i), &v)
                                                     : map->read_input(map->ctx, (uint16_t)(addr + i), &v);
            if (ex != MB_EX_NONE) {
                return exception(fc, ex, resp);
            }
            wr16(&resp[2 + 2U * i], v);
        }
        return (size_t)(2U + 2U * qty);
    }

    case MB_FC_WRITE_SINGLE: {
        if (req_len != 5) {
            return exception(fc, MB_EX_ILLEGAL_VALUE, resp);
        }
        uint16_t addr = rd16(&req[1]);
        if (addr >= map->holding_count) {
            return exception(fc, MB_EX_ILLEGAL_ADDRESS, resp);
        }
        uint8_t ex = map->write_holding(map->ctx, addr, rd16(&req[3]));
        if (ex != MB_EX_NONE) {
            return exception(fc, ex, resp);
        }
        memcpy(resp, req, 5); /* echo */
        return 5;
    }

    case MB_FC_WRITE_MULTIPLE: {
        if (req_len < 6) {
            return exception(fc, MB_EX_ILLEGAL_VALUE, resp);
        }
        uint16_t addr  = rd16(&req[1]);
        uint16_t qty   = rd16(&req[3]);
        uint8_t  bytes = req[5];
        if (qty < 1 || qty > 123 || bytes != qty * 2U || req_len != (size_t)(6U + bytes)) {
            return exception(fc, MB_EX_ILLEGAL_VALUE, resp);
        }
        if ((uint32_t)addr + qty > map->holding_count) {
            return exception(fc, MB_EX_ILLEGAL_ADDRESS, resp);
        }
        for (uint16_t i = 0; i < qty; i++) {
            uint8_t ex = map->write_holding(map->ctx, (uint16_t)(addr + i), rd16(&req[6 + 2U * i]));
            if (ex != MB_EX_NONE) {
                return exception(fc, ex, resp);
            }
        }
        resp[0] = fc;
        wr16(&resp[1], addr);
        wr16(&resp[3], qty);
        return 5;
    }

    default:
        return exception(fc, MB_EX_ILLEGAL_FUNCTION, resp);
    }
}

bool mb_rtu_crc_ok(const uint8_t *frame, size_t len)
{
    if (len < 4) {
        return false;
    }
    uint16_t crc = crc16_modbus(frame, len - 2);
    return frame[len - 2] == (uint8_t)crc && frame[len - 1] == (uint8_t)(crc >> 8);
}

static size_t rtu_append_crc(uint8_t *frame, size_t len)
{
    uint16_t crc = crc16_modbus(frame, len);
    frame[len]     = (uint8_t)crc;
    frame[len + 1] = (uint8_t)(crc >> 8);
    return len + 2;
}

size_t mb_rtu_slave_process(const MbMap_t *map, uint8_t slave_id,
                            const uint8_t *frame, size_t len,
                            uint8_t *resp, size_t resp_cap)
{
    if (len < 4 || resp_cap < 8 || !mb_rtu_crc_ok(frame, len)) {
        return 0;
    }
    const uint8_t addr = frame[0];
    if (addr != slave_id && addr != 0U) {
        return 0;
    }
    size_t pdu_len = mb_pdu_process(map, &frame[1], len - 3, &resp[1], resp_cap - 3);
    if (addr == 0U || pdu_len == 0) {
        return 0; /* broadcast: no reply */
    }
    resp[0] = slave_id;
    return rtu_append_crc(resp, pdu_len + 1);
}

size_t mb_tcp_process(const MbMap_t *map, const uint8_t *adu, size_t len,
                      uint8_t *resp, size_t resp_cap)
{
    /* MBAP: TID(2) PID(2) LEN(2) UID(1) */
    if (len < 8 || resp_cap < 9) {
        return 0;
    }
    uint16_t pid = rd16(&adu[2]);
    uint16_t mlen = rd16(&adu[4]);
    if (pid != 0U || mlen < 2 || (size_t)mlen + 6U != len) {
        return 0;
    }
    size_t pdu_len = mb_pdu_process(map, &adu[7], (size_t)mlen - 1U, &resp[7], resp_cap - 7);
    if (pdu_len == 0) {
        return 0;
    }
    memcpy(resp, adu, 4);           /* TID + PID */
    wr16(&resp[4], (uint16_t)(pdu_len + 1U));
    resp[6] = adu[6];               /* Unit id */
    return pdu_len + 7U;
}

size_t mb_rtu_build_read(uint8_t slave, uint8_t fc, uint16_t addr, uint16_t qty, uint8_t *out)
{
    out[0] = slave;
    out[1] = fc;
    wr16(&out[2], addr);
    wr16(&out[4], qty);
    return rtu_append_crc(out, 6);
}

size_t mb_rtu_build_write_single(uint8_t slave, uint16_t addr, uint16_t val, uint8_t *out)
{
    out[0] = slave;
    out[1] = MB_FC_WRITE_SINGLE;
    wr16(&out[2], addr);
    wr16(&out[4], val);
    return rtu_append_crc(out, 6);
}

int mb_rtu_parse_read_resp(uint8_t slave, uint8_t fc, const uint8_t *frame, size_t len,
                           uint16_t *regs, uint16_t qty)
{
    if (len < 5 || !mb_rtu_crc_ok(frame, len) || frame[0] != slave) {
        return -1;
    }
    if (frame[1] == (uint8_t)(fc | 0x80U)) {
        return frame[2];
    }
    if (frame[1] != fc || frame[2] != qty * 2U || len != (size_t)(5U + qty * 2U)) {
        return -2;
    }
    for (uint16_t i = 0; i < qty; i++) {
        regs[i] = rd16(&frame[3 + 2U * i]);
    }
    return 0;
}

int mb_rtu_parse_write_single_resp(uint8_t slave, uint16_t addr, uint16_t val,
                                   const uint8_t *frame, size_t len)
{
    if (len < 5 || !mb_rtu_crc_ok(frame, len) || frame[0] != slave) {
        return -1;
    }
    if (frame[1] == (uint8_t)(MB_FC_WRITE_SINGLE | 0x80U)) {
        return frame[2];
    }
    if (len != 8 || frame[1] != MB_FC_WRITE_SINGLE || rd16(&frame[2]) != addr || rd16(&frame[4]) != val) {
        return -2;
    }
    return 0;
}
