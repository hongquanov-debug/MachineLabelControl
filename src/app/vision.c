/*
 * TÓM TẮT (tiếng Việt): liên kết với PC Vision trên RS485_3.
 *  Ngắt UART (rx_cb) đẩy byte nhận được vào StreamBuffer của FreeRTOS (bộ đệm
 *  byte an toàn giữa ngắt và task). Task vision lấy byte ra, ghép thành gói
 *  (vision_parser_feed) rồi ghi góc theta vào bản ghi của lon tương ứng.
 */
#include "vision.h"

#include "FreeRTOS.h"
#include "app_config.h"
#include "bsp_rs485.h"
#include "stream_buffer.h"
#include "task.h"
#include "tracking.h"
#include "vision_proto.h"

static StreamBufferHandle_t s_rx;
static VisionParser_t s_parser;
static VisionStats_t s_stats;

static void rx_cb(const uint8_t *data, uint16_t len, bool idle, void *ctx)
{
    (void)idle;
    (void)ctx;
    if (len > 0U) {
        BaseType_t woken = pdFALSE;
        xStreamBufferSendFromISR(s_rx, data, len, &woken);
        portYIELD_FROM_ISR(woken);
    }
}

void vision_init(void)
{
    s_rx = xStreamBufferCreate(256, 1);
    configASSERT(s_rx != NULL);
    vision_parser_init(&s_parser);
    bsp_rs485_init(RS485_VISION, VISION_BAUD, RS485_PARITY_NONE, rx_cb, NULL);
}

typedef struct {
    const VisionResult_t *res;
    bool accepted;
} ApplyCtx_t;

static void apply_result(CanRecord_t *r, void *vctx)
{
    ApplyCtx_t *c = (ApplyCtx_t *)vctx;
    if (r->status != CAN_STATUS_VISION_WAIT) {
        return;  /* already timed out -> keep NG */
    }
    int16_t th = c->res->theta_x100;
    if (c->res->quality == VISION_Q_OK && th >= -18000 && th <= 18000) {
        r->theta_degree = (float)th / 100.0f;
        r->status       = CAN_STATUS_VISION_OK;
    } else {
        r->status = CAN_ERROR_VISION_NG;   /* NG_NO_TAB / NG_DEFECT / NG_WRONG_TYPE */
    }
    c->accepted = true;
}

void vision_task(void *arg)
{
    (void)arg;
    uint8_t buf[32];
    for (;;) {
        /* 1) pending trigger frames (optional, §2.2.A) */
        uint16_t id;
        while (xQueueReceive(tracking_q_vision_trig(), &id, 0) == pdTRUE) {
            uint8_t frame[VISION_TRIG_LEN];
            vision_build_trigger(id, frame);
            if (bsp_rs485_send(RS485_VISION, frame, VISION_TRIG_LEN, 5)) {
                s_stats.tx_frames++;
            }
        }

        /* 2) responses */
        size_t n = xStreamBufferReceive(s_rx, buf, sizeof(buf), pdMS_TO_TICKS(2));
        for (size_t i = 0; i < n; i++) {
            VisionResult_t res;
            if (!vision_parser_feed(&s_parser, buf[i], &res)) {
                continue;
            }
            ApplyCtx_t ctx = {&res, false};
            if (tracking_edit(res.can_id, apply_result, &ctx) && ctx.accepted) {
                s_stats.rx_ok++;
                tracking_vision_result_ok();
                if (res.quality != VISION_Q_OK) {
                    s_stats.rx_ng++;
                }
            } else {
                s_stats.rx_late++;
            }
        }
        s_stats.crc_errors   = s_parser.crc_errors;
        s_stats.frame_errors = s_parser.frame_errors;
    }
}

VisionStats_t vision_stats(void) { return s_stats; }
