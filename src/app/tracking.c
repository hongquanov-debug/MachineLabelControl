#include "tracking.h"

#include "alarm.h"
#include "app_config.h"
#include "bsp_board.h"
#include "bsp_encoder.h"
#include "bsp_tick.h"
#include "crit.h"
#include "io.h"
#include "task.h"

static CanFifo_t s_fifo;
static volatile bool s_enabled;
static uint16_t s_next_id = 1;
static uint32_t s_last_s2_enc;
static bool s_have_last_s2;
static volatile uint8_t s_consec_vision_tmo;

static QueueHandle_t s_q_trig, s_q_orient, s_q_label;
static volatile TrackingCounters_t s_cnt;

/* speed measurement */
static uint32_t s_speed_win_enc;
static uint16_t s_speed_win_ticks;
static volatile uint32_t s_pulses_per_s;

/* P8 reject actuator timing (1 ms service) */
static volatile uint16_t s_p8_timer_ms;

void tracking_init(void)
{
    can_fifo_clear(&s_fifo);
    s_q_trig   = xQueueCreate(8, sizeof(uint16_t));
    s_q_orient = xQueueCreate(4, sizeof(StationEvent_t));
    s_q_label  = xQueueCreate(4, sizeof(StationEvent_t));
    configASSERT(s_q_trig && s_q_orient && s_q_label);

    s_speed_win_enc = bsp_encoder_now();
    bsp_tick_init(TRACKING_TICK_HZ, tracking_tick_isr);
    HAL_NVIC_EnableIRQ(S2_EXTI_IRQn);
}

void tracking_enable(bool en) { s_enabled = en; }

void tracking_clear(void)
{
    uint32_t s = crit_enter();
    can_fifo_clear(&s_fifo);
    s_have_last_s2 = false;
    s_consec_vision_tmo = 0;
    crit_exit(s);
    xQueueReset(s_q_trig);
    xQueueReset(s_q_orient);
    xQueueReset(s_q_label);
}

QueueHandle_t tracking_q_vision_trig(void) { return s_q_trig; }
QueueHandle_t tracking_q_orient(void)      { return s_q_orient; }
QueueHandle_t tracking_q_label(void)       { return s_q_label; }

bool tracking_get(uint16_t can_id, CanRecord_t *out)
{
    uint32_t s = crit_enter();
    CanRecord_t *r = can_fifo_find(&s_fifo, can_id);
    if (r != NULL) {
        *out = *r;
    }
    crit_exit(s);
    return r != NULL;
}

bool tracking_edit(uint16_t can_id, tracking_edit_fn fn, void *ctx)
{
    uint32_t s = crit_enter();
    CanRecord_t *r = can_fifo_find(&s_fifo, can_id);
    if (r != NULL) {
        fn(r, ctx);
    }
    crit_exit(s);
    return r != NULL;
}

uint8_t tracking_count(void)              { return can_fifo_count(&s_fifo); }
uint32_t tracking_belt_pulses_per_s(void) { return s_pulses_per_s; }

uint16_t tracking_speed_cans_min(void)
{
    /* cans/min = v[mm/s] * 60 / pitch[mm] */
    return (uint16_t)((s_pulses_per_s * 60U) / CAN_PITCH_PULSES);
}

TrackingCounters_t tracking_counters(void)
{
    TrackingCounters_t c;
    uint32_t s = crit_enter();
    c.fed = s_cnt.fed;
    c.ok  = s_cnt.ok;
    c.ng  = s_cnt.ng;
    crit_exit(s);
    return c;
}

void tracking_vision_result_ok(void) { s_consec_vision_tmo = 0; }

/* ------------------------------------------------------------------------ */
/* EXTI2 - S2 can sensor                                                    */
/* ------------------------------------------------------------------------ */
void tracking_on_s2_isr(uint32_t enc)
{
    if (!s_enabled) {
        return;
    }
    /* Debounce: reject edges closer than S2_MIN_SPACING_PULSES to the previous can */
    if (s_have_last_s2 && (uint32_t)(enc - s_last_s2_enc) < S2_MIN_SPACING_PULSES) {
        return;
    }
    s_last_s2_enc  = enc;
    s_have_last_s2 = true;

    CanRecord_t r = {0};
    r.can_id          = s_next_id;
    r.enc_capture_s2  = enc;
    r.station_idx     = STATION_IDX_CAMERA;
    r.status          = CAN_STATUS_INIT;
    r.timestamp_enter = xTaskGetTickCountFromISR();

    s_next_id = (uint16_t)((s_next_id == 0xFFFFU) ? 1U : s_next_id + 1U);
    if (!can_fifo_push(&s_fifo, &r)) {
        alarm_raise(ALM_18_TRACKING_OVERFLOW);
        return;
    }
    s_cnt.fed++;
}

/* ------------------------------------------------------------------------ */
/* TIM6 - station scan                                                      */
/* ------------------------------------------------------------------------ */
static void ev_camera(CanRecord_t *r, TickType_t now, BaseType_t *woken)
{
    bsp_cam_trigger_pulse();                 /* 10 us strobe + capture */
    r->status         = CAN_STATUS_VISION_WAIT;
    r->timestamp_trig = now;
    r->station_idx    = STATION_IDX_ORIENT;
#if VISION_SEND_TRIGGER_PACKET
    xQueueSendFromISR(s_q_trig, &r->can_id, woken);
#else
    (void)woken;
#endif
}

