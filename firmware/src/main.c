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

#ifdef CONFIG_THREAD_ANALYZER
        /* Debug build only (-DEXTRA_CONF_FILE=debug.conf): stack baseline, chapter 3. */
        k_sleep(K_SECONDS(5));
        thread_analyzer_print(0);
#endif


        k_sleep(K_FOREVER);


        return 0;
}