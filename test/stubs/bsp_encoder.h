#include <stdint.h>
extern uint32_t g_enc;
static inline uint32_t bsp_encoder_now(void){return g_enc;}
