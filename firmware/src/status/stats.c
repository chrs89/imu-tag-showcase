#include "status/stats.h"
#include "app/app_wq.h"
#include "fusion/orientation_service.h"
#include "imu/imu_fifo.h"
#include "imu/mag_poll.h"
#include "pipeline/fusion_sink.h"
#include "pipeline/burst_processor.h"
#include "pipeline/log_sink.h"
#include <zephyr/logging/log.h>
#ifdef CONFIG_TIMING_FUNCTIONS
#include <string.h>
#include <zephyr/timing/timing.h>
#include "dsp/dsp_accumulator.h"
#endif

LOG_MODULE_REGISTER(stats, LOG_LEVEL_INF);

static struct periodic_log s_stats_log = { .period_ms = 5000 };

#ifdef CONFIG_IMU_TAG_DEBUG_COUNTERS
/* Bench lines for the measurements page: margin counters, the newest sample,
 * and the two time anchors of a burst compared (ticks of the system clock). */
static void log_acquisition(void)
{
	struct imu_fifo_stats st;
	struct imu_fifo_anchor_stats an;
	imu_sample_t s;

	imu_fifo_get_stats(&st);
	LOG_INF("fifo: samples %u at tick %u, level max %u, overruns %u, orphans %u, "
		"unknown tags %u, bus errors %u, anchor fallbacks %u, settle dropped %u",
		st.samples, (uint32_t)k_uptime_ticks(), st.level_max, st.overruns, st.orphans,
		st.unknown_tags, st.bus_errors, st.anchor_fallbacks, st.settle_dropped);

	if (log_sink_latest(&s) != 0u) {
		LOG_INF("sink: discontinuities %u, last seq %u ts %u us, accel mm/s2 %d %d %d, "
			"gyro mrad/s %d %d %d",
			log_sink_discontinuities(), s.seq, s.timestamp_us,
			(int)(s.accel.x * 1000.0f), (int)(s.accel.y * 1000.0f),
			(int)(s.accel.z * 1000.0f), (int)(s.gyro.x * 1000.0f),
			(int)(s.gyro.y * 1000.0f), (int)(s.gyro.z * 1000.0f));
	}

	imu_fifo_anchor_stats_take(&an);
	if (an.count != 0u) {
		LOG_INF("anchor: n %u, thread minus isr ticks min %d max %d mean x100 %d",
			an.count, an.min_ticks, an.max_ticks,
			(int32_t)(an.sum_ticks * 100 / (int64_t)an.count));
		LOG_INF("anchor bins <1 <2 <4 <8 <16 <32 <64 <128 >=128: "
			"%u %u %u %u %u %u %u %u %u",
			an.hist[0], an.hist[1], an.hist[2], an.hist[3], an.hist[4], an.hist[5],
			an.hist[6], an.hist[7], an.hist[8]);
	}
#ifdef CONFIG_TIMING_FUNCTIONS
	uint32_t last_ns, max_ns;

	uint32_t read_last_ns, read_max_ns;

	burst_processor_pass_ns(&last_ns, &max_ns);
	imu_fifo_read_ns(&read_last_ns, &read_max_ns);
	LOG_INF("burst pass: last %u ns, max %u ns; of that fifo read: last %u ns, max %u ns",
		last_ns, max_ns, read_last_ns, read_max_ns);
#endif
}

/* Bench lines of the orientation path. Floats are printed as scaled integers:
 * a float argument of a format string would be promoted to double. */
