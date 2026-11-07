#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/app_version.h>
#include <zephyr/drivers/hwinfo.h>
#include <pipeline/burst_processor.h>
#ifdef CONFIG_THREAD_ANALYZER
#include <zephyr/debug/thread_analyzer.h>
#endif

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

	rc = burst_processor_init();
	if (rc != 0) {
		LOG_ERR("burst processor init failed: %d", rc);
	}

#ifdef CONFIG_THREAD_ANALYZER
	/* Debug build only (-DEXTRA_CONF_FILE=debug.conf): stack baseline, chapter 3. */
	k_sleep(K_SECONDS(5));
	thread_analyzer_print(0);
#endif

	while (1) {
		k_sleep(K_SECONDS(60));
	}

	return 0;
}
