/* burst_processor.c - burst thread: drains the IMU FIFO once per INT1
 * watermark interrupt and calls the sinks for every sample. Owns: the thread,
 * its stack, the sink table. Runs on: one thread at priority 6; waits in
 * imu_fifo_wait() on the semaphore the INT1 ISR gives. INT2 (free-fall, tap,
 * motion) is a work item and never touches this thread. The cycle counter
 * for the pass timing is started by main. */
#include "pipeline/burst_processor.h"
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#ifdef CONFIG_TIMING_FUNCTIONS
#include <zephyr/timing/timing.h>
#endif
#include "imu/imu_fifo.h"

LOG_MODULE_REGISTER(imu_burst_proc, LOG_LEVEL_INF);

#define BURST_PROCESSOR_STACK    2048 /* unverified: re-measure with the thread analyzer */
#define BURST_PROCESSOR_PRIORITY 6    /* preemptible, above dsp_worker and app_wq */
#define BURST_SINK_MAX           4

/* static state */
static K_THREAD_STACK_DEFINE(s_bp_stack, BURST_PROCESSOR_STACK);
static struct k_thread s_bp_thread;
static atomic_t s_bursts = ATOMIC_INIT(0);

static burst_sink_fn s_sinks[BURST_SINK_MAX]; /* written before the thread starts */
static uint8_t s_sink_count;

#ifdef CONFIG_TIMING_FUNCTIONS
static atomic_t s_pass_last_ns;
static atomic_t s_pass_max_ns;
#endif

/* helpers: trust the caller */

/* The callback the decoder gets: one sample to every sink. */
static void dispatch_sample(const imu_sample_t *s, void *user)
{
	ARG_UNUSED(user);
	for (uint8_t i = 0; i < s_sink_count; i++) {
		s_sinks[i](s);
	}
}

static void burst_processor_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("burst_proc prio %d, stack %zu, sinks %u",
		k_thread_priority_get(k_current_get()), K_THREAD_STACK_SIZEOF(s_bp_stack),
		s_sink_count);

	while (1) {
		imu_fifo_wait();
#ifdef CONFIG_TIMING_FUNCTIONS
		timing_t t0 = timing_counter_get();
#endif
		imu_fifo_do_burst(dispatch_sample, NULL);
		if (imu_fifo_int1_active()) {
			/* re-prime: the level never fell, no new edge will come */
			imu_fifo_notify();
		}
		atomic_inc(&s_bursts);
#ifdef CONFIG_TIMING_FUNCTIONS
		timing_t t1 = timing_counter_get();
		const uint32_t ns = (uint32_t)timing_cycles_to_ns(timing_cycles_get(&t0, &t1));

		atomic_set(&s_pass_last_ns, (atomic_val_t)ns);
		if (ns > (uint32_t)atomic_get(&s_pass_max_ns)) {
			atomic_set(&s_pass_max_ns, (atomic_val_t)ns);
		}
#endif
	}
}

/* public API: validate at the boundary */

int burst_processor_register_sink(burst_sink_fn fn)
{
	if (fn == NULL) {
		return -EINVAL;
	}
	if (s_sink_count >= ARRAY_SIZE(s_sinks)) {
		return -ENOMEM;
	}
	s_sinks[s_sink_count++] = fn;
	return 0;
}

int burst_processor_init(void)
{
	k_thread_create(&s_bp_thread, s_bp_stack, K_THREAD_STACK_SIZEOF(s_bp_stack),
			burst_processor_thread_fn, NULL, NULL, NULL, BURST_PROCESSOR_PRIORITY,
			K_FP_REGS, K_NO_WAIT); /* the fusion sink computes in float */
	k_thread_name_set(&s_bp_thread, "burst_proc");

	return 0;
}

uint32_t burst_processor_count(void)
{
	return (uint32_t)atomic_get(&s_bursts);
}

#ifdef CONFIG_TIMING_FUNCTIONS
void burst_processor_pass_ns(uint32_t *last_ns, uint32_t *max_ns)
{
	*last_ns = (uint32_t)atomic_get(&s_pass_last_ns);
	*max_ns = (uint32_t)atomic_get(&s_pass_max_ns);
}
#endif
