/**
 * Station logic test: compiles the REAL src/app/tracking.c on the PC, with
 * FreeRTOS / HAL / BSP replaced by the stubs in test/stubs. Simulates the
 * encoder, the tick, the station tasks and both cameras.   pio test -e native
 */
#include <string.h>
#include <unity.h>

#include "alarm.h"
#include "bsp_tick.h"
#include "io.h"
#include "app_config.h"
#include "vision_proto.h"

#include "tracking.c"   /* code under test (found through -Isrc/app) */

TickType_t g_tick;
uint32_t g_enc;
bool g_s5 = true;
int g_cam1, g_cam2, g_p8_pushes;
static uint32_t s_alarm_mask;

/* ---- stubs used by tracking.c ---- */
typedef struct { uint8_t buf[64][16]; int n; size_t sz; } Q;
static Q qs[3]; static int nq;
QueueHandle_t xQueueCreate(int n, size_t sz) { (void)n; qs[nq].sz = sz; return &qs[nq++]; }
BaseType_t xQueueSendFromISR(QueueHandle_t q, const void *it, BaseType_t *w) { Q *x = q; (void)w; memcpy(x->buf[x->n++ % 64], it, x->sz); return pdTRUE; }
BaseType_t xQueueReset(QueueHandle_t q) { ((Q *)q)->n = 0; return pdTRUE; }
static int qpop(Q *x, int *i, void *out) { if (*i >= x->n) return 0; memcpy(out, x->buf[(*i)++ % 64], x->sz); return 1; }
void alarm_raise(AlarmCode_t c) { s_alarm_mask |= 1UL << c; }
void bsp_cam_trigger_pulse(void) { g_cam1++; }
void bsp_cam2_trigger_pulse(void) { g_cam2++; }
bool bsp_s5_label_present(void) { return g_s5; }
void bsp_tick_init(uint32_t hz, bsp_tick_cb_t cb) { (void)hz; (void)cb; }
void do_set(DoBit_t b, bool on) { if (b == DO_CYL_P8 && on) g_p8_pushes++; }
void HAL_NVIC_EnableIRQ(int irq) { (void)irq; }
void setUp(void) {}
void tearDown(void) {}

/* ---- scenario helpers ---- */
typedef enum { V_OK, V_NONE } VisionAns;
typedef enum { C_OK, C_NG, C_NONE } CodeAns;
static void st(CanRecord_t *r, void *c) { r->status = *(CanStatus_t *)c; }
static void rel(CanRecord_t *r, void *c) { (void)c; r->enc_capture_s2 = g_enc - ENC_POS_ORIENT; r->flags &= (uint8_t)~CAN_FLAG_HOLD; }

/* Feed one can and run the belt to the end; station tasks are emulated. */
static void run_can(VisionAns va, CodeAns ca)
{
    int qi[3] = {qs[0].n, qs[1].n, qs[2].n};
    tracking_on_s2_isr(g_enc);
    for (int step = 0; step < 30000; step++) {        /* 0.2 ms per step, 1 pulse/step = 500 mm/s */
        g_enc += 1;
        if (step % 5 == 0) g_tick++;
        tracking_tick_isr();
        VisionTrigReq_t t; StationEvent_t e;
        while (qpop(&qs[0], &qi[0], &t)) {
            if (t.cmd == VISION_CMD_TRIGGER && va == V_OK) {
                CanStatus_t s = CAN_STATUS_VISION_OK; tracking_edit(t.can_id, st, &s);
            } else if (t.cmd == VISION_CMD_TRIGGER_CODE && ca != C_NONE) {
                CanRecord_t r; tracking_get(t.can_id, &r);
                if (r.status == CAN_STATUS_CODE_WAIT) {     /* what vision.c apply_result() does */
                    CanStatus_t s = ca == C_OK ? CAN_STATUS_CODE_OK : CAN_ERROR_CODE_NG;
                    tracking_edit(t.can_id, st, &s);
                    tracking_code_result(ca == C_OK);
                }
            }
        }
        while (qpop(&qs[1], &qi[1], &e)) {             /* orient task: done immediately */
            CanStatus_t s = CAN_STATUS_ORIENTED; tracking_edit(e.can_id, st, &s); tracking_edit(e.can_id, rel, NULL);
        }
        while (qpop(&qs[2], &qi[2], &e)) {             /* label task: labeled */
            CanStatus_t s = CAN_STATUS_LABELED; tracking_edit(e.can_id, st, &s);
        }
        if (tracking_count() == 0) return;
    }
}


