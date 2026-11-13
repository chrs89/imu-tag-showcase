/* imu_ext.c - the only owner of the LSM6DSV16X registers. Owns: s_imu_lock,
 * the SPI spec and the ST HAL context (ADR-004). Runs on: boot, the burst
 * thread, app_wq and the system work queue (INT2 event item); every register
 * access happens under s_imu_lock. The upstream driver probes the chip before
 * main(); after that only this module talks to it. */
#include "imu/imu_ext.h"
#include <errno.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include "lsm6dsv16x_reg.h" /* ST HAL, on the include path through USE_STDC_LSM6DSV16X */

LOG_MODULE_REGISTER(imu_ext, LOG_LEVEL_INF); /* Later: CONFIG_IMU_TAG_LOG_LEVEL, chapter 3 */

/* Same operation word as the upstream driver: 8-bit words, SPI mode 3,
 * matching spi-cpol and spi-cpha in the board file. */
#define IMU_SPI_OP   (SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_MODE_CPOL | SPI_MODE_CPHA)
#define IMU_SPI_READ 0x80u /* bit 7 of the address byte selects a read */

/* static state */
static const struct spi_dt_spec s_spi = SPI_DT_SPEC_GET(DT_ALIAS(imu0), IMU_SPI_OP);
static K_MUTEX_DEFINE(s_imu_lock);

/* Output rate and FIFO batch rate always change together (CLAUDE.md rule 2). */
static const struct {
	uint16_t hz;
	lsm6dsv16x_data_rate_t odr;
	lsm6dsv16x_fifo_xl_batch_t xl_batch;
	lsm6dsv16x_fifo_gy_batch_t gy_batch;
} s_rates[] = {
	{240, LSM6DSV16X_ODR_AT_240Hz, LSM6DSV16X_XL_BATCHED_AT_240Hz,
	 LSM6DSV16X_GY_BATCHED_AT_240Hz},
	{480, LSM6DSV16X_ODR_AT_480Hz, LSM6DSV16X_XL_BATCHED_AT_480Hz,
	 LSM6DSV16X_GY_BATCHED_AT_480Hz},
	{960, LSM6DSV16X_ODR_AT_960Hz, LSM6DSV16X_XL_BATCHED_AT_960Hz,
	 LSM6DSV16X_GY_BATCHED_AT_960Hz},
};

/* helpers: trust the caller (hold s_imu_lock) */

/* Bus read with the exact signature of the HAL's stmdev_read_ptr, so the
 * context needs no function-pointer cast and the length stays 16 bits. */
static int32_t imu_spi_read(void *handle, uint8_t reg, uint8_t *data, uint16_t len)
{
	const struct spi_dt_spec *spi = handle;
	uint8_t addr = reg | IMU_SPI_READ;
	const struct spi_buf tx_buf = {.buf = &addr, .len = 1};
	const struct spi_buf_set tx = {.buffers = &tx_buf, .count = 1};
	const struct spi_buf rx_buf[2] = {
		{.buf = NULL, .len = 1}, /* skip the byte clocked in during the address */
		{.buf = data, .len = len},
	};
	const struct spi_buf_set rx = {.buffers = rx_buf, .count = 2};

	return spi_transceive_dt(spi, &tx, &rx);
}

/* Bus write with the exact signature of the HAL's stmdev_write_ptr. */
static int32_t imu_spi_write(void *handle, uint8_t reg, const uint8_t *data, uint16_t len)
{
	const struct spi_dt_spec *spi = handle;
	uint8_t addr = reg & (uint8_t)~IMU_SPI_READ;
	const struct spi_buf tx_buf[2] = {
		{.buf = &addr, .len = 1},
		{.buf = (uint8_t *)data, .len = len}, /* spi_buf.buf is void *; only read */
	};
	const struct spi_buf_set tx = {.buffers = tx_buf, .count = 2};

	return spi_write_dt(spi, &tx);
}

static void imu_mdelay(uint32_t ms)
{
	k_msleep((int32_t)ms);
}

static const stmdev_ctx_t s_ctx = {
	.write_reg = imu_spi_write,
	.read_reg = imu_spi_read,
	.mdelay = imu_mdelay,
	.handle = (void *)&s_spi, /* read-only use; the HAL passes it back unchanged */
};

/* Rate change as one register sequence. Runs to the end on every path so the
 * caller can unlock in one place; the first error is kept. The FIFO returns
 * to the mode it was in, so a stopped FIFO stays stopped. */
static int32_t set_rate_locked(size_t idx)
{
	lsm6dsv16x_fifo_mode_t mode = LSM6DSV16X_BYPASS_MODE;
	int32_t rc = lsm6dsv16x_fifo_mode_get(&s_ctx, &mode);

	rc = (rc != 0) ? rc : lsm6dsv16x_fifo_mode_set(&s_ctx, LSM6DSV16X_BYPASS_MODE);
	rc = (rc != 0) ? rc : lsm6dsv16x_xl_data_rate_set(&s_ctx, s_rates[idx].odr);
	rc = (rc != 0) ? rc : lsm6dsv16x_gy_data_rate_set(&s_ctx, s_rates[idx].odr);
	rc = (rc != 0) ? rc : lsm6dsv16x_fifo_xl_batch_set(&s_ctx, s_rates[idx].xl_batch);
	rc = (rc != 0) ? rc : lsm6dsv16x_fifo_gy_batch_set(&s_ctx, s_rates[idx].gy_batch);
	rc = (rc != 0) ? rc : lsm6dsv16x_fifo_mode_set(&s_ctx, mode);

	return rc;
}

/* public API: validate at the boundary */

int imu_ext_init(void)
{
	uint8_t id = 0;
	int32_t rc;

	if (!spi_is_ready_dt(&s_spi)) {
		return -ENODEV;
	}

	k_mutex_lock(&s_imu_lock, K_FOREVER);
	rc = lsm6dsv16x_device_id_get(&s_ctx, &id);
	k_mutex_unlock(&s_imu_lock);

	if (rc != 0) {
		return -EIO;
	}
	if (id != LSM6DSV16X_ID) {
		return -ENODEV;
	}
	LOG_INF("imu_ext: WHO_AM_I 0x%02x", id);
	return 0;
}

int imu_ext_set_odr(uint16_t odr_hz)
{
	size_t idx;
	int32_t rc;

	for (idx = 0; idx < ARRAY_SIZE(s_rates); idx++) {
		if (s_rates[idx].hz == odr_hz) {
			break;
		}
	}
	if (idx == ARRAY_SIZE(s_rates)) {
		return -EINVAL;
	}

	k_mutex_lock(&s_imu_lock, K_FOREVER);
	rc = set_rate_locked(idx);
	k_mutex_unlock(&s_imu_lock);

	return (rc != 0) ? -EIO : 0;
}

int imu_ext_set_accel_range(uint8_t range_g)
{
	ARG_UNUSED(range_g);
	return -ENOSYS;
}

int imu_ext_fifo_read(uint8_t *buf, uint16_t max_rows)
{
	ARG_UNUSED(buf);
	ARG_UNUSED(max_rows);
	return -ENOSYS;
}

int imu_ext_read_event_status(uint8_t *status)
{
	ARG_UNUSED(status);
	return -ENOSYS;
}
