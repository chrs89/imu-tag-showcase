/*
 * fusion_sink.h - the sink that turns samples into an orientation.
 * Context: fusion_sink() runs on the burst thread for every sample and keeps
 * the sink contract (CLAUDE.md rule 3); the other functions are called from
 * app_wq. The result is read with orientation_service_get().
 */
#ifndef PIPELINE_FUSION_SINK_H_
#define PIPELINE_FUSION_SINK_H_
#include <stdint.h>
#include "imu/imu_types.h"

/** Counters of the sink; a copy, each field read atomically. */
struct fusion_sink_stats {
	uint32_t updates;      /* filter steps */
	uint32_t gaps;         /* samples with IMU_SAMPLE_DISCONTINUITY */
	uint32_t mag_rejected; /* 9-axis wanted, magnetometer sample not usable */
};

/** Sink: runs on the burst thread for every sample. Takes the latest
 *  magnetometer sample, removes the gyro bias, runs one filter step. Reads
 *  the configuration snapshot only on a discontinuity or after
 *  fusion_sink_config_changed(). */
void fusion_sink(const imu_sample_t *s);

/** Called from app_wq after app_config_set_fusion(); one atomic store. The
 *  sink applies mode and gain at its next sample. A rate change needs no
 *  call: the FIFO flush marks a discontinuity. */
void fusion_sink_config_changed(void);

/** Called from any thread; three atomic reads. */
void fusion_sink_get_stats(struct fusion_sink_stats *out);

#ifdef CONFIG_IMU_TAG_DEBUG_COUNTERS
/** Called from any thread; bench instrument for the gyro scale check.
 *  @return the integral of the bias-corrected gyro z rate since boot, in
 *  hundredths of a degree. */
int32_t fusion_sink_turn_z_cdeg(void);
#endif

#ifdef CONFIG_TIMING_FUNCTIONS
/** Called from app_wq (stats item); run time of the filter step and publish
 *  in nanoseconds since the last call: shortest, last, longest. All three
 *  are 0 when no step ran. Clears the shortest and the longest. */
void fusion_sink_step_ns_take(uint32_t *min_ns, uint32_t *last_ns, uint32_t *max_ns);
#endif

#endif /* PIPELINE_FUSION_SINK_H_ */
