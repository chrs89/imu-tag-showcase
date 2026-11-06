/*
 * imu_ext.h - the only owner of the LSM6DSV16X registers.
 * Context: boot (main), the burst thread and the system work queue, all
 * serialised by s_imu_lock. Units: ODR in Hz, accel range in g.
 * See docs/adr/ADR-004.
 */
#ifndef IMU_EXT_H_
#define IMU_EXT_H_
#include <stdint.h>
#include <stdbool.h>

/** Called from main at boot; takes s_imu_lock. Probes the chip over SPI.
 *  @return 0, -ENODEV if the devicetree device is not ready, -EIO on a bus error. */
int imu_ext_init(void);

/** Called from the system work queue; takes s_imu_lock. Puts the FIFO in
 *  bypass, sets ODR and both batch rates, returns the FIFO to stream.
 *  @return 0, -EINVAL for an unsupported rate, -EBUSY while event triggers
 *  are armed (ADR-003), -EIO on a bus error. */
int imu_ext_set_odr(uint16_t odr_hz);

/** Called from the system work queue; takes s_imu_lock.
 *  @return 0, -EINVAL for a range other than 2, 4, 8 or 16 g, -EIO on a bus error. */
int imu_ext_set_accel_range(uint8_t range_g);

/** Called from the burst thread; takes s_imu_lock for one SPI transaction.
 *  Reads up to @p max_rows FIFO rows (7 bytes each) into @p buf.
 *  @return number of rows read (0 or more), -EIO on a bus error. */
int imu_ext_fifo_read(uint8_t *buf, uint16_t max_rows);

/** Called from the system work queue (event work item); takes s_imu_lock
 *  for one SPI transaction. Reads and clears the event status register.
 *  @return 0, -EIO on a bus error. */
int imu_ext_read_event_status(uint8_t *status);

#endif /* IMU_EXT_H_ */
