/*
 * log_sink.h - the first sink: counts samples and keeps the newest one.
 * Context: log_sink() runs on the burst thread for every sample; the getters
 * run on app_wq (stats item). Nothing is logged per sample.
 */
#ifndef PIPELINE_LOG_SINK_H
#define PIPELINE_LOG_SINK_H

#include <stdint.h>
#include "imu/imu_types.h"

/** Sink, registered with burst_processor_register_sink(); runs on the burst
 *  thread. Two atomic increments and one copy under a spinlock. */
void log_sink(const imu_sample_t *s);

/** Called from any thread; copies the newest sample under the spinlock.
 *  @return samples seen since boot; 0 means @p out was not written. */
uint32_t log_sink_latest(imu_sample_t *out);

/** Called from any thread; atomic_get.
 *  @return samples that carried IMU_SAMPLE_DISCONTINUITY since boot. */
uint32_t log_sink_discontinuities(void);

#endif /* PIPELINE_LOG_SINK_H */
