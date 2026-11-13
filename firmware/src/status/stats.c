#include "status/stats.h"
#include "app/app_wq.h"
#include "imu/imu_fifo.h"
#include <zephyr/logging/log.h>
#ifdef CONFIG_TIMING_FUNCTIONS
#include <string.h>
#include <zephyr/timing/timing.h>
#include "dsp/dsp_accumulator.h"
#endif

LOG_MODULE_REGISTER(stats, LOG_LEVEL_INF);

static struct periodic_log s_stats_log = { .period_ms = 5000 };

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

        if (!s_timing_ready) {
                timing_init();
                timing_start();
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
