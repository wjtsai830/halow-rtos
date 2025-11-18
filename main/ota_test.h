/**
 * @file ota_test.h
 * @brief OTA A/B partition switching test utilities
 * 
 * Provides functions to test OTA partition switching functionality
 * without requiring actual firmware updates.
 */

#ifndef OTA_TEST_H
#define OTA_TEST_H

#include "esp_err.h"
#include "esp_ota_ops.h"
#include <stdbool.h>

/**
 * @brief Test OTA partition switching functionality
 * This simulates an OTA update by switching to the other partition
 * @return ESP_OK on success
 */
esp_err_t ota_test_switch_partition(void);

/**
 * @brief Display current OTA partition information
 */
void ota_test_show_partition_info(void);

/**
 * @brief Mark current partition as valid (prevent rollback)
 * @return ESP_OK on success
 */
esp_err_t ota_test_mark_partition_valid(void);

/**
 * @brief Copy current firmware to the other partition for testing
 * @return ESP_OK on success
 */
esp_err_t ota_test_copy_firmware_to_other_partition(void);

/**
 * @brief Test complete A/B switching cycle
 * @return ESP_OK on success
 */
esp_err_t ota_test_full_ab_cycle(void);

#endif // OTA_TEST_H