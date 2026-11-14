/* imu_fifo.c - INT1 watermark path. Owns: s_fifo_sem, the INT1 GPIO callback,
 * the edge time, the row buffer, the decoder state and the margin counters.
 * Runs on: the INT1 ISR (edge time, notify) and the burst thread (wait,
 * burst, re-prime). Register access goes through imu_ext. */
#include "imu/imu_fifo.h"
#include <errno.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include "app/app_config.h"
#include "imu/imu_ext.h"

LOG_MODULE_REGISTER(imu_fifo, LOG_LEVEL_INF); /* Later: CONFIG_IMU_TAG_LOG_LEVEL, chapter 3 */

#define IMU_NODE DT_ALIAS(imu0)

/* The whole FIFO in whole pairs: the level is 9 bits, 511 rows at most. */
#define FIFO_BUF_ROWS 510u
#define FIFO_WM_MAX   254u

/* static state */
static K_SEM_DEFINE(s_fifo_sem, 0, 1);
static const struct gpio_dt_spec s_int1 = GPIO_DT_SPEC_GET(IMU_NODE, int1_gpios);
static struct gpio_callback s_int1_cb_data;

static atomic_t s_edge_ticks; /* low 32 bits of k_uptime_ticks() at the last INT1 edge */
static atomic_t s_edge_fresh; /* 1: an edge occurred that no burst has used yet */
static atomic_t s_wm_rows;    /* watermark in force */
static atomic_t s_mark_pending;

static uint8_t s_rows[FIFO_BUF_ROWS * FIFO_ROW_BYTES]; /* burst thread only */
static struct fifo_decode_state s_dec;                 /* burst thread only */

static atomic_t s_isr_count;
static atomic_t s_burst_count;
static atomic_t s_samples;
static atomic_t s_level_max;
static atomic_t s_overruns;
static atomic_t s_bus_errors;
static atomic_t s_anchor_fallbacks;
static atomic_t s_orphans;
static atomic_t s_unknown_tags;

#ifdef CONFIG_IMU_TAG_DEBUG_COUNTERS
static struct k_spinlock s_anchor_lock;
static struct imu_fifo_anchor_stats s_anchor_stats; /* under s_anchor_lock */
#endif

/* ISR: stores the time of the edge, then signals. The edge marks the row
 * that brought the FIFO level to the watermark. */
static void int1_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);
	atomic_set(&s_edge_ticks, (atomic_val_t)(uint32_t)k_uptime_ticks());
	atomic_set(&s_edge_fresh, 1);
	imu_fifo_notify();
	atomic_inc(&s_isr_count);
}

/* helpers: trust the caller */

/* Tick count from the recent past, low 32 bits, to microseconds that wrap
 * at 2^32. Thread context: the 64-bit conversion stays out of the ISR. */
static uint32_t ticks32_to_us(uint32_t t32)
{
	const int64_t now = k_uptime_ticks();
	const uint32_t age = (uint32_t)now - t32;

	return (uint32_t)k_ticks_to_us_floor64((uint64_t)(now - (int64_t)age));
}

static bool watermark_valid(uint16_t rows)
{
	return rows != 0u && rows <= FIFO_WM_MAX && (rows & 1u) == 0u;
}

#ifdef CONFIG_IMU_TAG_DEBUG_COUNTERS
/* Bench instrument: the status-read anchor against the edge anchor for the
 * same sample, in ticks. */
static void anchor_stats_add(uint32_t edge, uint32_t status, uint16_t pairs, uint16_t edge_idx,
			     uint16_t odr_hz)
{
	const int32_t periods = (int32_t)pairs - 1 - (int32_t)edge_idx;
	const int32_t span = (periods * CONFIG_SYS_CLOCK_TICKS_PER_SEC + odr_hz / 2) / odr_hz;
	const int32_t d = (int32_t)(status - edge) - span;
	unsigned int bin = 0;

	while (bin < IMU_FIFO_ANCHOR_BINS - 1 && d >= (int32_t)(1u << bin)) {
		bin++;
	}

	k_spinlock_key_t key = k_spin_lock(&s_anchor_lock);

	if (s_anchor_stats.count == 0u || d < s_anchor_stats.min_ticks) {
		s_anchor_stats.min_ticks = d;
	}
	if (s_anchor_stats.count == 0u || d > s_anchor_stats.max_ticks) {
		s_anchor_stats.max_ticks = d;
	}
	s_anchor_stats.sum_ticks += d;
	s_anchor_stats.hist[bin]++;
	s_anchor_stats.count++;
	k_spin_unlock(&s_anchor_lock, key);
}
#endif

/* public API: validate at the boundary */

int imu_fifo_init(uint16_t watermark_rows)
{
	int rc;

	if (!watermark_valid(watermark_rows)) {
		return -EINVAL;
	}
	if (!gpio_is_ready_dt(&s_int1)) {
		return -ENODEV;
	}
	rc = gpio_pin_configure_dt(&s_int1, GPIO_INPUT);
	if (rc != 0) {
		return rc;
	}
	atomic_set(&s_wm_rows, watermark_rows);
	gpio_init_callback(&s_int1_cb_data, int1_isr, BIT(s_int1.pin));
	rc = gpio_add_callback(s_int1.port, &s_int1_cb_data);
	if (rc != 0) {
		return rc;
	}
	rc = gpio_pin_interrupt_configure_dt(&s_int1, GPIO_INT_EDGE_TO_ACTIVE);
	if (rc != 0) {
		return rc;
	}

	rc = imu_ext_fifo_start((uint8_t)watermark_rows);
	if (rc != 0) {
		return rc;
	}
	LOG_INF("FIFO started, watermark %u rows", watermark_rows);
	return 0;
}

