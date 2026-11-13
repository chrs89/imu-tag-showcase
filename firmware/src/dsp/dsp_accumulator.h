#ifndef DSP_DSP_ACCUMULATOR_H
#define DSP_DSP_ACCUMULATOR_H

#include <stdint.h>

/* Lock-free hand-off of DSP windows from the burst thread (producer) to
 * dsp_worker (consumer). Two slots; each slot state word has one writer per
 * direction: the producer sets FREE to READY, the consumer READY to FREE.
 * The producer never waits: when the slot it needs is still READY, the
 * window is dropped whole and counted once. */

/** Largest window the slots can hold, in samples. */
#define DSP_MAX_WINDOW_SAMPLES 512

/** Reset the module for windows of window_len samples.
 *  Called before either thread touches the module.
 *  Returns 0, or -EINVAL when window_len is 0 or above DSP_MAX_WINDOW_SAMPLES. */
int dsp_accumulator_init(uint16_t window_len);

/** Append one sample to the window being filled.
 *  Called from the burst thread sink loop, O(1), never blocks. */
void dsp_accumulator_push(float value);

/** Copy the oldest ready window into out (room for DSP_MAX_WINDOW_SAMPLES).
 *  Called from dsp_worker only; blocks on s_window_sem. */
void dsp_accumulator_wait_window(float *out, uint16_t *len, uint32_t *seq);

/** Read the counters of published and dropped windows.
 *  Called from any thread. */
void dsp_accumulator_stats(uint32_t *filled, uint32_t *dropped);

/** Number of windows published and not yet taken, 0 to 2.
 *  Called from any thread. */
unsigned int dsp_accumulator_pending(void);

#endif
