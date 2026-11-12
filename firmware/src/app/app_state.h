#ifndef APP_APP_STATE_H
#define APP_APP_STATE_H

#include <stdbool.h>
#include <stdint.h>

/* Runtime state of the IMU Tag: one flag and two counters, each a single
 * word held in an atomic_t. No mutex, because no reader ever needs two of
 * them to agree (chapter 6 section 4, A4 section 9). The state enum and
 * transition rules of the private firmware return in chapter 13. */

/** Called from any thread. True while a stream is active. */
bool app_state_streaming(void);

/** Called from app_wq only. Idempotent: setting the current value again
 *  changes nothing and counts nothing. */
void app_state_set_streaming(bool on);

/** Called from any thread. Number of off-to-on transitions since boot. */
uint32_t app_state_stream_starts(void);

/** Called from any thread. Number of on-to-off transitions since boot. */
uint32_t app_state_stream_stops(void);

#endif
