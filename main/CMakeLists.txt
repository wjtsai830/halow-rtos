idf_component_register(SRCS "task_gpio.c" "task_main.c" "task_login.c" "ota_test.c"
                       PRIV_REQUIRES console nvs_flash app_update driver
                       INCLUDE_DIRS ".")
