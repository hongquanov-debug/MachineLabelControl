/**
 * @file vision_proto.h
 * @brief Binary protocol MCU <-> Vision PC on RS485_3 (Spec §2.2).
 *
 * Multi-byte fields are Little-Endian. CRC16 Modbus is transmitted low byte first.
 */
#ifndef VISION_PROTO_H
#define VISION_PROTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VISION_TRIG_LEN       8U
#define VISION_RESP_LEN       12U
#define VISION_CMD_TRIGGER    0x10U  /* CMD_TRIGGER_INSPECT */
#define VISION_CMD_RESULT     0x20U  /* CMD_VISION_RESULT  */

typedef enum {
    VISION_Q_OK         = 0x00,
    VISION_Q_NO_TAB     = 0x01,
    VISION_Q_DEFECT     = 0x02,
    VISION_Q_WRONG_TYPE = 0x03
} VisionQuality_t;

typedef struct {
    uint16_t can_id;
    int16_t  theta_x100;
    uint8_t  quality;
} VisionResult_t;

typedef struct {
    uint8_t  buf[VISION_RESP_LEN];
    uint8_t  len;
    uint32_t crc_errors;
    uint32_t frame_errors;
} VisionParser_t;

/** Build "Trigger Request" frame (Spec §2.2.A). Returns frame length (8). */
size_t vision_build_trigger(uint16_t can_id, uint8_t out[VISION_TRIG_LEN]);

/** Build "Vision Response" frame (§2.2.B) - used by tests / simulators. */
size_t vision_build_result(const VisionResult_t *res, uint8_t out[VISION_RESP_LEN]);

void vision_parser_init(VisionParser_t *p);

/**
 * Feed one received byte. Returns true when a complete, CRC-valid response
 * frame was decoded into @p out. Resynchronises automatically on errors.
 */
bool vision_parser_feed(VisionParser_t *p, uint8_t byte, VisionResult_t *out);

#ifdef __cplusplus
}
#endif

#endif /* VISION_PROTO_H */
