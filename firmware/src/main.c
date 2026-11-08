#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/app_version.h>
#include <zephyr/drivers/hwinfo.h>
#ifdef CONFIG_THREAD_ANALYZER
#include <zephyr/debug/thread_analyzer.h>
#endif

#include "pipeline/burst_processor.h"
#include "imu/imu_fifo.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Main thread. Logs version and reset cause, starts the modules, then idles.
 * With CONFIG_IMU_TAG_DEBUG_COUNTERS it logs the INT1 counters every 5 s. */
int main(void)
{
        uint32_t cause = 0;
        int rc = hwinfo_get_reset_cause(&cause);

        if (rc != 0) {
                LOG_WRN("reset cause unavailable: %d", rc);
        }
        LOG_INF("IMU Tag showcase %s, reset cause 0x%08x", APP_VERSION_STRING, cause);

        rc = burst_processor_init();
        if (rc != 0) {
                LOG_ERR("burst processor init failed: %d", rc);
        }

        rc = imu_fifo_init(0);
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

#ifdef CONFIG_IMU_TAG_DEBUG_COUNTERS
        while (1) {
                k_sleep(K_SECONDS(5));
                LOG_INF("int1 isr %u, bursts %u",
                        imu_fifo_isr_count(), imu_fifo_burst_count());
        }
#else
        k_sleep(K_FOREVER);
#endif

        return 0;
}