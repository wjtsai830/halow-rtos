# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Set custom partition table for 16MB Flash (25Q128)
set(PARTITION_TABLE_CSV_PATH ${CMAKE_CURRENT_SOURCE_DIR}/partitions.csv)

# Add mm-iot-esp32 components to search path if available
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/mm-iot-esp32/framework")
    set(EXTRA_COMPONENT_DIRS 
        "${CMAKE_CURRENT_SOURCE_DIR}/mm-iot-esp32/framework/morselib"
        "${CMAKE_CURRENT_SOURCE_DIR}/mm-iot-esp32/framework/mm_shims"
        "${CMAKE_CURRENT_SOURCE_DIR}/mm-iot-esp32/framework/src/mmipal"
        "${CMAKE_CURRENT_SOURCE_DIR}/mm-iot-esp32/framework/src/mmutils"
    )
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)
project(halow-rtos)
