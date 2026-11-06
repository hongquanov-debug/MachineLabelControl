#include "vision_proto.h"

#include "crc16.h"

#define RESP_H1 0xAAU
#define RESP_H2 0x55U

size_t vision_build_trigger(uint16_t can_id, uint8_t out[VISION_TRIG_LEN])
{
    return vision_build_trigger_cmd(VISION_CMD_TRIGGER, can_id, out);
}

size_t vision_build_trigger_cmd(uint8_t cmd, uint16_t can_id, uint8_t out[VISION_TRIG_LEN])
{
    out[0] = 0x55U;
    out[1] = 0xAAU;
    out[2] = cmd;
    out[3] = (uint8_t)(can_id & 0xFFU);
    out[4] = (uint8_t)(can_id >> 8);
    uint16_t crc = crc16_modbus(out, 5);
    out[5] = (uint8_t)(crc & 0xFFU);
    out[6] = (uint8_t)(crc >> 8);
    out[7] = 0x0AU;
    return VISION_TRIG_LEN;
}

size_t vision_build_result(const VisionResult_t *res, uint8_t out[VISION_RESP_LEN])
{
    uint16_t th = (uint16_t)res->theta_x100;
    const bool code = (res->cmd == VISION_CMD_CODE_RESULT);
    out[0] = RESP_H1;
    out[1] = RESP_H2;
    out[2] = code ? VISION_CMD_CODE_RESULT : VISION_CMD_RESULT;
    out[3] = (uint8_t)(res->can_id & 0xFFU);
    out[4] = (uint8_t)(res->can_id >> 8);
    out[5] = code ? res->grade : (uint8_t)(th & 0xFFU);
    out[6] = code ? res->data_len : (uint8_t)(th >> 8);
    out[7] = res->quality;
    uint16_t crc = crc16_modbus(out, 8);
    out[8]  = (uint8_t)(crc & 0xFFU);
    out[9]  = (uint8_t)(crc >> 8);
    out[10] = 0x0DU;
    out[11] = 0x0AU;
    return VISION_RESP_LEN;
}

void vision_parser_init(VisionParser_t *p)
{
    p->len          = 0;
    p->crc_errors   = 0;
    p->frame_errors = 0;
}

static void resync(VisionParser_t *p, uint8_t byte)
{
    /* A failing byte may itself be the start of the next frame */
    if (byte == RESP_H1) {
        p->buf[0] = byte;
        p->len    = 1;
    } else {
        p->len = 0;
    }
}

bool vision_parser_feed(VisionParser_t *p, uint8_t byte, VisionResult_t *out)
{
    switch (p->len) {
    case 0:
        if (byte == RESP_H1) {
            p->buf[p->len++] = byte;
        }
        return false;
    case 1:
        if (byte == RESP_H2) {
            p->buf[p->len++] = byte;
        } else {
            resync(p, byte);
        }
        return false;
    case 2:
        if (byte == VISION_CMD_RESULT || byte == VISION_CMD_CODE_RESULT) {
            p->buf[p->len++] = byte;
        } else {
            p->frame_errors++;
            resync(p, byte);
        }
        return false;
    default:
        break;
    }

    p->buf[p->len++] = byte;
    if (p->len < VISION_RESP_LEN) {
        return false;
    }

    /* Complete frame */
    p->len = 0;
    const uint8_t *b = p->buf;
    if (b[10] != 0x0DU || b[11] != 0x0AU) {
        p->frame_errors++;
        return false;
    }
    uint16_t crc_rx = (uint16_t)(b[8] | ((uint16_t)b[9] << 8));
    if (crc16_modbus(b, 8) != crc_rx) {
        p->crc_errors++;
        return false;
    }
    out->cmd        = b[2];
    out->can_id     = (uint16_t)(b[3] | ((uint16_t)b[4] << 8));
    out->quality    = b[7];
    if (b[2] == VISION_CMD_CODE_RESULT) {
        out->theta_x100 = 0;
        out->grade      = b[5];
        out->data_len   = b[6];
    } else {
        out->theta_x100 = (int16_t)(uint16_t)(b[5] | ((uint16_t)b[6] << 8));
        out->grade      = 0;
        out->data_len   = 0;
    }
    return true;
}
