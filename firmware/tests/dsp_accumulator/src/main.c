#include <zephyr/ztest.h>
#include <errno.h>
#include "dsp/dsp_accumulator.h"

#define WINDOW_LEN 8

/* Producer and consumer both run on the test thread: a lagging consumer is
 * simply a wait_window() call that is not made yet. */

static float s_out[DSP_MAX_WINDOW_SAMPLES];

/* Runs before every case: the module state is static. */
static void reset_module(void *fixture)
{
	ARG_UNUSED(fixture);
	zassert_equal(dsp_accumulator_init(WINDOW_LEN), 0);
}

/* Pushes one window whose samples are base, base + 1, ... */
static void push_window(float base)
{
	for (int i = 0; i < WINDOW_LEN; i++) {
		dsp_accumulator_push(base + (float)i);
	}
}

/* Takes one window and checks its sequence number and contents. */
static void expect_window(uint32_t want_seq, float want_base)
{
	uint16_t len = 0;
	uint32_t seq = 0;

	dsp_accumulator_wait_window(s_out, &len, &seq);
	zassert_equal(len, WINDOW_LEN);
	zassert_equal(seq, want_seq);
	for (int i = 0; i < WINDOW_LEN; i++) {
		zassert_true(s_out[i] == want_base + (float)i, "sample %d of seq %u", i, seq);
	}
}

ZTEST(dsp_accumulator, test_init_rejects_bad_length)
{
	zassert_equal(dsp_accumulator_init(0), -EINVAL);
	zassert_equal(dsp_accumulator_init(DSP_MAX_WINDOW_SAMPLES + 1), -EINVAL);
	zassert_equal(dsp_accumulator_init(DSP_MAX_WINDOW_SAMPLES), 0);
}

/* Case (a): the consumer keeps up. */
ZTEST(dsp_accumulator, test_one_window_in_order)
{
	uint32_t filled, dropped;

	push_window(100.0f);
	zassert_equal(dsp_accumulator_pending(), 1);
	expect_window(1, 100.0f);

	dsp_accumulator_stats(&filled, &dropped);
	zassert_equal(filled, 1);
	zassert_equal(dropped, 0);
	zassert_equal(dsp_accumulator_pending(), 0);
}

/* Cases (b) and (c): the consumer lags by more than one window. The third
 * window is dropped whole and counted once, both stored windows arrive in
 * order, the flow resumes, and no window is left behind. */
ZTEST(dsp_accumulator, test_consumer_lag_drops_one_window_and_recovers)
{
	uint32_t filled, dropped;

	push_window(100.0f);
	push_window(200.0f);
	push_window(300.0f); /* both slots READY: dropped */
	zassert_equal(dsp_accumulator_pending(), 2);

	expect_window(1, 100.0f);
	expect_window(2, 200.0f);

	dsp_accumulator_stats(&filled, &dropped);
	zassert_equal(filled, 2);
	zassert_equal(dropped, 1);

	push_window(400.0f);
	expect_window(3, 400.0f);

	dsp_accumulator_stats(&filled, &dropped);
	zassert_equal(filled, 3);
	zassert_equal(dropped, 1);
	zassert_equal(dsp_accumulator_pending(), 0);
}

/* A drop that starts in the middle of a window discards the partial window:
 * the next delivered window holds only samples pushed after the slot freed. */
ZTEST(dsp_accumulator, test_partial_window_is_not_mixed_after_a_drop)
{
	push_window(100.0f);
	push_window(200.0f);
	for (int i = 0; i < WINDOW_LEN / 2; i++) {
		dsp_accumulator_push(300.0f + (float)i); /* dropped */
	}

	expect_window(1, 100.0f);
	push_window(400.0f);
	expect_window(2, 200.0f);
	expect_window(3, 400.0f);
	zassert_equal(dsp_accumulator_pending(), 0);
}

ZTEST_SUITE(dsp_accumulator, NULL, NULL, reset_module, NULL, NULL);
