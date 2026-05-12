cmake_minimum_required(VERSION 3.20)
project(stm32f4_jumpgame C ASM)

set(CMSIS_DIR       ${CMAKE_SOURCE_DIR}/vendor/STM32CubeF4/Drivers/CMSIS)
set(HAL_DIR         ${CMAKE_SOURCE_DIR}/vendor/STM32CubeF4/Drivers/STM32F4xx_HAL_Driver)
set(BSP_DIR         ${CMAKE_SOURCE_DIR}/vendor/STM32CubeF4/Drivers/BSP/STM32F429I-Discovery)
set(ILI9341_DIR     ${CMAKE_SOURCE_DIR}/vendor/STM32CubeF4/Drivers/BSP/Components/ili9341)
set(BSP_COMMON_DIR  ${CMAKE_SOURCE_DIR}/vendor/STM32CubeF4/Drivers/BSP/Components/Common)
set(FONTS_DIR       ${CMAKE_SOURCE_DIR}/vendor/STM32CubeF4/Utilities/Fonts)

# All HAL .c files; dead-strip drops the unused ones via --gc-sections
file(GLOB HAL_SOURCES ${HAL_DIR}/Src/*.c)
list(FILTER HAL_SOURCES EXCLUDE REGEX ".*template\\.c$")

set(BSP_SOURCES
    ${BSP_DIR}/stm32f429i_discovery.c
    ${BSP_DIR}/stm32f429i_discovery_lcd.c
    ${BSP_DIR}/stm32f429i_discovery_sdram.c
    ${ILI9341_DIR}/ili9341.c
)

set(STARTUP_FILE ${CMSIS_DIR}/Device/ST/STM32F4xx/Source/Templates/gcc/startup_stm32f429xx.s)
set(SYSTEM_FILE  ${CMSIS_DIR}/Device/ST/STM32F4xx/Source/Templates/system_stm32f4xx.c)

set(APP_SOURCES
    ${CMAKE_SOURCE_DIR}/app/main.c
    ${CMAKE_SOURCE_DIR}/app/stm32f4xx_it.c
)

set(LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/linker/stm32f429zi.ld)

add_executable(blink.elf
    ${APP_SOURCES}
    ${BSP_SOURCES}
    ${HAL_SOURCES}
    ${STARTUP_FILE}
    ${SYSTEM_FILE}
)

target_include_directories(blink.elf PRIVATE
    ${CMAKE_SOURCE_DIR}/app
    ${CMSIS_DIR}/Include
    ${CMSIS_DIR}/Device/ST/STM32F4xx/Include
    ${HAL_DIR}/Inc
    ${BSP_DIR}
    ${BSP_COMMON_DIR}
    ${ILI9341_DIR}
    ${FONTS_DIR}
)

target_compile_definitions(blink.elf PRIVATE
    STM32F429xx
    USE_HAL_DRIVER
    USE_STM32F429I_DISCO
    HSE_VALUE=8000000
)

# Silence noisy warnings in vendor code (out of our control)
target_compile_options(blink.elf PRIVATE
    -Wno-unused-parameter
    -Wno-unused-but-set-variable
)

target_link_options(blink.elf PRIVATE
    -T${LINKER_SCRIPT}
    -Wl,-Map=${CMAKE_BINARY_DIR}/blink.map
)
set_target_properties(blink.elf PROPERTIES LINK_DEPENDS ${LINKER_SCRIPT})

add_custom_command(TARGET blink.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:blink.elf> ${CMAKE_BINARY_DIR}/blink.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:blink.elf> ${CMAKE_BINARY_DIR}/blink.hex
    COMMAND ${CMAKE_SIZE}              $<TARGET_FILE:blink.elf>
)
