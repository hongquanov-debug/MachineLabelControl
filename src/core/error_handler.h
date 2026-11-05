#ifndef ERROR_HANDLER_H
#define ERROR_HANDLER_H

/** Fatal error: put all outputs in a safe state and halt (watchdog will reset). */
void error_handler(void) __attribute__((noreturn));

#endif
