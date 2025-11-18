/**
 * @file config_manager.h
 * @brief System configuration management for Halow RTOS
 * 
 * Manages system settings stored in the 'config' NVS partition:
 * - WiFi configuration (SSID, credentials)
 * - MQTT settings (broker, topics)
 * - System parameters (timezone, logging level)
 * - HaLow specific settings
 */

#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include "esp_err.h"
#include <stdbool.h>

// Configuration keys for config partition (512KB total)
#define CONFIG_NAMESPACE_GPIO       "gpio_cfg"      // GPIO pin configurations
#define CONFIG_NAMESPACE_HALOW      "halow_cfg"     // HaLow WiFi credentials
#define CONFIG_NAMESPACE_MQTT       "mqtt_cfg"      // MQTT broker settings
#define CONFIG_NAMESPACE_SYSTEM     "system_cfg"    // System parameters

// GPIO Configuration
typedef struct {
    uint8_t led_pin;
    uint8_t reset_pin;
    uint8_t status_pins[8];  // Up to 8 status indicator pins
    bool gpio_invert_flags;
} gpio_config_t;

// HaLow WiFi Configuration (802.11ah)
typedef struct {
    char ssid[32];           // HaLow network SSID
    char password[64];       // HaLow network password
    bool auto_connect;
    int max_retry;
    uint8_t channel;         // HaLow specific channel
    bool low_power_mode;     // HaLow power saving
} halow_wifi_config_t;

// MQTT Configuration
typedef struct {
    char broker_uri[128];
    char client_id[32];
    char username[32];
    char password[32];
    int port;
    int keepalive;
} mqtt_config_t;

// System Configuration
typedef struct {
    int log_level;
    char timezone[32];
    bool watchdog_enable;
    int watchdog_timeout_ms;
} system_config_t;

/**
 * @brief Initialize configuration manager
 * @return ESP_OK on success
 */
esp_err_t config_manager_init(void);

/**
 * @brief Load GPIO configuration from config partition
 * @param gpio_cfg Pointer to GPIO configuration structure
 * @return ESP_OK on success, ESP_ERR_NVS_NOT_FOUND if not configured
 */
esp_err_t config_load_gpio(gpio_config_t* gpio_cfg);

/**
 * @brief Save GPIO configuration to config partition
 * @param gpio_cfg Pointer to GPIO configuration structure
 * @return ESP_OK on success
 */
esp_err_t config_save_gpio(const gpio_config_t* gpio_cfg);

/**
 * @brief Load HaLow WiFi configuration from config partition
 * @param halow_cfg Pointer to HaLow wifi configuration structure
 * @return ESP_OK on success, ESP_ERR_NVS_NOT_FOUND if not configured
 */
esp_err_t config_load_halow_wifi(halow_wifi_config_t* halow_cfg);

/**
 * @brief Save HaLow WiFi configuration to config partition
 * @param halow_cfg Pointer to HaLow wifi configuration structure
 * @return ESP_OK on success
 */
esp_err_t config_save_halow_wifi(const halow_wifi_config_t* halow_cfg);

/**
 * @brief Load MQTT configuration from config partition
 * @param mqtt_cfg Pointer to MQTT configuration structure
 * @return ESP_OK on success, ESP_ERR_NVS_NOT_FOUND if not configured
 */
esp_err_t config_load_mqtt(mqtt_config_t* mqtt_cfg);

/**
 * @brief Save MQTT configuration to config partition
 * @param mqtt_cfg Pointer to MQTT configuration structure
 * @return ESP_OK on success
 */
esp_err_t config_save_mqtt(const mqtt_config_t* mqtt_cfg);

/**
 * @brief Check if config partition is available and functioning
 * @return true if config partition is ready for use
 */
bool config_partition_available(void);

/**
 * @brief Reset all configuration to defaults
 * @return ESP_OK on success
 */
esp_err_t config_reset_all(void);

#endif // CONFIG_MANAGER_H