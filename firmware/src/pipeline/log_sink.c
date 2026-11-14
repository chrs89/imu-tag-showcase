/* log_sink.c - first sink of the pipeline. Owns: s_latest and its spinlock,
 * two counters. Runs on: the burst thread (log_sink) and any reader (the
 * getters). Shows the sink contract: constant time, no sleep, no log line
 * per sample; the stats item prints from the getters. */
#include "pipeline/log_sink.h"
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/atomic.h>

/* static state */
static struct k_spinlock s_latest_lock;
static imu_sample_t s_latest; /* under s_latest_lock: a sample is several words */
static atomic_t s_samples;
static atomic_t s_discontinuities;

/* public API */

void log_sink(const imu_sample_t *s)
{
	k_spinlock_key_t key = k_spin_lock(&s_latest_lock);

	s_latest = *s;
	k_spin_unlock(&s_latest_lock, key);

	atomic_inc(&s_samples);
	if ((s->flags & IMU_SAMPLE_DISCONTINUITY) != 0u) {
		atomic_inc(&s_discontinuities);
	}
}

uint32_t log_sink_latest(imu_sample_t *out)
{
	const uint32_t n = (uint32_t)atomic_get(&s_samples);

	if (n != 0u) {
		k_spinlock_key_t key = k_spin_lock(&s_latest_lock);

		*out = s_latest;
		k_spin_unlock(&s_latest_lock, key);
	}
	return n;
}

uint32_t log_sink_discontinuities(void)
{
	return (uint32_t)atomic_get(&s_discontinuities);
}
