cmake_minimum_required(VERSION 3.14)
include(pico-sdk/pico_sdk_init.cmake)
project(rp2_i2c_eeprom_msc)
pico_sdk_init()
add_executable(rp2_i2c_eeprom_msc)

target_compile_options(rp2_i2c_eeprom_msc PRIVATE -Wall -Wextra)

target_sources(rp2_i2c_eeprom_msc PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/rp2-i2c-eeprom-msc.c
    ${CMAKE_CURRENT_LIST_DIR}/usb_descriptors.c
)

# Show size after link
SET(SHOW_SIZE_INFO "-Wl,--print-memory-usage")
SET(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} ${SHOW_SIZE_INFO}")

target_include_directories(rp2_i2c_eeprom_msc PUBLIC
	${CMAKE_CURRENT_LIST_DIR} # for TinyUSB to find tusb_config.h
)

target_link_libraries(rp2_i2c_eeprom_msc
    pico_stdlib
    pico_unique_id
    hardware_i2c
    tinyusb_device
    tinyusb_board
)

pico_enable_stdio_usb(rp2_i2c_eeprom_msc 0)
pico_enable_stdio_uart(rp2_i2c_eeprom_msc 0)

pico_add_extra_outputs(rp2_i2c_eeprom_msc)
