cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(imu_tag)

# Hidden float-to-double promotions cost software emulation on the M4F.
zephyr_compile_options(-Wdouble-promotion)

target_include_directories(app PRIVATE src)

target_sources(app PRIVATE
  src/main.c
  src/imu/imu_ext.c
  src/imu/imu_fifo.c
  src/events/event_triggers.c
  src/pipeline/burst_processor.c
)
