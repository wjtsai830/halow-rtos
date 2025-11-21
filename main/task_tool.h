/**
 * @file task_tool.h
 * @brief Network tools and utilities for ESP32 RTOS
 */

#ifndef TASK_TOOL_H
#define TASK_TOOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <esp_err.h>

/**
 * @brief Initialize network tools
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t task_tool_init(void);

/**
 * @brief Ping a remote host to test network connectivity
 * @param host IP address or hostname to ping
 * @param count Number of ping packets to send (default: 4)
 * @param interval_ms Interval between ping packets in milliseconds (default: 1000)
 * @return 0 on success, error code otherwise
 */
int task_tool_ping(const char* host, int count, int interval_ms);

#ifdef __cplusplus
}
#endif

#endif // TASK_TOOL_H
