/**
 * Host unit tests for lib/pmems_core:  pio test -e native
 */
#include <string.h>
#include <unity.h>

#include "can_record.h"
#include "crc16.h"
#include "modbus_pdu.h"
#include "orient_math.h"
#include "pulse_ramp.h"
#include "recipe_types.h"
#include "vision_proto.h"

void setUp(void) {}
void tearDown(void) {}

/* ---------------- CRC16 Modbus ---------------- */
static void test_crc16_known_vector(void)
{
    /* "123456789" -> 0x4B37 (CRC-16/MODBUS check value) */
    TEST_ASSERT_EQUAL_HEX16(0x4B37, crc16_modbus((const uint8_t *)"123456789", 9));
    /* Modbus request 01 03 00 00 00 0A -> CRC 0xCDC5 (sent C5 CD) */
    const uint8_t req[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A};
    TEST_ASSERT_EQUAL_HEX16(0xCDC5, crc16_modbus(req, sizeof(req)));
}

/* ---------------- Vision protocol (Spec §2.2) ---------------- */
static void test_vision_trigger_frame(void)
{
    uint8_t f[VISION_TRIG_LEN];
    TEST_ASSERT_EQUAL(8, vision_build_trigger(0x1234, f));
    TEST_ASSERT_EQUAL_HEX8(0x55, f[0]);
    TEST_ASSERT_EQUAL_HEX8(0xAA, f[1]);
    TEST_ASSERT_EQUAL_HEX8(0x10, f[2]);
    TEST_ASSERT_EQUAL_HEX8(0x34, f[3]); /* little endian */
    TEST_ASSERT_EQUAL_HEX8(0x12, f[4]);
    uint16_t crc = crc16_modbus(f, 5);
    TEST_ASSERT_EQUAL_HEX8(crc & 0xFF, f[5]);
    TEST_ASSERT_EQUAL_HEX8(crc >> 8, f[6]);
    TEST_ASSERT_EQUAL_HEX8(0x0A, f[7]);
}

static void test_vision_parse_with_noise_and_resync(void)
{
    VisionParser_t p;
    vision_parser_init(&p);
    VisionResult_t in = {.can_id = 77, .theta_x100 = -1250, .quality = VISION_Q_OK}, out;
    uint8_t f[VISION_RESP_LEN];
    vision_build_result(&in, f);

    const uint8_t noise[] = {0x00, 0xAA, 0x13, 0xAA, 0xAA};
    for (size_t i = 0; i < sizeof(noise); i++) {
        TEST_ASSERT_FALSE(vision_parser_feed(&p, noise[i], &out));
    }
    bool got = false;
    for (size_t i = 0; i < sizeof(f); i++) {
        got = vision_parser_feed(&p, f[i], &out);
    }
    TEST_ASSERT_TRUE(got);
    TEST_ASSERT_EQUAL_UINT16(77, out.can_id);
    TEST_ASSERT_EQUAL_INT16(-1250, out.theta_x100);
    TEST_ASSERT_EQUAL_UINT8(VISION_Q_OK, out.quality);
}

static void test_vision_crc_error_rejected(void)
{
    VisionParser_t p;
    vision_parser_init(&p);
    VisionResult_t in = {.can_id = 5, .theta_x100 = 3542, .quality = 0}, out;
    uint8_t f[VISION_RESP_LEN];
    vision_build_result(&in, f);
    f[6] ^= 0x01;
    bool got = false;
    for (size_t i = 0; i < sizeof(f); i++) {
        got |= vision_parser_feed(&p, f[i], &out);
    }
    TEST_ASSERT_FALSE(got);
    TEST_ASSERT_EQUAL_UINT32(1, p.crc_errors);
}

/* ---------------- Tracking FIFO ---------------- */
static void test_fifo_push_pop_wrap_find(void)
{
    static CanFifo_t f;
    can_fifo_clear(&f);
    CanRecord_t r = {0};
    for (uint16_t i = 1; i <= CAN_FIFO_CAPACITY; i++) {
        r.can_id = i;
        TEST_ASSERT_TRUE(can_fifo_push(&f, &r));
    }
    TEST_ASSERT_TRUE(can_fifo_full(&f));
    TEST_ASSERT_FALSE(can_fifo_push(&f, &r));

    CanRecord_t out;
    TEST_ASSERT_TRUE(can_fifo_pop(&f, &out));
    TEST_ASSERT_EQUAL_UINT16(1, out.can_id);
    r.can_id = 100;
    TEST_ASSERT_TRUE(can_fifo_push(&f, &r)); /* wraps around */
    TEST_ASSERT_EQUAL_UINT16(2, can_fifo_at(&f, 0)->can_id);
    TEST_ASSERT_EQUAL_UINT16(100, can_fifo_at(&f, CAN_FIFO_CAPACITY - 1)->can_id);
    TEST_ASSERT_NOT_NULL(can_fifo_find(&f, 100));
    TEST_ASSERT_NULL(can_fifo_find(&f, 1));
}

