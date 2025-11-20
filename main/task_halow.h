/**
 * @file task_halow.h
 * @brief HaLow WiFi control system for Halow RTOS
 *
 * Features:
 * - Initialize HaLow hardware and software stack
 * - Start/stop HaLow networking
 * - Scan for available HaLow networks
 * - Connect to HaLow networks (open or secured)
 * - Display version information
 */

#ifndef TASK_HALOW_H
#define TASK_HALOW_H

#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief Initialize HaLow system
 * Sets up GPIO pins, event groups, semaphores, and initializes Morse Micro SDK
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t task_halow_init(void);

/**
 * @brief Register HaLow console commands
 * Registers commands for halow control in the console system
 */
void register_halow_commands(void);

/**
 * @brief Start HaLow networking
 * Enables HaLow interface and registers callbacks
 * @return 0 on success, error code otherwise
 */
int halow_start(void);

/**
 * @brief Stop HaLow networking
 * Disables HaLow interface and cleans up events
 * @return 0 on success, error code otherwise
 */
int halow_stop(void);

/**
 * @brief Scan for available HaLow networks
 * Performs a network scan and displays results
 * @return 0 on success, error code otherwise
 */
int halow_scan(void);

/**
 * @brief Connect to a HaLow network
 * @param ssid Network SSID to connect to
 * @param password Password (optional for open networks)
 * @return 0 on success, error code otherwise
 */
int halow_connect(const char* ssid, const char* password);

/**
 * @brief Display HaLow version information
 * Shows firmware, hardware, and chip information
 * @return 0 on success, error code otherwise
 */
int halow_version(void);

/**
 * @brief Check if HaLow is currently initialized
 * @return true if initialized, false otherwise
 */
bool halow_is_initialized(void);

/**
 * @brief Check if HaLow is currently started
 * @return true if started, false otherwise
 */
bool halow_is_started(void);

#endif // TASK_HALOW_H
