#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/app_version.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/settings/settings.h>
#ifdef CONFIG_TIMING_FUNCTIONS
#include <zephyr/timing/timing.h>
#endif
#ifdef CONFIG_THREAD_ANALYZER
#include <zephyr/debug/thread_analyzer.h>
#endif


#include "app/app_wq.h"
#include "pipeline/burst_processor.h"
#include "imu/imu_ext.h"
#include "imu/imu_fifo.h"
#include "app/app_config.h"
#include "fusion/orientation_service.h"
#include "imu/imu_calibration.h"
#include "imu/mag_poll.h"
#include "pipeline/fusion_sink.h"
#include "pipeline/log_sink.h"
#include "status/stats.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Boot line with the calibration in force: gyro bias in mdps, magnetometer
 * bias in tenths of a uT, scale times 1000. All zero and 1000 means defaults. */
static void log_calibration(void)
{
        struct imu_cal_values c;

        imu_cal_get_values(&c);
        LOG_INF("cal: gyro bias mdps %d %d %d, mag bias uT x10 %d %d %d, scale x1000 %d %d %d",
                (int)(c.gyro_bias[0] * 57295.78f), (int)(c.gyro_bias[1] * 57295.78f),
                (int)(c.gyro_bias[2] * 57295.78f), (int)(c.mag_bias[0] * 10.0f),
                (int)(c.mag_bias[1] * 10.0f), (int)(c.mag_bias[2] * 10.0f),
                (int)(c.mag_scale[0] * 1000.0f), (int)(c.mag_scale[1] * 1000.0f),
                (int)(c.mag_scale[2] * 1000.0f));
}

/* ---- Temporary bench hook (wiki chapters 9 and 10). Off by default:
 * BENCH_RUN_NONE compiles it out. It goes away when the command path of
 * chapter 12 can trigger the same calls (docs/BACKLOG.md). ----
 * Pick one run, build with debug.conf, flash, read the RTT log. The item runs
 * on app_wq, the only context that may change the configuration; the blocker
 * runs on the system work queue. Nothing happens before BENCH_START_S, so the
 * first stats lines show an untouched boot. */
#define BENCH_RUN_NONE        0
#define BENCH_RUN_960HZ       1 /* 960 Hz at BENCH_START_S */
#define BENCH_RUN_CAL         2 /* magnetometer run 30 s, save; gyro run at rest, save */
#define BENCH_RUN_6AXIS       3 /* fusion 6-axis at BENCH_START_S */
#define BENCH_RUN_RESET       4 /* filter to the identity at BENCH_START_S */
#define BENCH_RUN_BLOCK_100MS 5 /* 960 Hz, then one 100 ms block on sysworkq */
#define BENCH_RUN_BLOCK_600US 6 /* 960 Hz, then 600 us blocks every 7 ms on sysworkq */

#ifndef BENCH_RUN
#define BENCH_RUN BENCH_RUN_NONE
#endif
#define BENCH_START_S 20