static void test_can_distance_wraps(void)
{
    CanRecord_t r = {0};
    r.enc_capture_s2 = 0xFFFFFF00UL;
    TEST_ASSERT_EQUAL_INT32(0x100 + 500, can_distance(&r, 500));
    TEST_ASSERT_TRUE(can_status_is_error(CAN_ERROR_LABEL_MISS));
    TEST_ASSERT_FALSE(can_status_is_error(CAN_STATUS_PASSED));
}

/* ---------------- Modbus ---------------- */
static uint16_t s_hold[50];
static uint8_t rh(void *c, uint16_t a, uint16_t *v) { (void)c; *v = s_hold[a]; return 0; }
static uint8_t wh(void *c, uint16_t a, uint16_t v) { (void)c; s_hold[a] = v; return 0; }
static uint8_t ri(void *c, uint16_t a, uint16_t *v) { (void)c; *v = (uint16_t)(0x3000 + a); return 0; }
static const MbMap_t k_map = {50, 40, rh, wh, ri, NULL};

static void test_modbus_rtu_read_input(void)
{
    uint8_t req[8], resp[64];
    size_t n = mb_rtu_build_read(1, MB_FC_READ_INPUT, 0, 3, req);
    size_t r = mb_rtu_slave_process(&k_map, 1, req, n, resp, sizeof(resp));
    TEST_ASSERT_EQUAL(11, r);
    uint16_t regs[3];
    TEST_ASSERT_EQUAL_INT(0, mb_rtu_parse_read_resp(1, MB_FC_READ_INPUT, resp, r, regs, 3));
    TEST_ASSERT_EQUAL_HEX16(0x3000, regs[0]);
    TEST_ASSERT_EQUAL_HEX16(0x3002, regs[2]);
}

static void test_modbus_rtu_write_and_exceptions(void)
{
    uint8_t req[8], resp[64];
    size_t n = mb_rtu_build_write_single(1, 2, 120, req);
    size_t r = mb_rtu_slave_process(&k_map, 1, req, n, resp, sizeof(resp));
    TEST_ASSERT_EQUAL(8, r);
    TEST_ASSERT_EQUAL_UINT16(120, s_hold[2]);
    TEST_ASSERT_EQUAL_INT(0, mb_rtu_parse_write_single_resp(1, 2, 120, resp, r));

    n = mb_rtu_build_read(1, MB_FC_READ_HOLDING, 48, 5, req); /* out of range */
    r = mb_rtu_slave_process(&k_map, 1, req, n, resp, sizeof(resp));
    TEST_ASSERT_EQUAL_INT(MB_EX_ILLEGAL_ADDRESS, mb_rtu_parse_read_resp(1, MB_FC_READ_HOLDING, resp, r, NULL, 5));

    n = mb_rtu_build_read(2, MB_FC_READ_HOLDING, 0, 1, req); /* other slave */
    TEST_ASSERT_EQUAL(0, mb_rtu_slave_process(&k_map, 1, req, n, resp, sizeof(resp)));
    req[n - 1] ^= 0xFF; /* bad CRC */
    TEST_ASSERT_EQUAL(0, mb_rtu_slave_process(&k_map, 2, req, n, resp, sizeof(resp)));
}

static void test_modbus_tcp_write_multiple(void)
{
    const uint8_t adu[] = {0x00, 0x07, 0x00, 0x00, 0x00, 0x0B, 0x01, 0x10, 0x00,
                           0x03, 0x00, 0x02, 0x04, 0x00, 0x28, 0x00, 0x32};
    uint8_t resp[32];
    size_t r = mb_tcp_process(&k_map, adu, sizeof(adu), resp, sizeof(resp));
    TEST_ASSERT_EQUAL(12, r);
    TEST_ASSERT_EQUAL_HEX8(0x07, resp[1]); /* transaction id echoed */
    TEST_ASSERT_EQUAL_HEX8(0x10, resp[7]);
    TEST_ASSERT_EQUAL_UINT16(40, s_hold[3]);
    TEST_ASSERT_EQUAL_UINT16(50, s_hold[4]);
}

