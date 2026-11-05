#include "can_record.h"

#include <string.h>

void can_fifo_clear(CanFifo_t *f)
{
    f->head  = 0;
    f->count = 0;
}

bool can_fifo_push(CanFifo_t *f, const CanRecord_t *r)
{
    if (f->count >= CAN_FIFO_CAPACITY) {
        return false;
    }
    uint8_t tail = (uint8_t)((f->head + f->count) % CAN_FIFO_CAPACITY);
    f->buf[tail] = *r;
    f->count++;
    return true;
}

bool can_fifo_pop(CanFifo_t *f, CanRecord_t *out)
{
    if (f->count == 0U) {
        return false;
    }
    if (out != NULL) {
        *out = f->buf[f->head];
    }
    f->head = (uint8_t)((f->head + 1U) % CAN_FIFO_CAPACITY);
    f->count--;
    return true;
}

CanRecord_t *can_fifo_at(CanFifo_t *f, uint8_t idx)
{
    if (idx >= f->count) {
        return NULL;
    }
    return &f->buf[(f->head + idx) % CAN_FIFO_CAPACITY];
}

CanRecord_t *can_fifo_find(CanFifo_t *f, uint16_t can_id)
{
    for (uint8_t i = 0; i < f->count; i++) {
        CanRecord_t *r = can_fifo_at(f, i);
        if (r->can_id == can_id) {
            return r;
        }
    }
    return NULL;
}
