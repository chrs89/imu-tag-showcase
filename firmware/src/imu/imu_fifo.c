/* imu_fifo.c - INT1 watermark path. Owns: s_fifo_sem, s_burst_count, the
 * INT1 GPIO callback. Runs on: the INT1 ISR (notify only) and the burst
 * thread (wait, burst, re-prime). Register access goes through imu_ext. */
#include "imu/imu_fifo.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/atomic.h>
#include <errno.h>

LOG_MODULE_REGISTER(imu_fifo, LOG_LEVEL_INF); /* Later: CONFIG_IMU_TAG_LOG_LEVEL, chapter 3 */

/* ── IMU device ──────────────────────────────────────────────────────────── */
#define IMU_NODE DT_ALIAS(imu0)

/* ── Module state ────────────────────────────────────────────────────────── */
static atomic_t s_isr_count = ATOMIC_INIT(0);

/* static state */
static K_SEM_DEFINE(s_fifo_sem, 0, 1);
static atomic_t s_burst_count = ATOMIC_INIT(0);

/* ── INT1 GPIO (FIFO threshold interrupt) ────────────────────────────────── */

static const struct gpio_dt_spec s_int1 =
	GPIO_DT_SPEC_GET(IMU_NODE, int1_gpios);

static struct gpio_callback s_int1_cb_data;

static void int1_isr(const struct device *port,
                     struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(port); ARG_UNUSED(cb); ARG_UNUSED(pins);
	imu_fifo_notify();
	atomic_inc(&s_isr_count);
}


/* helpers: trust the caller */

/* public API: validate at the boundary */

int imu_fifo_init(uint16_t watermark_rows)
{   
	ARG_UNUSED(watermark_rows);
	int rc;
	if (!device_is_ready(s_int1.port)) { return -ENODEV; }
	rc = gpio_pin_configure_dt(&s_int1, GPIO_INPUT);
	if (rc) { return rc; }
	rc = gpio_pin_interrupt_configure_dt(&s_int1, GPIO_INT_EDGE_TO_ACTIVE);
	if (rc) { return rc; }
	gpio_init_callback(&s_int1_cb_data, int1_isr, BIT(s_int1.pin));
	rc = gpio_add_callback(s_int1.port, &s_int1_cb_data);
	if (rc) { return rc; }
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

void imu_fifo_do_burst(void)
{
	LOG_DBG("burst: not implemented");
}

bool imu_fifo_int1_active(void)
{
	return (bool) (gpio_pin_get_dt(&s_int1) > 0);
}

uint32_t imu_fifo_burst_count(void)
{
	return (uint32_t)atomic_get(&s_burst_count);
}

uint32_t imu_fifo_isr_count(void)
{
	return (uint32_t)atomic_get(&s_isr_count);
}