/* ---------------- Pulse ramp ---------------- */
static void test_ramp_exact_pulse_count_and_vmax(void)
{
    PulseProfile_t p = {400.0f, 20000.0f, 200000.0f};
    RampState_t r;
    float f = ramp_init(&r, &p, 5000);
    uint32_t pulses = 1;
    float fmax = f;
    while ((f = ramp_step(&r)) > 0.0f) {
        pulses++;
        if (f > fmax) {
            fmax = f;
        }
        TEST_ASSERT_TRUE(f >= 399.0f);
    }
    TEST_ASSERT_EQUAL_UINT32(5000, pulses);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 20000.0f, fmax);
}

static void test_ramp_continuous_stop(void)
{
    PulseProfile_t p = {400.0f, 10000.0f, 100000.0f};
    RampState_t r;
    ramp_init(&r, &p, 0);
    for (int i = 0; i < 3000; i++) {
        TEST_ASSERT_TRUE(ramp_step(&r) > 0.0f);
    }
    ramp_request_stop(&r);
    int n = 0;
    while (ramp_step(&r) > 0.0f && n < 100000) {
        n++;
    }
    TEST_ASSERT_TRUE(n > 100 && n < 1000); /* ~ (10000^2-400^2)/(2*100000) = 499 */
}

static void test_ramp_profile_m3_index(void)
{
    /* 60 deg = 16667 pulses in 35/80/35 ms (Spec §3.2) */
    PulseProfile_t p;
    ramp_profile_from_times(16667, 0.035f, 0.080f, 400.0f, &p);
    TEST_ASSERT_FLOAT_WITHIN(500.0f, 144800.0f, p.f_max);
    RampState_t r;
    float f = ramp_init(&r, &p, 16667);
    double t = 1.0 / f;
    while ((f = ramp_step(&r)) > 0.0f) {
        t += 1.0 / f;
    }
    TEST_ASSERT_FLOAT_WITHIN(0.005f, 0.150f, (float)t); /* total index time ~150 ms */
}

/* ---------------- Orientation ---------------- */
static void test_orient_math(void)
{
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -170.0f, orient_normalize_deg(190.0f));
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 180.0f, orient_normalize_deg(-180.0f));
    /* +35.42 deg on a 66 mm can -> 35.42 * 0.695 * 4.444 = 109.4 pulses */
    TEST_ASSERT_EQUAL_INT32(109, orient_m2_pulses(35.42f, 0, 0, 695, 1600.0f / 360.0f));
    /* offsets wrap: 170 + 20 = 190 -> -170 deg -> -525 pulses */
    TEST_ASSERT_EQUAL_INT32(-525, orient_m2_pulses(170.0f, 2000, 0, 695, 1600.0f / 360.0f));
}

/* ---------------- Recipe ---------------- */
static void test_recipe_defaults_and_crc(void)
{
    CanRecipe_t r;
    recipe_factory_default(2, 300, &r);
    TEST_ASSERT_TRUE(recipe_valid(&r));
    TEST_ASSERT_TRUE(recipe_check_ranges(&r));
    TEST_ASSERT_EQUAL_STRING("CAN_250ML_LUN", r.can_name);
    TEST_ASSERT_EQUAL_UINT16(300 - 232, r.m6_height_pos_x10);
    r.p5_down_dwell_ms = 41;
    TEST_ASSERT_FALSE(recipe_valid(&r));
    recipe_seal(&r);
    TEST_ASSERT_TRUE(recipe_valid(&r));

    recipe_factory_default(12, 300, &r);
    TEST_ASSERT_EQUAL_STRING("RECIPE_12", r.can_name);
    TEST_ASSERT_EQUAL(64, sizeof(CanRecipe_t));
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_crc16_known_vector);
    RUN_TEST(test_vision_trigger_frame);
    RUN_TEST(test_vision_parse_with_noise_and_resync);
    RUN_TEST(test_vision_crc_error_rejected);
    RUN_TEST(test_fifo_push_pop_wrap_find);
    RUN_TEST(test_can_distance_wraps);
    RUN_TEST(test_modbus_rtu_read_input);
    RUN_TEST(test_modbus_rtu_write_and_exceptions);
    RUN_TEST(test_modbus_tcp_write_multiple);
    RUN_TEST(test_ramp_exact_pulse_count_and_vmax);
    RUN_TEST(test_ramp_continuous_stop);
    RUN_TEST(test_ramp_profile_m3_index);
    RUN_TEST(test_orient_math);
    RUN_TEST(test_recipe_defaults_and_crc);
    return UNITY_END();
}