int imu_fifo_set_watermark(uint16_t watermark_rows)
{
	int rc;

	if (!watermark_valid(watermark_rows)) {
		return -EINVAL;
	}
	rc = imu_ext_fifo_set_watermark((uint8_t)watermark_rows);
	if (rc != 0) {
		return rc;
	}
	/* An edge raised at the old watermark does not mark the row the new
	 * one would: the next burst uses the status-read anchor. */
	atomic_set(&s_wm_rows, watermark_rows);
	atomic_set(&s_edge_fresh, 0);
	return 0;
}

void imu_fifo_notify(void)
{
	k_sem_give(&s_fifo_sem);
}

void imu_fifo_wait(void)
{
	k_sem_take(&s_fifo_sem, K_FOREVER);
}

void imu_fifo_do_burst(fifo_sample_fn on_sample, void *user)
{
	struct imu_fifo_info info;
	struct fifo_anchor anchor;
	app_config_t cfg;

	/* Taken before the read: the next edge can only come after the read
	 * has brought the level below the watermark. */
	const bool fresh = atomic_set(&s_edge_fresh, 0) != 0;
	const uint32_t edge = (uint32_t)atomic_get(&s_edge_ticks);
	const uint16_t wm = (uint16_t)atomic_get(&s_wm_rows);
	const uint16_t edge_idx = (uint16_t)(wm / 2u - 1u);

	const int rows = imu_ext_fifo_read(s_rows, FIFO_BUF_ROWS, &info);

	if (rows < 0) {
		atomic_inc(&s_bus_errors);
		return;
	}
	if ((atomic_val_t)info.level > atomic_get(&s_level_max)) {
		atomic_set(&s_level_max, info.level);
	}
	if (info.overrun || info.clipped) {
		atomic_inc(&s_overruns);
		s_dec.mark_next = true;
	}
	if (atomic_set(&s_mark_pending, 0) != 0) {
		s_dec.mark_next = true;
	}
	if (rows == 0) {
		return;
	}

	const uint16_t pairs = (uint16_t)rows / 2u;

	app_config_snapshot(&cfg); /* once per burst */

	/* After an overrun or a flush, row watermark - 1 of the buffer is no
	 * longer the row that raised the pin. */
	if (fresh && !s_dec.mark_next && pairs > edge_idx) {
		anchor = (struct fifo_anchor){.ts_us = ticks32_to_us(edge), .index = edge_idx};
#ifdef CONFIG_IMU_TAG_DEBUG_COUNTERS
		anchor_stats_add(edge, info.status_ticks, pairs, edge_idx, cfg.imu_odr_hz);
#endif
	} else {
		anchor = (struct fifo_anchor){.ts_us = ticks32_to_us(info.status_ticks),
					      .index = (uint16_t)(pairs - 1u)};
		atomic_inc(&s_anchor_fallbacks);
	}

	const struct fifo_decode_cfg dc = {
		.accel_scale = fifo_accel_scale(cfg.accel_range_g),
		.gyro_scale = FIFO_GYRO_SCALE_2000DPS,
		.odr_hz = cfg.imu_odr_hz,
	};
	const size_t n = fifo_decode(s_rows, (uint16_t)rows, &dc, &anchor, &s_dec, on_sample, user);

	atomic_add(&s_samples, (atomic_val_t)n);
	atomic_set(&s_orphans, (atomic_val_t)s_dec.orphans);
	atomic_set(&s_unknown_tags, (atomic_val_t)s_dec.unknown_tags);
	atomic_inc(&s_burst_count);
}

bool imu_fifo_int1_active(void)
{
	return gpio_pin_get_dt(&s_int1) > 0;
}

void imu_fifo_mark_discontinuity(void)
{
	atomic_set(&s_mark_pending, 1);
}

void imu_fifo_get_stats(struct imu_fifo_stats *out)
{
	out->isr_count = (uint32_t)atomic_get(&s_isr_count);
	out->bursts = (uint32_t)atomic_get(&s_burst_count);
	out->samples = (uint32_t)atomic_get(&s_samples);
	out->level_max = (uint32_t)atomic_get(&s_level_max);
	out->overruns = (uint32_t)atomic_get(&s_overruns);
	out->bus_errors = (uint32_t)atomic_get(&s_bus_errors);
	out->anchor_fallbacks = (uint32_t)atomic_get(&s_anchor_fallbacks);
	out->orphans = (uint32_t)atomic_get(&s_orphans);
	out->unknown_tags = (uint32_t)atomic_get(&s_unknown_tags);
}

uint32_t imu_fifo_burst_count(void)
{
	return (uint32_t)atomic_get(&s_burst_count);
}

uint32_t imu_fifo_isr_count(void)
{
	return (uint32_t)atomic_get(&s_isr_count);
}

#ifdef CONFIG_IMU_TAG_DEBUG_COUNTERS
void imu_fifo_anchor_stats_take(struct imu_fifo_anchor_stats *out)
{
	k_spinlock_key_t key = k_spin_lock(&s_anchor_lock);

	*out = s_anchor_stats;
	s_anchor_stats = (struct imu_fifo_anchor_stats){0};
	k_spin_unlock(&s_anchor_lock, key);
}
#endif
