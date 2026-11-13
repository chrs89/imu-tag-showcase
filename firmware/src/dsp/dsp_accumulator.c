#include "dsp/dsp_accumulator.h"

#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#define SLOTS 2

enum {
	SLOT_FREE = 0,
	SLOT_READY = 1,
};

static float s_slot[SLOTS][DSP_MAX_WINDOW_SAMPLES];
/* Plain variables: written by the producer before the slot goes READY,
 * read by the consumer after it has seen READY. The state word orders them. */
static uint16_t s_len[SLOTS];
static uint32_t s_seq[SLOTS];
static atomic_t s_state[SLOTS];
/* Count is the number of READY slots, so the limit is the number of slots. */
static K_SEM_DEFINE(s_window_sem, 0, SLOTS);

/* Producer-owned: only dsp_accumulator_push() writes these after init. */
static uint8_t s_fill;
static uint16_t s_n;
static uint16_t s_window_len;
static uint32_t s_window_seq;
static bool s_dropping;

/* Consumer-owned: only dsp_accumulator_wait_window() writes this after init. */
static uint8_t s_read;

static atomic_t s_windows_filled = ATOMIC_INIT(0);
static atomic_t s_windows_dropped = ATOMIC_INIT(0);

int dsp_accumulator_init(uint16_t window_len)
{
	if (window_len == 0 || window_len > DSP_MAX_WINDOW_SAMPLES) {
		return -EINVAL;
	}

	s_fill = 0;
	s_n = 0;
	s_window_len = window_len;
	s_window_seq = 0;
	s_dropping = false;
	s_read = 0;

	for (int i = 0; i < SLOTS; i++) {
		s_len[i] = 0;
		s_seq[i] = 0;
		atomic_set(&s_state[i], SLOT_FREE);
	}
	atomic_set(&s_windows_filled, 0);
	atomic_set(&s_windows_dropped, 0);
	k_sem_reset(&s_window_sem);

	return 0;
}

void dsp_accumulator_push(float value)
{
	if (s_window_len == 0) {
		/* Not initialised: nothing to fill. */
		return;
	}

	if (atomic_get(&s_state[s_fill]) != SLOT_FREE) {
		/* The consumer still owns this slot. Drop the window whole,
		 * count the episode once, restart clean when the slot frees. */
		if (!s_dropping) {
			atomic_inc(&s_windows_dropped);
			s_dropping = true;
		}
		s_n = 0;
		return;
	}
	s_dropping = false;

	s_slot[s_fill][s_n++] = value;
	if (s_n < s_window_len) {
		return;
	}

	s_len[s_fill] = s_n;
	s_seq[s_fill] = ++s_window_seq;
	/* Publishes the samples, s_len and s_seq: everything above is
	 * visible to the consumer before it can observe READY. */
	atomic_set(&s_state[s_fill], SLOT_READY);
	k_sem_give(&s_window_sem);
	atomic_inc(&s_windows_filled);

	s_fill = (uint8_t)(1u - s_fill);
	s_n = 0;
}

void dsp_accumulator_wait_window(float *out, uint16_t *len, uint32_t *seq)
{
	(void)k_sem_take(&s_window_sem, K_FOREVER);

	/* The producer alternates slots and skips none, so the next READY
	 * slot is always the one this index points at. */
	__ASSERT(atomic_get(&s_state[s_read]) == SLOT_READY, "slot %u not ready", s_read);

	*len = s_len[s_read];
	*seq = s_seq[s_read];
	memcpy(out, s_slot[s_read], (size_t)*len * sizeof(float));
	/* Copy first, then hand the slot back: the producer may reuse it
	 * from this line on. */
	atomic_set(&s_state[s_read], SLOT_FREE);

	s_read = (uint8_t)(1u - s_read);
}

void dsp_accumulator_stats(uint32_t *filled, uint32_t *dropped)
{
	*filled = (uint32_t)atomic_get(&s_windows_filled);
	*dropped = (uint32_t)atomic_get(&s_windows_dropped);
}

unsigned int dsp_accumulator_pending(void)
{
	return k_sem_count_get(&s_window_sem);
}
