/**
 * @file vision.h
 * @brief Vision PC link on RS485_3 (Spec §2): sends trigger frames, parses
 *        results, writes theta/status into the tracking record.
 */
#ifndef VISION_H
#define VISION_H

#include <stdint.h>

typedef struct {
    uint32_t rx_ok;
    uint32_t rx_late;       /* result for a can no longer waiting (timeout / unknown id) */
    uint32_t rx_ng;         /* quality flag != OK */
    uint32_t crc_errors;
    uint32_t frame_errors;
    uint32_t tx_frames;
} VisionStats_t;

void vision_init(void);
void vision_task(void *arg);
VisionStats_t vision_stats(void);

#endif
