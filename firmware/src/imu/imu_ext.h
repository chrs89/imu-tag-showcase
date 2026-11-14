/*
 * imu_ext.h - the only owner of the LSM6DSV16X registers.
 * Context: boot (main), the burst thread, app_wq (commands) and the system
 * work queue (INT2 event item), all serialised by s_imu_lock. Units: ODR in
 * Hz, accel range in g.
 * See docs/adr/ADR-004.
 */
#ifndef IMU_EXT_H_
#define IMU_EXT_H_
#include <stdint.h>
#include <stdbool.h>

/** Called from main at boot; takes s_imu_lock. Probes the chip over SPI
 *  through this module's own bus context.
 *  @return 0, -ENODEV if the SPI bus or chip select is not ready or the chip
 *  ID is not 0x70, -EIO on a bus error. */
int imu_ext_init(void);

/** Called from app_wq; takes s_imu_lock once. Puts the FIFO in bypass, sets
 *  the accel and gyro output rate and both FIFO batch rates to @p odr_hz,
 *  then returns the FIFO to the mode it was in. The bypass empties the FIFO:
 *  the caller marks the sample stream with imu_fifo_mark_discontinuity().
 *  @return 0, -EINVAL for a rate other than 240, 480 or 960 Hz, -EIO on a
 *  bus error. */
int imu_ext_set_odr(uint16_t odr_hz);

/** Called from app_wq; takes s_imu_lock.
 *  @return 0, -EINVAL for a range other than 2, 4, 8 or 16 g, -EIO on a bus error. */
int imu_ext_set_accel_range(uint8_t range_g);

/** What the status read of one burst found. */
struct imu_fifo_info {
	uint16_t level;        /* rows in the FIFO at the status read */
	bool overrun;          /* the sensor overwrote rows before this read */
	bool clipped;          /* more rows than the caller's buffer holds */
	uint32_t status_ticks; /* low 32 bits of k_uptime_ticks() right after the status read */
};

/** Called from main at boot through imu_fifo_init(); takes s_imu_lock once.
 *  Empties the FIFO, sets the watermark, batches accel and gyro at the
 *  current output rate, routes the FIFO threshold to INT1 and starts the
 *  FIFO in stream mode.
 *  @return 0, -EINVAL for a watermark that is 0 or odd, -EIO on a bus error. */
int imu_ext_fifo_start(uint8_t watermark_rows);

/** Called from app_wq; takes s_imu_lock for one register write. Safe while
 *  the FIFO is running.
 *  @return 0, -EINVAL for a watermark that is 0 or odd, -EIO on a bus error. */
int imu_ext_fifo_set_watermark(uint8_t watermark_rows);

/** Called from the burst thread; takes s_imu_lock once for the status read
 *  and the row read. Reads whole pairs only: an odd row stays in the FIFO.
 *  Fills @p info on every return.
 *  @return number of rows read into @p buf (7 bytes each, 0 or more), -EIO
 *  on a bus error. */
int imu_ext_fifo_read(uint8_t *buf, uint16_t max_rows, struct imu_fifo_info *info);

/** Called from the system work queue (event work item); takes s_imu_lock
 *  for one SPI transaction. Reads and clears the event status register.
 *  @return 0, -EIO on a bus error. */
int imu_ext_read_event_status(uint8_t *status);

#endif /* IMU_EXT_H_ */
