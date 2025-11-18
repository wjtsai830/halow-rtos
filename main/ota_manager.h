/**
 * @file ota_manager.h
 * @brief OTA (Over-The-Air) update manager for Halow RTOS
 * 
 * Provides functionality for:
 * - Firmware update via HTTP/MQTT
 * - A/B partition switching
 * - Rollback protection
 * - Update verification
 */

#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include "esp_err.h"
#include "esp_ota_ops.h"
#include <stdbool.h>

// OTA Configuration
#define OTA_RECV_TIMEOUT_MS     10000
#define OTA_MAX_RETRY           3
#define OTA_BUFFER_SIZE         4096

// OTA Status
typedef enum {
    OTA_STATUS_IDLE,
    OTA_STATUS_DOWNLOADING,
    OTA_STATUS_VERIFYING,
    OTA_STATUS_INSTALLING,
    OTA_STATUS_COMPLETE,
    OTA_STATUS_FAILED,
    OTA_STATUS_ROLLBACK
} ota_status_t;

// OTA Update Info
typedef struct {
    char version[32];
    char url[256];
    size_t file_size;
    char sha256[65];  // SHA256 hash for verification
} ota_update_info_t;

/**
 * @brief Initialize OTA manager
 * @return ESP_OK on success
 */
esp_err_t ota_manager_init(void);

/**
 * @brief Get current running partition info
 * @param partition_label Buffer to store partition label
 * @param label_size Size of label buffer
 * @param app_desc Pointer to store app description
 * @return ESP_OK on success
 */
esp_err_t ota_get_current_partition_info(char* partition_label, size_t label_size, 
                                         esp_app_desc_t* app_desc);

/**
 * @brief Check if system can perform OTA update
 * @return true if OTA partitions are available and system is stable
 */
bool ota_can_update(void);

/**
 * @brief Start OTA update from URL
 * @param update_info OTA update information
 * @return ESP_OK if update started successfully
 */
esp_err_t ota_start_update(const ota_update_info_t* update_info);

/**
 * @brief Get current OTA status
 * @return Current OTA status
 */
ota_status_t ota_get_status(void);

/**
 * @brief Get OTA progress percentage (0-100)
 * @return Progress percentage
 */
int ota_get_progress(void);

/**
 * @brief Mark current firmware as valid (prevent rollback)
 * Should be called after successful boot and system check
 * @return ESP_OK on success
 */
esp_err_t ota_mark_valid(void);

/**
 * @brief Check if this boot is first boot after OTA update
 * @return true if this is first boot after update
 */
bool ota_is_first_boot_after_update(void);

/**
 * @brief Perform system rollback to previous firmware
 * @return ESP_OK if rollback initiated (system will restart)
 */
esp_err_t ota_rollback(void);

/**
 * @brief Get available space for OTA update
 * @return Available space in bytes
 */
size_t ota_get_available_space(void);

#endif // OTA_MANAGER_H