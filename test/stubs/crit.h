#include <stdint.h>
static inline uint32_t crit_enter(void){return 0;}
static inline void crit_exit(uint32_t s){(void)s;}
