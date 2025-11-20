# Check if mm-iot-esp32 components are available
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../mm-iot-esp32/framework/morselib" AND 
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../mm-iot-esp32/framework/mm_shims")
   
    message(STATUS "Found mm-iot-esp32 framework, enabling HaLow support")
    
    # Copy regulatory database files
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../mm-iot-esp32/examples/scan/main/src/mm_app_regdb.c")
        configure_file("${CMAKE_CURRENT_SOURCE_DIR}/../mm-iot-esp32/examples/scan/main/src/mm_app_regdb.c" 
                       "${CMAKE_CURRENT_SOURCE_DIR}/mm_app_regdb.c" COPYONLY)
        configure_file("${CMAKE_CURRENT_SOURCE_DIR}/../mm-iot-esp32/examples/scan/main/src/mm_app_regdb.h" 
                       "${CMAKE_CURRENT_SOURCE_DIR}/mm_app_regdb.h" COPYONLY)
        
        set(HALOW_SRCS "mm_app_regdb.c")
    else()
        message(WARNING "Regulatory database not found, HaLow may not work properly")
        set(HALOW_SRCS)
    endif()
    
    # Register component with all sources
    idf_component_register(SRCS ${HALOW_SRCS} "task_gpio.c" "task_main.c" "task_login.c" "ota_test.c" "task_halow.c" "mm_app_regdb.c"
                           PRIV_REQUIRES console nvs_flash app_update driver morselib mm_shims esp_netif
                           INCLUDE_DIRS ".")
    
    # Define country code
    target_compile_definitions(${COMPONENT_LIB} PRIVATE COUNTRY_CODE="US")
    
else()
    # Fallback build without Morse Micro support
    message(WARNING "mm-iot-esp32 framework not found")
    message(WARNING "Expected: ../mm-iot-esp32/framework/morselib and ../mm-iot-esp32/framework/mm_shims")
    message(WARNING "Building with basic functionality only (no HaLow support)")
    
    idf_component_register(SRCS "task_gpio.c" "task_main.c" "task_login.c" "ota_test.c"
                           PRIV_REQUIRES console nvs_flash app_update driver
                           INCLUDE_DIRS ".")
    
    # Define that HaLow is disabled
    target_compile_definitions(${COMPONENT_LIB} PRIVATE HALOW_DISABLED=1)
endif()
