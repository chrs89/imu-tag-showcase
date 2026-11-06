#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
	LOG_INF("IMU Tag showcase: boot");

	while (1) {
		k_sleep(K_SECONDS(60));
	}

	return 0;
}