static bool tracking_ready;
static void fresh(void)
{
    if (!tracking_ready) {
        tracking_init();
        tracking_ready = true;
    }
    tracking_clear();
    tracking_enable(true);
    s_alarm_mask = 0;
    g_s5 = true;
}

static void test_good_can_passes_and_triggers_camera2(void)
{
    fresh();
    int cam2 = g_cam2;
    TrackingCounters_t c0 = tracking_counters();
    run_can(V_OK, C_OK);
    TrackingCounters_t c = tracking_counters();
    TEST_ASSERT_EQUAL_UINT32(c0.ok + 1, c.ok);
    TEST_ASSERT_EQUAL_UINT32(c0.ng, c.ng);
    TEST_ASSERT_EQUAL(cam2 + 1, g_cam2);
}

static void test_code_ng_is_rejected(void)
{
    fresh();
    TrackingCounters_t c0 = tracking_counters();
    run_can(V_OK, C_NG);
    TrackingCounters_t c = tracking_counters();
    TEST_ASSERT_EQUAL_UINT32(c0.ng + 1, c.ng);
    TEST_ASSERT_EQUAL_UINT32(c0.ok, c.ok);
}

static void test_code_timeout_rejects_and_raises_alm09(void)
{
    fresh();
    TrackingCounters_t c0 = tracking_counters();
    for (int i = 0; i < (int)CODE_MAX_CONSEC_TIMEOUT; i++) {
        run_can(V_OK, C_NONE);
    }
    TrackingCounters_t c = tracking_counters();
    TEST_ASSERT_EQUAL_UINT32(c0.ng + CODE_MAX_CONSEC_TIMEOUT, c.ng);
    TEST_ASSERT_TRUE(s_alarm_mask & (1UL << ALM_09_CODE_CAM_TIMEOUT));
}

static void test_consecutive_code_ng_raises_alm21(void)
{
    fresh();
    for (int i = 0; i < (int)CODE_MAX_CONSEC_NG - 1; i++) {
        run_can(V_OK, C_NG);
    }
    TEST_ASSERT_FALSE(s_alarm_mask & (1UL << ALM_21_CODE_NG_REPEATED));
    run_can(V_OK, C_OK);                       /* a good code resets the counter */
    for (int i = 0; i < (int)CODE_MAX_CONSEC_NG - 1; i++) {
        run_can(V_OK, C_NG);
    }
    TEST_ASSERT_FALSE(s_alarm_mask & (1UL << ALM_21_CODE_NG_REPEATED));
    run_can(V_OK, C_NG);
    TEST_ASSERT_TRUE(s_alarm_mask & (1UL << ALM_21_CODE_NG_REPEATED));
}

static void test_vision_ng_can_skips_camera2(void)
{
    fresh();
    int cam2 = g_cam2;
    TrackingCounters_t c0 = tracking_counters();
    run_can(V_NONE, C_OK);
    TrackingCounters_t c = tracking_counters();
    TEST_ASSERT_EQUAL(cam2, g_cam2);
    TEST_ASSERT_EQUAL_UINT32(c0.ng + 1, c.ng);
}

static void test_s5_missing_label(void)
{
    fresh();
    g_s5 = false;
    int cam2 = g_cam2;
    TrackingCounters_t c0 = tracking_counters();
    run_can(V_OK, C_OK);
    TrackingCounters_t c = tracking_counters();
#if LABEL_S5_CHECK_ENABLE
    TEST_ASSERT_EQUAL(cam2, g_cam2);            /* rejected by S5, camera 2 not used */
    TEST_ASSERT_EQUAL_UINT32(c0.ng + 1, c.ng);
#else
    TEST_ASSERT_EQUAL(cam2 + 1, g_cam2);        /* S5 disabled: camera 2 decides */
    TEST_ASSERT_EQUAL_UINT32(c0.ok + 1, c.ok);
#endif
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_good_can_passes_and_triggers_camera2);
    RUN_TEST(test_code_ng_is_rejected);
    RUN_TEST(test_code_timeout_rejects_and_raises_alm09);
    RUN_TEST(test_consecutive_code_ng_raises_alm21);
    RUN_TEST(test_vision_ng_can_skips_camera2);
    RUN_TEST(test_s5_missing_label);
    return UNITY_END();
}
