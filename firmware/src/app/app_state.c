#include "app/app_state.h"
#include <zephyr/sys/atomic.h>

/* All private to this file; every value is one word. */
static atomic_t s_streaming = ATOMIC_INIT(0);            /* atomic.h 61 */
static atomic_t s_stream_starts = ATOMIC_INIT(0);
static atomic_t s_stream_stops = ATOMIC_INIT(0);

bool app_state_streaming(void)
{
        return atomic_get(&s_streaming) != 0;
}

void app_state_set_streaming(bool on)
{
        /* atomic_set returns the previous value, so one operation tells
         * us whether this call was a real transition. */
        atomic_val_t was = atomic_set(&s_streaming, on ? 1 : 0);

        if (on && was == 0) {
                atomic_inc(&s_stream_starts);
        } else if (!on && was != 0) {
                atomic_inc(&s_stream_stops);
        }
}

uint32_t app_state_stream_starts(void)
{
        return (uint32_t)atomic_get(&s_stream_starts);
}

uint32_t app_state_stream_stops(void)
{
        return (uint32_t)atomic_get(&s_stream_stops);
}
