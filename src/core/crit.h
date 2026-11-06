/**
 * @file crit.h
 * @brief "Vùng găng" (critical section) - bảo vệ dữ liệu dùng chung giữa task và ngắt.
 *
 *  Vấn đề: biến s_do_image (ảnh 24 ngõ ra) được sửa ở cả task lẫn ngắt.
 *  Lệnh C "x |= bit" thực ra gồm 3 bước máy: đọc -> OR -> ghi. Nếu một ngắt
 *  chen vào giữa và cũng sửa x, thay đổi của ngắt sẽ bị ghi đè mất.
 *  Giải pháp: tạm CHẶN ngắt trong lúc đọc-sửa-ghi.
 *
 *  Cách làm ở đây: dùng thanh ghi BASEPRI của Cortex-M4. Khi BASEPRI = 5<<4,
 *  mọi ngắt có mức ưu tiên 5..15 bị chặn, còn ngắt mức 0..4 vẫn chạy.
 *  -> Ngắt phát xung động cơ (mức 2) KHÔNG bị ảnh hưởng, nên xung không bị méo.
 *
 *  Ưu điểm so với taskENTER_CRITICAL() của FreeRTOS: dùng được ở mọi nơi
 *  (task, ISR, và cả trước khi scheduler chạy) và lồng nhau được.
 *
 *  Cách dùng (luôn đi theo cặp, giữ vùng găng càng NGẮN càng tốt):
 *      uint32_t s = crit_enter();
 *      ... đọc/sửa dữ liệu dùng chung ...
 *      crit_exit(s);
 */
#ifndef CRIT_H
#define CRIT_H

#include <stdint.h>

#include "FreeRTOSConfig.h"
#include "stm32f4xx.h"

static inline uint32_t crit_enter(void)
{
    uint32_t old = __get_BASEPRI();                           /* lưu mức cũ để lồng nhau được */
    __set_BASEPRI_MAX(configMAX_SYSCALL_INTERRUPT_PRIORITY);  /* chỉ NÂNG mức chặn, không bao giờ hạ */
    __DSB();                                                  /* đảm bảo lệnh trên có hiệu lực ngay */
    __ISB();
    return old;
}

static inline void crit_exit(uint32_t old)
{
    __set_BASEPRI(old);                                       /* trả lại mức chặn trước đó */
}

#endif /* CRIT_H */