static void vision_timeout_check(CanRecord_t *r, TickType_t now)
{
    if (r->status == CAN_STATUS_VISION_WAIT &&
        (now - r->timestamp_trig) > pdMS_TO_TICKS(VISION_TIMEOUT_MS)) {
        /* §2.3: mark NG, skip orient + label, reject at P8 */
        r->status = CAN_ERROR_VISION_NG;
        r->flags |= CAN_FLAG_VISION_TMO;
        if (++s_consec_vision_tmo >= VISION_MAX_CONSEC_TIMEOUT) {
            alarm_raise(ALM_05_VISION_TIMEOUT);
        }
    }
}

static void ev_orient(CanRecord_t *r, TickType_t now, BaseType_t *woken)
{
    if (r->status == CAN_STATUS_VISION_WAIT) {
        r->status = CAN_ERROR_VISION_NG;     /* should not happen (timeout << travel) */
    }
    r->station_idx = STATION_IDX_LABEL;
    if (r->status == CAN_STATUS_VISION_OK) {
        StationEvent_t ev = {r->can_id, now};
        if (xQueueSendFromISR(s_q_orient, &ev, woken) == pdTRUE) {
            r->flags |= CAN_FLAG_HOLD;       /* orient task owns the can until release */
        } else {
            r->status = CAN_ERROR_VISION_NG; /* station overloaded: cannot orient */
        }
    }
}

static void ev_label(CanRecord_t *r, TickType_t now, BaseType_t *woken)
{
    r->station_idx = STATION_IDX_INSPECT;
    if (r->status == CAN_STATUS_ORIENTED) {
        StationEvent_t ev = {r->can_id, now};
        if (xQueueSendFromISR(s_q_label, &ev, woken) != pdTRUE) {
            r->status = CAN_ERROR_LABEL_MISS;
        }
    } else if (!can_status_is_error(r->status)) {
        r->status = CAN_ERROR_LABEL_MISS;
    }
}

static void ev_inspect(CanRecord_t *r)
{
    r->station_idx = STATION_IDX_REJECT;
    if (r->status == CAN_STATUS_LABELED) {
        r->status = bsp_s5_label_present() ? CAN_STATUS_INSPECTED : CAN_ERROR_LABEL_MISS;
    }
}

static void ev_reject(CanRecord_t *r)
{
    r->station_idx = STATION_IDX_END;
    if (r->status != CAN_STATUS_INSPECTED) {
        /* every can not positively inspected is NG (incl. NG_CAMERA/VACUUM/LABEL) */
        do_set(DO_CYL_P8, true);
        s_p8_timer_ms = P8_PUSH_TIME_MS;
        r->status = CAN_STATUS_REJECTED;
        s_cnt.ng++;
    } else {
        r->status = CAN_STATUS_PASSED;
    }
}

void tracking_tick_isr(void)
{
    const uint32_t enc = bsp_encoder_now();
    const TickType_t now = xTaskGetTickCountFromISR();
    BaseType_t woken = pdFALSE;

    /* belt speed, 100 ms window */
    if (++s_speed_win_ticks >= TRACKING_TICK_HZ / 10U) {
        s_pulses_per_s     = (uint32_t)(enc - s_speed_win_enc) * 10U;
        s_speed_win_enc    = enc;
        s_speed_win_ticks  = 0;
    }
    const int32_t label_lead = (int32_t)((s_pulses_per_s * LABEL_T_PRESS_MS) / 1000U);

    uint32_t s = crit_enter();
    for (uint8_t i = 0; i < can_fifo_count(&s_fifo); i++) {
        CanRecord_t *r = can_fifo_at(&s_fifo, i);
        vision_timeout_check(r, now);
        if (r->flags & CAN_FLAG_HOLD) {
            continue;   /* held by P3: position frozen until re-anchored */
        }
        const int32_t d = can_distance(r, enc);
        switch (r->station_idx) {
        case STATION_IDX_CAMERA:
            if (d >= ENC_POS_CAMERA) ev_camera(r, now, &woken);
            break;
        case STATION_IDX_ORIENT:
            if (d >= ENC_POS_ORIENT - ORIENT_PRECLOSE_PULSES) ev_orient(r, now, &woken);
            break;
        case STATION_IDX_LABEL:
            if (d >= ENC_POS_LABEL - label_lead) ev_label(r, now, &woken);
            break;
        case STATION_IDX_INSPECT:
            if (d >= ENC_POS_INSPECT) ev_inspect(r);
            break;
        case STATION_IDX_REJECT:
            if (d >= ENC_POS_REJECT) ev_reject(r);
            break;
        default:
            break;
        }
    }

    /* retire cans that reached the end of the belt (FIFO order = physical order) */
    CanRecord_t *head = can_fifo_at(&s_fifo, 0);
    if (head != NULL && head->station_idx == STATION_IDX_END &&
        !(head->flags & CAN_FLAG_HOLD) && can_distance(head, enc) >= ENC_POS_END) {
        if (head->status == CAN_STATUS_PASSED) {
            s_cnt.ok++;
        }
        can_fifo_pop(&s_fifo, NULL);
    }
    crit_exit(s);

    portYIELD_FROM_ISR(woken);
}

void tracking_service_1ms(void)
{
    uint32_t s = crit_enter();
    if (s_p8_timer_ms > 0U) {
        if (--s_p8_timer_ms == 0U) {
            do_set(DO_CYL_P8, false);
        }
    }
    crit_exit(s);
}
