/**
 * @file tracking.h
 * @brief Can tracking engine (Spec §1.2/§1.3).
 *
 *  - EXTI2 (S2): capture TIM2->CNT, assign can_id, push CanRecord_t into FIFO.
 *  - TIM6 5 kHz: scan all cans, fire station events by encoder distance:
 *      +500   CAM_TRIG pulse, start 80 ms Vision timeout
 *      +3000  (minus pre-close) -> orient task queue  (VISION_OK cans only)
 *      +6500  (minus lead = v_belt * 210 ms) -> label task queue
 *      +9500  read S5 -> INSPECTED / LABEL_MISS
 *      +12500 NG -> P8 reject
 *      +20000 pop record, update counters
 *
 * All FIFO accesses are protected by crit_enter()/crit_exit() (BASEPRI 5).
 */
#ifndef TRACKING_H
#define TRACKING_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "can_record.h"
#include "queue.h"

typedef struct {
    uint16_t   can_id;
    TickType_t tick;       /* when the event fired */
} StationEvent_t;

typedef struct {
    uint32_t fed;          /* cans detected at S2 */
    uint32_t ok;           /* cans leaving the belt with label OK */
    uint32_t ng;           /* cans rejected by P8 */
} TrackingCounters_t;

void tracking_init(void);
void tracking_enable(bool en);             /* accept new cans at S2 */
void tracking_clear(void);                 /* drop all records (E-Stop / purge) */

/* Queues consumed by station tasks */
QueueHandle_t tracking_q_vision_trig(void);   /* uint16_t can_id */
QueueHandle_t tracking_q_orient(void);        /* StationEvent_t */
QueueHandle_t tracking_q_label(void);         /* StationEvent_t */

/* Thread-safe record access */
bool tracking_get(uint16_t can_id, CanRecord_t *out);
typedef void (*tracking_edit_fn)(CanRecord_t *rec, void *ctx);
bool tracking_edit(uint16_t can_id, tracking_edit_fn fn, void *ctx);

uint8_t  tracking_count(void);
uint32_t tracking_belt_pulses_per_s(void);
uint16_t tracking_speed_cans_min(void);
TrackingCounters_t tracking_counters(void);
void     tracking_vision_result_ok(void);   /* reset consecutive timeout counter */

/* ISR / periodic hooks */
void tracking_on_s2_isr(uint32_t enc_capture);
void tracking_tick_isr(void);
void tracking_service_1ms(void);

#endif /* TRACKING_H */
