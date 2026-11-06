/**
 * @file can_record.h
 * @brief Can tracking data types (Spec §1.3) + tracking FIFO (max 32 cans).
 */
#ifndef CAN_RECORD_H
#define CAN_RECORD_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAN_FIFO_CAPACITY 32U

typedef enum {
    CAN_STATUS_INIT        = 0x00,
    CAN_STATUS_VISION_WAIT = 0x01,
    CAN_STATUS_VISION_OK   = 0x02,
    CAN_STATUS_ORIENTED    = 0x04,
    CAN_STATUS_LABELED     = 0x08,
    CAN_STATUS_INSPECTED   = 0x10,
    CAN_STATUS_REJECTED    = 0x20,
    CAN_STATUS_PASSED      = 0x40,
    /* --- extension: camera 2 DataMatrix check (not in spec REV 3.2) --- */
    CAN_STATUS_CODE_WAIT   = 0x11,  /* camera 2 triggered, waiting for 0x21 result */
    CAN_STATUS_CODE_OK     = 0x12,  /* DataMatrix read and accepted */
    /* --- */
    CAN_ERROR_VISION_NG    = 0x80,
    CAN_ERROR_VACUUM_MISS  = 0x81,
    CAN_ERROR_LABEL_MISS   = 0x82,
    CAN_ERROR_CODE_NG      = 0x83   /* extension: DataMatrix missing / unreadable / rejected */
} CanStatus_t;

/* Extension flags (not in spec REV 3.2) */
#define CAN_FLAG_HOLD        0x01U  /**< Can physically held at a station (P3) - position frozen */
#define CAN_FLAG_VISION_TMO  0x02U  /**< NG caused by Vision timeout (counts for ALM_05) */
#define CAN_FLAG_CODE_TMO    0x04U  /**< NG caused by camera 2 timeout (counts for ALM_09) */

typedef struct {
    uint16_t    can_id;            /**< Incrementing id (1 .. 65535) */
    uint32_t    enc_capture_s2;    /**< TIM2->CNT captured in EXTI2 (S2) */
    float       theta_degree;      /**< Tab angle from Vision (-180 .. +180 deg) */
    uint8_t     station_idx;       /**< Next station to be processed (0 .. 6) */
    CanStatus_t status;            /**< Current status */
    uint32_t    timestamp_enter;   /**< Tick when can passed S2 */
    /* ---- Extensions (not in spec REV 3.2, required by implementation) ---- */
    uint32_t    timestamp_trig;    /**< Tick of CAM_TRIG / CAM2_TRIG pulse (timeout reference) */
    uint8_t     flags;             /**< CAN_FLAG_x */
} CanRecord_t;

static inline bool can_status_is_error(CanStatus_t s)
{
    return (uint8_t)s >= (uint8_t)CAN_ERROR_VISION_NG;
}

/** Signed encoder distance travelled since S2 (pulses, 0.1 mm). Wrap-safe. */
static inline int32_t can_distance(const CanRecord_t *r, uint32_t enc_now)
{
    return (int32_t)(enc_now - r->enc_capture_s2);
}

/* ------------------------------------------------------------------------ */
/* Ring buffer FIFO. Not thread-safe: caller provides locking.              */
/* ------------------------------------------------------------------------ */
typedef struct {
    CanRecord_t buf[CAN_FIFO_CAPACITY];
    uint8_t     head;   /**< index of oldest element */
    uint8_t     count;
} CanFifo_t;

void         can_fifo_clear(CanFifo_t *f);
bool         can_fifo_push(CanFifo_t *f, const CanRecord_t *r);
bool         can_fifo_pop(CanFifo_t *f, CanRecord_t *out);
CanRecord_t *can_fifo_at(CanFifo_t *f, uint8_t idx);       /**< 0 = oldest */
CanRecord_t *can_fifo_find(CanFifo_t *f, uint16_t can_id);
static inline uint8_t can_fifo_count(const CanFifo_t *f) { return f->count; }
static inline bool    can_fifo_full(const CanFifo_t *f)  { return f->count >= CAN_FIFO_CAPACITY; }

#ifdef __cplusplus
}
#endif

#endif /* CAN_RECORD_H */
