/**
 * @file station_orient.h
 * @brief Station 2 - can orientation (P3 stop pin, P4 clamp, M2 PU wheel), Spec §1.2.
 *
 * Sequence: P3 out -> can arrives -> P4 clamp -> M2 rotates theta -> P4 release
 *           -> P3 retract -> tracking record re-anchored at X = 300 mm.
 */
#ifndef STATION_ORIENT_H
#define STATION_ORIENT_H

void station_orient_task(void *arg);
/** Release P3/P4 (homing, E-Stop recovery). */
void station_orient_release(void);

#endif
