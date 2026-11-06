#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/app_version.h>
#include <zephyr/drivers/hwinfo.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Main thread. Logs version and reset cause, then idles. Never returns. */
int main(void)
{
        uint32_t cause = 0;
        int rc = hwinfo_get_reset_cause(&cause);

        if (rc != 0) {
                LOG_WRN("reset cause unavailable: %d", rc);
        }
        LOG_INF("IMU Tag showcase %s, reset cause 0x%08x", APP_VERSION_STRING, cause);

        while (1) {
                k_sleep(K_SECONDS(60));
        }

        return 0;
}