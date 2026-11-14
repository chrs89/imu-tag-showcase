/*
 * burst_processor.h - the acquisition thread and its sink table.
 * Context: one thread at priority 6. A sink runs on that thread for every
 * sample, up to 960 times a second: bounded work, no sleep, no wait on a lock
 * another thread holds for long, no log line per sample (CLAUDE.md rule 3).
 */
#ifndef PIPELINE_BURST_PROCESSOR_H
#define PIPELINE_BURST_PROCESSOR_H

#include <stdint.h>
#include "imu/imu_types.h"

typedef void (*burst_sink_fn)(const imu_sample_t *s);

/** Called from main before burst_processor_init(); not thread-safe by design:
 *  the table is filled before the thread exists and only read afterwards.
 *  @return 0, -EINVAL for a NULL sink, -ENOMEM when the table is full. */
int burst_processor_register_sink(burst_sink_fn fn);

/** Called from main at boot, after the sinks are registered; creates the
 *  burst thread.
 *  @return 0. */
int burst_processor_init(void);

/** Called from any thread; atomic_get on s_bursts.
 *  @return loop passes completed since boot. */
uint32_t burst_processor_count(void);

#ifdef CONFIG_TIMING_FUNCTIONS
/** Called from app_wq (stats item); run time of one loop pass from the wake
 *  to the next wait, in nanoseconds: the last pass and the longest so far. */
void burst_processor_pass_ns(uint32_t *last_ns, uint32_t *max_ns);
#endif

#endif /* PIPELINE_BURST_PROCESSOR_H */