static void log_fusion(void)
{
	struct fusion_sink_stats fs;
	struct mag_poll_stats ms;
	imu_orientation_t o;
	imu_mag_sample_t m;

	orientation_service_get(&o);
	fusion_sink_get_stats(&fs);
	mag_poll_get_stats(&ms);
	mag_poll_get(&m);

	LOG_INF("quat x1000 w %d x %d y %d z %d, flags 0x%02x, updates %u, gaps %u, "
		"mag rejected %u",
		(int)(o.w * 1000.0f), (int)(o.x * 1000.0f), (int)(o.y * 1000.0f),
		(int)(o.z * 1000.0f), o.flags, fs.updates, fs.gaps, fs.mag_rejected);
	LOG_INF("mag: uT x10 %d %d %d, norm2 uT2 %d, valid %d, polls %u, errors %u, stuck %u, "
		"turn z cdeg %d",
		(int)(m.mag.x * 10.0f), (int)(m.mag.y * 10.0f), (int)(m.mag.z * 10.0f),
		(int)(m.mag.x * m.mag.x + m.mag.y * m.mag.y + m.mag.z * m.mag.z), (int)m.valid,
		ms.polls, ms.errors, ms.stuck, fusion_sink_turn_z_cdeg());
#ifdef CONFIG_TIMING_FUNCTIONS
	uint32_t min_ns, last_ns, max_ns, zero_first;

	fusion_sink_step_ns_take(&min_ns, &last_ns, &max_ns);
	LOG_INF("fusion step: min %u ns, last %u ns, max %u ns, zero readings %u (first at "
		"update %u)",
		min_ns, last_ns, max_ns, fusion_sink_step_zero_count(&zero_first), zero_first);
#endif
}
#else
static void log_acquisition(void)
{
}

static void log_fusion(void)
{
}
#endif

/* Runs on app_wq every period_ms. Logs the INT1 counters and re-arms itself. */
static void periodic_log_fn(struct k_work *work)
{
        struct k_work_delayable *dwork = k_work_delayable_from_work(work);
        struct periodic_log *self = CONTAINER_OF(dwork, struct periodic_log, dwork);

#ifdef CONFIG_TIMING_FUNCTIONS
        /* M-05-2, debug build only: cost of one LOG_INF from a work handler.
         * The result is logged on the next tick so the extra log lines stay
         * outside the measured window. */
        static bool s_timing_ready;
        static uint64_t s_last_cycles;
        timing_t t0, t1;

        /* main started the cycle counter before any work item ran */
        if (!s_timing_ready) {
                s_timing_ready = true;
                LOG_INF("busy %d", k_work_busy_get(&self->dwork.work));
        } else {
                LOG_INF("log cost %u cycles, %u ns", (uint32_t)s_last_cycles,
                        (uint32_t)timing_cycles_to_ns(s_last_cycles));
        }

        /* M-07-3, debug build only: time of one copy of a full DSP window,
         * the longest the consumer can hold a slot of dsp_accumulator. The
         * source changes every tick and the destination is read back, so
         * the compiler cannot drop the copy. */
        static float s_copy_src[DSP_MAX_WINDOW_SAMPLES];
        static float s_copy_dst[DSP_MAX_WINDOW_SAMPLES];
        timing_t c0, c1;
        uint64_t copy_cycles;

        s_copy_src[0] += 1.0f;
        c0 = timing_counter_get();
        memcpy(s_copy_dst, s_copy_src, sizeof(s_copy_src));
        c1 = timing_counter_get();
        copy_cycles = timing_cycles_get(&c0, &c1);
        LOG_INF("memcpy %u B: %u cycles, %u ns, check %d", (uint32_t)sizeof(s_copy_src),
                (uint32_t)copy_cycles, (uint32_t)timing_cycles_to_ns(copy_cycles),
                (int)s_copy_dst[0]);

        t0 = timing_counter_get();
#endif

        LOG_INF("int1 isr %u, bursts %u", imu_fifo_isr_count(), imu_fifo_burst_count());
        log_acquisition();
        log_fusion();

#ifdef CONFIG_TIMING_FUNCTIONS
        t1 = timing_counter_get();
        s_last_cycles = timing_cycles_get(&t0, &t1);
#endif

        k_work_schedule_for_queue(app_wq_get(), dwork, K_MSEC(self->period_ms));
}

void stats_init(void)
{
        k_work_init_delayable(&s_stats_log.dwork, periodic_log_fn);
        k_work_schedule_for_queue(app_wq_get(), &s_stats_log.dwork,
                                  K_MSEC(s_stats_log.period_ms));
}
