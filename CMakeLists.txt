# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.1)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2 CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(display_demo C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(display_demo display_demo.cpp gc9107.cpp bmp.cpp ws2812.cpp qmi8658.cpp cube.cpp starfield.cpp plasma.cpp)

# Reuse the SDK's WS2812 PIO program (from pico_status_led)
pico_generate_pio_header(display_demo ${PICO_SDK_PATH}/src/rp2_common/pico_status_led/ws2812.pio)

# Embeds FILE into TARGET's flash image as `extern "C" const uint8_t SYMBOL[], SYMBOL_end[]`
function(embed_binary TARGET SYMBOL FILE)
    get_filename_component(FILE ${FILE} ABSOLUTE)
    set(ASM ${CMAKE_CURRENT_BINARY_DIR}/embed_${SYMBOL}.S)
    file(GENERATE OUTPUT ${ASM} CONTENT
"    .section .rodata.${SYMBOL}, \"a\"
    .global ${SYMBOL}, ${SYMBOL}_end
    .balign 4
${SYMBOL}:
    .incbin \"${FILE}\"
${SYMBOL}_end:
")
    target_sources(${TARGET} PRIVATE ${ASM})
    set_source_files_properties(${ASM} PROPERTIES OBJECT_DEPENDS ${FILE}) # rebuild when FILE changes
endfunction()

embed_binary(display_demo flash_img_bmp flash_img_rgb565.bmp)

pico_set_program_name(display_demo "display_demo")
pico_set_program_version(display_demo "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(display_demo 0)
pico_enable_stdio_usb(display_demo 1)

# Add the standard library to the build
target_link_libraries(display_demo
        pico_stdlib
        hardware_spi
        hardware_dma
        hardware_pio
        hardware_i2c)

# Add the standard include files to the build
target_include_directories(display_demo PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

pico_add_extra_outputs(display_demo)

