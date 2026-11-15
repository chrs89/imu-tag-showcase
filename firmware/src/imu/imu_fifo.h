/*
 * imu_fifo.h - INT1 watermark path: edge time and signal in the ISR, burst
 * read, time anchor, decode hand-off, margin counters.
 * Context: the INT1 ISR stores the edge time and gives s_fifo_sem; the burst
 * thread waits on it and reads the FIFO through imu_ext. Units: watermark in
 * FIFO rows, two per sample (docs/MEASUREMENTS.md M-09-0), time in
 * microseconds of MCU uptime.
 * See docs/adr/ADR-001.
 */
#ifndef IMU_FIFO_H_
#define IMU_FIFO_H_
#include <stdint.h>
#include <stdbool.h>
#include "imu/fifo_decode.h"

/** Counters of the acquisition path; a copy, each field read atomically. */
struct imu_fifo_stats {
	uint32_t isr_count;        /* INT1 edges seen */
	uint32_t bursts;           /* bursts decoded */
	uint32_t samples;          /* samples passed to the sinks */
	uint32_t settle_dropped;   /* samples dropped while the sensor settled after the start */
	uint32_t level_max;        /* highest FIFO level at a status read; 511 minus this is the margin */
	uint32_t overruns;         /* bursts with a sensor overrun or a clipped read */
	uint32_t bus_errors;       /* bursts skipped after a bus error */
	uint32_t anchor_fallbacks; /* bursts timed by the status read, not the INT1 edge */
	uint32_t orphans;          /* rows without a partner at the end of a burst */
	uint32_t unknown_tags;     /* rows that are neither gyro nor accel */
};

/** Called from main at boot, after imu_ext_init() and burst_processor_init();
 *  registers the INT1 GPIO callback and starts the FIFO with the watermark.
 *  @return 0, -EINVAL for a watermark that is 0, odd or above 254, -ENODEV if
 *  the INT1 GPIO is not ready, -EIO on a bus error. */
int imu_fifo_init(uint16_t watermark_rows);

/** Called from app_wq; changes the watermark while the FIFO runs. The next
 *  burst is timed by its status read.
 *  @return 0, -EINVAL for a watermark that is 0, odd or above 254, -EIO on a
 *  bus error. */
int imu_fifo_set_watermark(uint16_t watermark_rows);

/** Called from the INT1 ISR after it stored the edge time, from the burst
 *  thread to re-prime, or from a test; only gives s_fifo_sem. */
void imu_fifo_notify(void);

/** Called from the burst thread; blocks on s_fifo_sem with K_FOREVER until
 *  the ISR or the re-prime gives it. */
void imu_fifo_wait(void);

/** Called from the burst thread; takes s_imu_lock through imu_ext once,
 *  picks the time anchor of the burst, decodes the rows and calls
 *  @p on_sample once per sample. */
void imu_fifo_do_burst(fifo_sample_fn on_sample, void *user);

#ifdef CONFIG_TIMING_FUNCTIONS
/** Called from app_wq (stats item); bench instrument. Elapsed time of the
 *  FIFO read of one burst (lock, status read, row read) in nanoseconds: the
 *  last burst and the longest so far. The rest of a burst pass is decode
 *  and sinks. */
void imu_fifo_read_ns(uint32_t *last_ns, uint32_t *max_ns);
#endif

/** Called from the burst thread; reads the INT1 pin, touches no kernel object.
 *  @return true while the FIFO still holds at least the watermark. */
bool imu_fifo_int1_active(void);

/** Called from any thread after the FIFO was flushed (rate or range change);
 *  one atomic store. The next sample carries IMU_SAMPLE_DISCONTINUITY. */
void imu_fifo_mark_discontinuity(void);

/** Called from any thread; fills @p out with the counters. */
void imu_fifo_get_stats(struct imu_fifo_stats *out);

/** Called from any thread; atomic_get on s_burst_count.
 *  @return bursts completed since boot. */
uint32_t imu_fifo_burst_count(void);

/** Called from any thread; atomic_get on s_isr_count.
 *  @return INT1 ISR invocations since boot. */
uint32_t imu_fifo_isr_count(void);

#ifdef CONFIG_IMU_TAG_DEBUG_COUNTERS
#define IMU_FIFO_ANCHOR_BINS 9

/** How much later the status-read anchor would have stamped the edge sample,
 *  in system clock ticks, over the bursts since the last take. Bins: below
 *  1, 2, 4, 8, 16, 32, 64, 128 ticks, and 128 or more. */
struct imu_fifo_anchor_stats {
	uint32_t count;
	int32_t min_ticks;
	int32_t max_ticks;
	int64_t sum_ticks;
	uint32_t hist[IMU_FIFO_ANCHOR_BINS];
};

/** Called from app_wq (stats item); copies the anchor statistics under a
 *  spinlock and clears them. */
void imu_fifo_anchor_stats_take(struct imu_fifo_anchor_stats *out);
#endif

#endif /* IMU_FIFO_H_ */
