/**
 * @file station_label.h
 * @brief Station 3 - 6-head rotary labeler (M3 index 60 deg, P5, vacuum), Spec §3.
 *
 * Head positions (angle -> role):
 *   0 deg feed (VAC on, P7 assist) | 60 deg vacuum check | 120 deg transfer
 *   180 deg labeling (P5)          | 240 deg blow/clean  | 300 deg ready
 *
 * One cycle per can (event fired by tracking, LABEL_T_PRESS_MS ahead of arrival):
 *   t=0..150   M3 index 60 deg (35/80/35 ms)
 *   dwell      feed head: VAC on, +30 ms P7 30 ms, M4 pulls next label to gap
 *              check head: +vacuum_detect_ms read DI_VAC_SW -> OK / MISS
 *   t=160      P5 down (DI_P5_DOWN <= 150 ms else ALM_08)
 *   t=210      label pressed on lid
 *   t=up-blow  VAC off + DO_VAC_BLOW on
 *   t=210+dwell P5 up, blow off; DI_P5_UP before t=380 ms (ALM_08)
 */
#ifndef STATION_LABEL_H
#define STATION_LABEL_H

#include <stdbool.h>

void station_label_task(void *arg);

/** Request priming cycles (load heads up to the 180 deg station) - on START. */
void station_label_request_prime(void);

/** Lift P5, all vacuum off (homing, E-Stop recovery). */
void station_label_safe(void);

/** Reset head bookkeeping after M3 homing (table at station 0). */
void station_label_reset_heads(void);

#endif
