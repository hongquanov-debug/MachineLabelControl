/**
 * @file station_feed.h
 * @brief Can infeed escapement P1 (stop pin A) / P2 (V-wedge B).
 *        Releases one can every CAN_PITCH (120 mm of belt travel) -> P_can = 120 mm.
 */
#ifndef STATION_FEED_H
#define STATION_FEED_H

#include <stdbool.h>

void station_feed_task(void *arg);
void station_feed_enable(bool en);   /* AUTO_RUN feeding on/off (ALM_03, ALM_07 -> off) */
bool station_feed_enabled(void);

#endif
