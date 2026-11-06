# Flash / debug runners for imu_tag_v1 (nRF52840QIAA)
# Default runner: jlink (no nrfjprog needed — J-Link OB on nRF52840 DK works directly)
board_runner_args(jlink "--device=nRF52840_xxAA" "--speed=4000" "--reset-after-load")
board_runner_args(nrfjprog "--nrf-family=NRF52" "--softreset")
board_runner_args(nrfutil "--nrf-family=NRF52")

include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
include(${ZEPHYR_BASE}/boards/common/nrfjprog.board.cmake)
include(${ZEPHYR_BASE}/boards/common/nrfutil.board.cmake)
