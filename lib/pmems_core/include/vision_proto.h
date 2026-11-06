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
#define VISION_CMD_TRIGGER       0x10U  /* camera 1 (tab angle): CMD_TRIGGER_INSPECT */
#define VISION_CMD_RESULT        0x20U  /* camera 1 result: CMD_VISION_RESULT */
/* --- extension: camera 2 (DataMatrix check after labeling), same 8/12-byte frames --- */
#define VISION_CMD_TRIGGER_CODE  0x11U  /* camera 2 trigger request (CAN_ID) */
#define VISION_CMD_CODE_RESULT   0x21U  /* camera 2 result: byte5 = grade, byte6 = data length */

typedef enum {
    VISION_Q_OK         = 0x00,
    VISION_Q_NO_TAB     = 0x01,
    VISION_Q_DEFECT     = 0x02,
    VISION_Q_WRONG_TYPE = 0x03
} VisionQuality_t;

/* QUALITY_FLAG of a 0x21 (DataMatrix) result */
typedef enum {
    CODE_Q_OK          = 0x00,  /* code found, decoded and content accepted by the PC */
    CODE_Q_NO_CODE     = 0x10,  /* no label / no DataMatrix in the image */
    CODE_Q_UNREADABLE  = 0x11,  /* code found but not decodable */
    CODE_Q_INVALID     = 0x12,  /* decoded, content rejected (format / wrong product) */
    CODE_Q_DUPLICATE   = 0x13,  /* decoded, already seen (duplicate serial) */
    CODE_Q_LOW_GRADE   = 0x14,  /* decoded, print quality grade below the limit */
} CodeQuality_t;

typedef struct {
    uint8_t  cmd;          /* VISION_CMD_RESULT (camera 1) or VISION_CMD_CODE_RESULT (camera 2) */
    uint16_t can_id;
    int16_t  theta_x100;   /* camera 1 only */
    uint8_t  quality;      /* VisionQuality_t or CodeQuality_t */
    uint8_t  grade;        /* camera 2: ISO/IEC 15415 grade 4=A .. 0=F */
    uint8_t  data_len;     /* camera 2: decoded data length (informative) */
} VisionResult_t;

typedef struct {
    uint8_t  buf[VISION_RESP_LEN];
    uint8_t  len;
    uint32_t crc_errors;
    uint32_t frame_errors;
} VisionParser_t;

/** Build "Trigger Request" frame (Spec §2.2.A). Returns frame length (8). */
size_t vision_build_trigger(uint16_t can_id, uint8_t out[VISION_TRIG_LEN]);

/** Same frame with an explicit command (0x10 camera 1, 0x11 camera 2). */
size_t vision_build_trigger_cmd(uint8_t cmd, uint16_t can_id, uint8_t out[VISION_TRIG_LEN]);

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