#if BENCH_RUN != BENCH_RUN_NONE
static void bench_fn(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(s_bench, bench_fn);

#define BENCH_AT_960                                                                              \
	(BENCH_RUN == BENCH_RUN_960HZ || BENCH_RUN == BENCH_RUN_BLOCK_100MS ||                    \
	 BENCH_RUN == BENCH_RUN_BLOCK_600US)

#if BENCH_AT_960
static void bench_go_960(void)
{
        int rc = app_config_set_imu(960, 2);

        rc = (rc != 0) ? rc : imu_ext_set_odr(960);
        imu_fifo_mark_discontinuity();
        LOG_INF("bench: 960 Hz, rc %d", rc);
}
#endif

#if BENCH_RUN == BENCH_RUN_BLOCK_100MS || BENCH_RUN == BENCH_RUN_BLOCK_600US
static void bench_block_fn(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(s_bench_block, bench_block_fn);

/* Runs on the system work queue (cooperative): holds every thread off. */
static void bench_block_fn(struct k_work *work)
{
        ARG_UNUSED(work);
#if BENCH_RUN == BENCH_RUN_BLOCK_100MS
        k_busy_wait(100000);
        LOG_INF("bench: 100 ms block done");
#else
        k_busy_wait(600);
        k_work_schedule(&s_bench_block, K_MSEC(7));
#endif
}
#endif

/* Runs on app_wq. step counts the calls; each case arms the next one. */
static void bench_fn(struct k_work *work)
{
        static int step;
        int next_s = 0;

        ARG_UNUSED(work);
#if BENCH_RUN == BENCH_RUN_960HZ
        if (step == 0) {
                bench_go_960();
        }
#elif BENCH_RUN == BENCH_RUN_BLOCK_100MS || BENCH_RUN == BENCH_RUN_BLOCK_600US
        if (step == 0) {
                bench_go_960();
                next_s = 20;
        } else if (step == 1) {
                LOG_INF("bench: blocker starts");
                k_work_schedule(&s_bench_block, K_NO_WAIT);
        }
#elif BENCH_RUN == BENCH_RUN_CAL
        int rc;

        if (step == 0) {
                imu_cal_mag_start();
                LOG_INF("bench: mag run started, turn the tag through all orientations for 30 s");
                next_s = 30;
        } else if (step == 1) {
                rc = imu_cal_mag_finish();
                LOG_INF("bench: mag finish rc %d", rc);
                if (rc == 0) {
                        /* New values move the field direction: align again
                         * instead of converging to the new heading. */
                        orientation_service_request_reset();
                }
                rc = imu_cal_mag_save();
                LOG_INF("bench: mag save rc %d; put the tag down, gyro run in 10 s", rc);
                next_s = 10;
        } else if (step == 2) {
                imu_cal_gyro_start();
                LOG_INF("bench: gyro run started, keep the tag at rest");
                next_s = 3;
        } else if (step == 3) {
                rc = imu_cal_gyro_save();
                LOG_INF("bench: gyro state %d (2 done, 3 moved), save rc %d",
                        (int)imu_cal_gyro_state(), rc);
                log_calibration();
        }
#elif BENCH_RUN == BENCH_RUN_6AXIS
        if (step == 0) {
                const int rc = app_config_set_fusion(FUSION_MODE_6AXIS, 100);

                fusion_sink_config_changed();
                LOG_INF("bench: fusion 6-axis, rc %d", rc);
        }
#elif BENCH_RUN == BENCH_RUN_RESET
        if (step == 0) {
                /* identity, not a re-alignment: the correction rate shows (M-10-5) */
                orientation_service_request_identity();
                LOG_INF("bench: filter set to the identity");
        }
#endif
        step++;
        if (next_s > 0) {
                k_work_schedule_for_queue(app_wq_get(), &s_bench, K_SECONDS(next_s));
        }
}
#endif /* BENCH_RUN != BENCH_RUN_NONE */
/* ---- end of the temporary bench hook ---- */

/* Main thread. Logs version and reset cause, starts the modules, then idles.
 * stas.c logs the counters every 5000 ms */
int main(void)
{
        uint32_t cause = 0;
        app_config_t cfg;
        int rc = hwinfo_get_reset_cause(&cause);



        if (rc != 0) {
                LOG_WRN("reset cause unavailable: %d", rc);
        }
        LOG_INF("IMU Tag showcase %s, reset cause 0x%08x", APP_VERSION_STRING, cause);

#ifdef CONFIG_TIMING_FUNCTIONS
        /* Debug build only: the cycle counter every timing hook reads. */
        timing_init();
        timing_start();
#endif

        app_wq_init();
        stats_init();

        /* Calibration values, before the first sample reaches the filter. The
         * handler is static, so only the load order matters here. */
        rc = settings_subsys_init();
        if (rc == 0) {
                rc = settings_load_subtree("imu_tag/cal");
        }
        if (rc != 0) {
                LOG_ERR("calibration not loaded: %d", rc);
        }
        log_calibration();

        /* Boot health check: the upstream driver probed the chip before main(). */
        if (!device_is_ready(DEVICE_DT_GET(DT_ALIAS(imu0)))) {
                LOG_ERR("LSM6DSV16X driver not ready");
        }
        rc = imu_ext_init();
        if (rc != 0) {
                LOG_ERR("imu_ext_init failed: %d", rc);
        } else {
                LOG_INF("imu_ext_init returned 0");
        }

        rc = mag_poll_init();
        if (rc != 0) {
                LOG_ERR("mag_poll_init failed: %d", rc);
        }

        /* Sinks first: the table is read-only once the thread exists. */
        rc = burst_processor_register_sink(fusion_sink);
        if (rc != 0) {
                LOG_ERR("fusion sink not registered: %d", rc);
        }
        rc = burst_processor_register_sink(log_sink);
        if (rc != 0) {
                LOG_ERR("log sink not registered: %d", rc);
        }
        rc = burst_processor_init();
        if (rc != 0) {
                LOG_ERR("burst processor init failed: %d", rc);
        }

        /* Starts the FIFO; from here on INT1 wakes the burst thread. */
        app_config_snapshot(&cfg);
        rc = imu_fifo_init((uint16_t)cfg.acq_profile);
        if (rc != 0) {
                LOG_ERR("imu_fifo_init failed: %d", rc);
        } else {
                LOG_INF("imu_fifo_init returned 0");
        }

#if BENCH_RUN != BENCH_RUN_NONE
        LOG_WRN("bench run %d armed, starts at %d s", BENCH_RUN, BENCH_START_S);
        k_work_schedule_for_queue(app_wq_get(), &s_bench, K_SECONDS(BENCH_START_S));
#endif

#ifdef CONFIG_THREAD_ANALYZER
        /* Debug build only (-DEXTRA_CONF_FILE=debug.conf): stack baseline, chapter 3. */
        k_sleep(K_SECONDS(5));
        thread_analyzer_print(0);
#if BENCH_RUN != BENCH_RUN_NONE
        /* Temporary, with the bench hook: stacks again once the run has settled. */
        k_sleep(K_SECONDS(BENCH_START_S + 55));
        thread_analyzer_print(0);
#endif
#endif


        k_sleep(K_FOREVER);


        return 0;
}