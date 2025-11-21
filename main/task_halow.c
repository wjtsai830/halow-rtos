/**
 * @file task_halow.c
 * @brief HaLow WiFi control system implementation for Halow RTOS
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "task_halow.h"
#include "esp_log.h"
#include "esp_console.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"

// Morse Micro SDK includes
#include "mmhal.h"
#include "mmosal.h"
#include "mmwlan.h"
#include "mmipal.h"
#include "mm_app_regdb.h"

static const char *TAG = "task_halow";

// ANSI Color Codes
#define COLOR_RESET     "\033[0m"
#define COLOR_BOLD      "\033[1m"
#define COLOR_RED       "\033[31m"
#define COLOR_GREEN     "\033[32m"
#define COLOR_YELLOW    "\033[33m"
#define COLOR_BLUE      "\033[34m"
#define COLOR_CYAN      "\033[36m"
#define COLOR_WHITE     "\033[37m"

// GPIO pin definitions (uses Kconfig.projbuild CONFIG_MM_* values)
#define HALOW_SPI_CS_PIN    CONFIG_MM_SPI_CS
#define HALOW_SPI_MOSI_PIN  CONFIG_MM_SPI_MOSI
#define HALOW_SPI_CLK_PIN   CONFIG_MM_SPI_SCK
#define HALOW_SPI_MISO_PIN  CONFIG_MM_SPI_MISO
#define HALOW_SPI_IRQ_PIN   CONFIG_MM_SPI_IRQ
#define HALOW_BUSY_PIN      CONFIG_MM_BUSY
#define HALOW_RESET_PIN     CONFIG_MM_RESET_N
#define HALOW_WAKE_PIN      CONFIG_MM_WAKE

// Default country code - you may need to change this
#ifndef HALOW_COUNTRY_CODE
#define HALOW_COUNTRY_CODE "US"
#endif

#define HALOW_CONNECTED_BIT BIT0
#define HALOW_FAIL_BIT      BIT1
#define HALOW_SCAN_DONE_BIT BIT2
#define MAX_SSID_LEN        32
#define MAX_PASSWORD_LEN    64
#define MAX_SCAN_RESULTS    20

// Network configuration for auto-connect
typedef struct {
    char ssid[MAX_SSID_LEN];
    char password[MAX_PASSWORD_LEN];
    bool valid;  // Flag indicating if config is valid
} network_config_t;

#define AUTO_CONNECT_MAX_ATTEMPTS 3
#define AUTO_CONNECT_RETRY_DELAY_MS 2000

static char halow_last_password[MAX_PASSWORD_LEN];

static EventGroupHandle_t halow_event_group;
static bool halow_initialized = false;
static bool halow_started = false;
static bool halow_booted = false;
static struct mmosal_semb *halow_scan_semaphore = NULL;
static struct mmosal_semb *halow_link_semaphore = NULL;
static uint16_t scan_count = 0;
static char halow_connected_ssid[MMWLAN_SSID_MAXLEN + 1] = "";
static char halow_save_pending_ssid[MMWLAN_SSID_MAXLEN + 1] = "";      // SSID to save when connection succeeds
static char halow_save_pending_password[MAX_PASSWORD_LEN] = "";         // Password to save when connection succeeds

// Function forward declarations
static int halow_auto_connect(void);
static bool halow_should_save_network_config(const char* ssid, const char* password);

/**
 * Link state callback for HaLow connection status
 */
static void halow_link_state_handler(enum mmwlan_link_state link_state, void *arg)
{
    printf("HaLow Link went %s\n> ", (link_state == MMWLAN_LINK_DOWN) ? "Down" : "Up");
    fflush(stdout);

    if (link_state == MMWLAN_LINK_UP)
    {
        if (halow_event_group) {
            xEventGroupSetBits(halow_event_group, HALOW_CONNECTED_BIT);
        }
        if (halow_link_semaphore) {
            mmosal_semb_give(halow_link_semaphore);
        }
    }
    else
    {
        if (halow_event_group) {
            xEventGroupClearBits(halow_event_group, HALOW_CONNECTED_BIT);
        }
    }
}

/**
 * Receive callback for HaLow packets
 */
static void halow_rx_handler(uint8_t *header, unsigned header_len,
                            uint8_t *payload, unsigned payload_len,
                            void *arg)
{
    // Basic packet handling - just log for now
    ESP_LOGI(TAG, "HaLow packet received: header_len=%u, payload_len=%u", header_len, payload_len);
}

/**
 * STA status callback for HaLow connection state
 */
static void halow_sta_status_handler(enum mmwlan_sta_state sta_state)
{
    const char *sta_state_desc[] = {
        "DISABLED",
        "CONNECTING",
        "CONNECTED",
    };
    printf("HaLow STA state: %s (%u)\n> ", sta_state_desc[sta_state], sta_state);
    fflush(stdout);

    // Set connection status based on STA state
    if (sta_state == MMWLAN_STA_CONNECTED) {
        if (halow_event_group) {
            xEventGroupSetBits(halow_event_group, HALOW_CONNECTED_BIT);
        }

        // Save the network configuration when connection succeeds (use dedicated variables)
        if (halow_save_pending_ssid[0] != '\0') {
            ESP_LOGI(TAG, "=== HaLow Connection Success! ===");
            ESP_LOGI(TAG, "Connected to network: '%s'", halow_save_pending_ssid);
            ESP_LOGI(TAG, "Checking if config should be saved...");

            // Check if we need to save (compare with existing config)
            if (halow_should_save_network_config(halow_save_pending_ssid, halow_save_pending_password)) {
                ESP_LOGI(TAG, "Network config needs to be saved (new/different config)");
                esp_err_t save_err = halow_save_network_config(halow_save_pending_ssid, halow_save_pending_password);
                if (save_err == ESP_OK) {
                    ESP_LOGI(TAG, "Network config successfully saved: SSID='%s'", halow_save_pending_ssid);
                    ESP_LOGI(TAG, "Credentials saved to flash (certs partition)");
                    ESP_LOGI(TAG, "Auto-connect will be available on reboot");
                } else {
                    ESP_LOGE(TAG, "Failed to save network config: %s", esp_err_to_name(save_err));
                }
            } else {
                ESP_LOGI(TAG, "Network config already exists, skipping flash write to preserve life");
                ESP_LOGI(TAG, "Configuration is identical - no changes needed");
            }

            // Update connected SSID for status display after successful save
            strncpy(halow_connected_ssid, halow_save_pending_ssid, sizeof(halow_connected_ssid) - 1);
            halow_connected_ssid[sizeof(halow_connected_ssid) - 1] = '\0';

            // Clear the pending save config after processing
            halow_save_pending_ssid[0] = '\0';
            halow_save_pending_password[0] = '\0';

            ESP_LOGI(TAG, "=== HaLow Setup Complete! ===");
        } else {
            ESP_LOGI(TAG, "Connected but no pending config to save");
            // Still update the connected SSID for status display
            // This might happen with auto-connect
            ESP_LOGI(TAG, "Checking if this was an auto-connect...");
        }
    } else if (sta_state == MMWLAN_STA_DISABLED || sta_state == MMWLAN_STA_CONNECTING) {
        // Clear connection status for disconnected/connecting states
        if (halow_event_group) {
            xEventGroupClearBits(halow_event_group, HALOW_CONNECTED_BIT);
        }
        // Clear SSID if connection failed
        halow_connected_ssid[0] = '\0';

        // If connection failed, clear the saved password
        halow_last_password[0] = '\0';
    }
}

/**
 * Scan result callback
 */
static void halow_scan_rx_callback(const struct mmwlan_scan_result *result, void *arg)
{
    char bssid_str[18];
    char ssid_str[MMWLAN_SSID_MAXLEN + 1];

    scan_count++;
    snprintf(bssid_str, sizeof(bssid_str), "%02x:%02x:%02x:%02x:%02x:%02x",
             result->bssid[0], result->bssid[1], result->bssid[2],
             result->bssid[3], result->bssid[4], result->bssid[5]);

    // Ensure null termination
    memcpy(ssid_str, result->ssid, result->ssid_len);
    ssid_str[result->ssid_len] = '\0';

    printf("%2d. %-32s %s %4d %4d\n",
           scan_count, ssid_str, bssid_str, result->rssi, result->op_bw_mhz);
}

/**
 * Scan complete callback
 */
static void halow_scan_complete_callback(enum mmwlan_scan_state state, void *arg)
{
    printf("HaLow scan completed. Found %d networks.\n> ", scan_count);
    fflush(stdout);

    if (halow_event_group) {
        xEventGroupSetBits(halow_event_group, HALOW_SCAN_DONE_BIT);
    }
    if (halow_scan_semaphore) {
        mmosal_semb_give(halow_scan_semaphore);
    }
}

/**
 * @brief Check if GPIO pin is valid for ESP32 HaLow use
 * @param pin GPIO pin number to check
 * @return true if valid, false otherwise
 */
static bool halow_is_valid_pin(uint8_t pin)
{
    // ESP32 GPIO validation for HaLow module
    // Valid range: 0-39, but exclude reserved and input-only pins

    if (pin > 39) {
        ESP_LOGE(TAG, "GPIO pin %d is invalid (>39)", pin);
        return false;
    }

    // Input-only pins (can't be used for SPI/outputs)
    if (pin >= 34 && pin <= 39) {
        ESP_LOGE(TAG, "GPIO pin %d is input-only, can't use for HaLow SPI control", pin);
        return false;
    }

    // Reserved/common problem pins for ESP32
    if (pin >= 6 && pin <= 11) {
        ESP_LOGW(TAG, "GPIO pin %d is connected to flash, may not work reliably", pin);
        // Not returning false, just warning
    }

    // Strapping pins - should avoid
    if (pin == 0 || pin == 2 || pin == 5 || pin == 12 || pin == 15) {
        ESP_LOGW(TAG, "GPIO pin %d is used for strapping, consider using different pin", pin);
        // Not returning false, just warning
    }

    return true;
}

/**
 * @brief Validate HaLow GPIO pin configuration
 * @return true if all pins are valid, false otherwise
 */
static bool halow_validate_pin_config(void)
{
    bool valid = true;
    uint8_t pins[] = {
        HALOW_SPI_CS_PIN, HALOW_SPI_MOSI_PIN, HALOW_SPI_CLK_PIN, HALOW_SPI_MISO_PIN,
        HALOW_SPI_IRQ_PIN, HALOW_BUSY_PIN, HALOW_RESET_PIN, HALOW_WAKE_PIN
    };

    ESP_LOGI(TAG, "Validating HaLow GPIO pin configuration:");
    ESP_LOGI(TAG, "  CS: %d, MOSI: %d, CLK: %d, MISO: %d",
             HALOW_SPI_CS_PIN, HALOW_SPI_MOSI_PIN, HALOW_SPI_CLK_PIN, HALOW_SPI_MISO_PIN);
    ESP_LOGI(TAG, "  IRQ: %d, BUSY: %d, RESET: %d, WAKE: %d",
             HALOW_SPI_IRQ_PIN, HALOW_BUSY_PIN, HALOW_RESET_PIN, HALOW_WAKE_PIN);

    for (size_t i = 0; i < sizeof(pins)/sizeof(pins[0]); i++) {
        if (!halow_is_valid_pin(pins[i])) {
            valid = false;
        }
    }

    return valid;
}

/**
 * @brief Initialize HaLow system
 * Sets up GPIO pins, event groups, semaphores, and initializes Morse Micro SDK
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t task_halow_init(void)
{
    if (halow_initialized) {
        ESP_LOGW(TAG, "HaLow already initialized");
        return ESP_OK;
    }

    // Validate GPIO pin configuration before proceeding
    if (!halow_validate_pin_config()) {
        ESP_LOGE(TAG, "Invalid GPIO pin configuration for HaLow. Please check your Kconfig.projbuild or sdkconfig settings.");
        return ESP_ERR_INVALID_ARG;
    }

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    // Create event group and semaphores
    halow_event_group = xEventGroupCreate();
    if (!halow_event_group) {
        ESP_LOGE(TAG, "Failed to create HaLow event group");
        return ESP_FAIL;
    }

    halow_scan_semaphore = mmosal_semb_create("halow_scan");
    halow_link_semaphore = mmosal_semb_create("halow_link");

    if (!halow_scan_semaphore || !halow_link_semaphore) {
        ESP_LOGE(TAG, "Failed to create HaLow semaphores");
        return ESP_FAIL;
    }

    // Initialize Morse Micro HAL and WLAN subsystems
    // Make sure GPIO is not initialized by ESP-IDF driver before we init
    ESP_LOGI(TAG, "Calling mmhal_init()...");
    mmhal_init();
    ESP_LOGI(TAG, "mmhal_init() completed");

    ESP_LOGI(TAG, "Calling mmwlan_init()...");
    mmwlan_init();
    ESP_LOGI(TAG, "mmwlan_init() completed");

    // NOTE: Network stack (MMIPAL) will be initialized in halow_start()
    // after channel list is set, to avoid "Channel list not set" error

    halow_initialized = true;
    ESP_LOGI(TAG, "HaLow initialized successfully (network stack deferred to start)");
    return ESP_OK;
}



/**
 * @brief Start HaLow networking
 * @return 0 on success, error code otherwise
 */
int halow_start(void)
{
    if (!halow_initialized) {
        ESP_LOGE(TAG, "HaLow not initialized. Call task_halow_init() first.");
        return -1;
    }

    if (halow_started) {
        ESP_LOGW(TAG, "HaLow already started");
        return 0;
    }

    // Check if we need to do any setup (if not already started)
    if (!halow_started) {
        // Boot the WLAN interface if needed
        if (!halow_booted) {
            enum mmwlan_status status;
            const struct mmwlan_s1g_channel_list* channel_list;

            // Load regulatory domain
            channel_list = mmwlan_lookup_regulatory_domain(get_regulatory_db(), HALOW_COUNTRY_CODE);
            if (channel_list == NULL) {
                ESP_LOGE(TAG, "Could not find regulatory domain for country code %s", HALOW_COUNTRY_CODE);
                return -1;
            }

            status = mmwlan_set_channel_list(channel_list);
            if (status != MMWLAN_SUCCESS) {
                ESP_LOGE(TAG, "Failed to set country code %s", channel_list->country_code);
                return -1;
            }

            // Register callbacks
            status = mmwlan_register_link_state_cb(halow_link_state_handler, halow_link_semaphore);
            if (status != MMWLAN_SUCCESS) {
                ESP_LOGE(TAG, "Failed to register link state callback");
                return -1;
            }

            status = mmwlan_register_rx_cb(halow_rx_handler, NULL);
            if (status != MMWLAN_SUCCESS) {
                ESP_LOGE(TAG, "Failed to register RX callback");
                return -1;
            }

            struct mmwlan_boot_args boot_args = MMWLAN_BOOT_ARGS_INIT;
            status = mmwlan_boot(&boot_args);
            if (status != MMWLAN_SUCCESS) {
                ESP_LOGE(TAG, "Failed to boot HaLow interface");
                return -1;
            }

            // Initialize the network stack (MMIPAL) now that channel list is set
            ESP_LOGI(TAG, "Initializing network stack (MMIPAL) after boot...");
            struct mmipal_init_args mmipal_args = MMIPAL_INIT_ARGS_DEFAULT;
            enum mmipal_status mmipal_status = mmipal_init(&mmipal_args);

            if (mmipal_status != MMIPAL_SUCCESS) {
                ESP_LOGE(TAG, "Failed to initialize network stack: %d", mmipal_status);
                return -1;
            }
            ESP_LOGI(TAG, "Network stack (MMIPAL) initialized successfully");

            halow_booted = true;
        } else {
            // Interface already booted, just re-register callbacks
            enum mmwlan_status status;
            ESP_LOGI(TAG, "Re-registering HaLow callbacks for restarted interface");

            // Re-register callbacks
            status = mmwlan_register_link_state_cb(halow_link_state_handler, halow_link_semaphore);
            if (status != MMWLAN_SUCCESS) {
                ESP_LOGE(TAG, "Failed to register link state callback: %d", status);
                // Continue anyway
            }

            status = mmwlan_register_rx_cb(halow_rx_handler, NULL);
            if (status != MMWLAN_SUCCESS) {
                ESP_LOGE(TAG, "Failed to register RX callback: %d", status);
                // Continue anyway
            }
        }
    }

    halow_started = true;
    printf("HaLow started successfully\n> ");
    fflush(stdout);

    ESP_LOGI(TAG, "HaLow started successfully");

    // Attempt auto-connect if we have saved network config
    halow_auto_connect();

    return 0;
}

/**
 * @brief Stop HaLow networking
 * @return 0 on success, error code otherwise
 */
int halow_stop(void)
{
    if (!halow_started) {
        ESP_LOGW(TAG, "HaLow not started");
        return 0;
    }

    // Disable STA mode if enabled
    mmwlan_sta_disable();

    // Unregister callbacks to clean up state
    mmwlan_register_link_state_cb(NULL, NULL);
    mmwlan_register_rx_cb(NULL, NULL);

    // Clear connection status
    if (halow_event_group) {
        xEventGroupClearBits(halow_event_group, HALOW_CONNECTED_BIT);
    }

    halow_started = false;
    printf("HaLow stopped\n> ");
    fflush(stdout);

    ESP_LOGI(TAG, "HaLow stopped successfully");
    return 0;
}

/**
 * @brief Save network configuration to certs partition
 * @param ssid Network SSID
 * @param password Network password (can be NULL for open networks)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t halow_save_network_config(const char* ssid, const char* password)
{
    if (!ssid || strlen(ssid) == 0) {
        ESP_LOGE(TAG, "Cannot save network config: invalid SSID");
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err;

    // Open certs partition with namespace "halow_auto"
    err = nvs_open_from_partition("certs", "halow_auto", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open certs partition: %s", esp_err_to_name(err));
        return err;
    }

    // Save SSID
    err = nvs_set_str(handle, "ssid", ssid);
    if (err != ESP_OK) {
        nvs_close(handle);
        ESP_LOGE(TAG, "Failed to save SSID: %s", esp_err_to_name(err));
        return err;
    }

    // Save password (can be empty for open networks)
    const char* password_to_save = password ? password : "";
    err = nvs_set_str(handle, "password", password_to_save);
    if (err != ESP_OK) {
        nvs_close(handle);
        ESP_LOGE(TAG, "Failed to save password: %s", esp_err_to_name(err));
        return err;
    }

    // Mark config as valid
    err = nvs_set_u8(handle, "valid", 1);
    if (err != ESP_OK) {
        nvs_close(handle);
        ESP_LOGE(TAG, "Failed to set valid flag: %s", esp_err_to_name(err));
        return err;
    }

    // Commit changes
    err = nvs_commit(handle);
    if (err != ESP_OK) {
        nvs_close(handle);
        ESP_LOGE(TAG, "Failed to commit changes: %s", esp_err_to_name(err));
        return err;
    }

    nvs_close(handle);

    ESP_LOGI(TAG, "Network config saved to certs partition: SSID=%s", ssid);
    return ESP_OK;
}

/**
 * @brief Load network configuration from certs partition
 * @param ssid Buffer to store SSID (must be at least 32 bytes)
 * @param password Buffer to store password (must be at least 64 bytes)
 * @return true if config was loaded successfully, false otherwise
 */
bool halow_load_network_config(char* ssid, char* password)
{
    if (!ssid || !password) {
        ESP_LOGE(TAG, "Invalid buffers provided for loading network config");
        return false;
    }

    nvs_handle_t handle;
    esp_err_t err;

    // Open certs partition with namespace "halow_auto"
    err = nvs_open_from_partition("certs", "halow_auto", NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "No saved network config found (couldn't open partition): %s", esp_err_to_name(err));
        return false;
    }

    // Check if config is marked as valid
    uint8_t valid = 0;
    err = nvs_get_u8(handle, "valid", &valid);
    if (err != ESP_OK || valid != 1) {
        nvs_close(handle);
        ESP_LOGD(TAG, "Network config not valid or missing");
        return false;
    }

    // Load SSID
    size_t ssid_len = MAX_SSID_LEN;
    err = nvs_get_str(handle, "ssid", ssid, &ssid_len);
    if (err != ESP_OK) {
        nvs_close(handle);
        ESP_LOGE(TAG, "Failed to load SSID: %s", esp_err_to_name(err));
        return false;
    }

    // Load password
    size_t password_len = MAX_PASSWORD_LEN;
    err = nvs_get_str(handle, "password", password, &password_len);
    if (err != ESP_OK) {
        nvs_close(handle);
        ESP_LOGE(TAG, "Failed to load password: %s", esp_err_to_name(err));
        return false;
    }

    nvs_close(handle);

    ESP_LOGI(TAG, "Network config loaded from certs partition: SSID=%s", ssid);
    return true;
}

/**
 * @brief Check if we need to save network configuration (compare with existing saved config)
 * @param ssid Network SSID to compare
 * @param password Network password to compare (can be NULL for open networks)
 * @return true if config needs to be saved (different from saved config), false otherwise
 */
bool halow_should_save_network_config(const char* ssid, const char* password)
{
    char saved_ssid[MAX_SSID_LEN] = {0};
    char saved_password[MAX_PASSWORD_LEN] = {0};

    // Try to load existing config
    if (!halow_load_network_config(saved_ssid, saved_password)) {
        // No existing config, so we should save
        return true;
    }

    // Compare SSID
    if (strcmp(saved_ssid, ssid) != 0) {
        // Different SSID, need to save
        return true;
    }

    // Compare password (handle NULL cases)
    const char* new_password = password ? password : "";
    if (strcmp(saved_password, new_password) != 0) {
        // Different password, need to save
        return true;
    }

    // Config is identical, no need to save
    return false;
}

/**
 * @brief Clear saved network configuration
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t halow_clear_network_config(void)
{
    nvs_handle_t handle;
    esp_err_t err;

    // Open certs partition with namespace "halow_auto"
    err = nvs_open_from_partition("certs", "halow_auto", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open certs partition for clearing: %s", esp_err_to_name(err));
        return err;
    }

    // Clear all keys in the namespace
    err = nvs_erase_all(handle);
    if (err != ESP_OK) {
        nvs_close(handle);
        ESP_LOGE(TAG, "Failed to erase network config: %s", esp_err_to_name(err));
        return err;
    }

    // Commit changes
    err = nvs_commit(handle);
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit config erase: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Network config cleared from certs partition");
    return ESP_OK;
}

/**
 * @brief Attempt automatic connection to saved network
 * @return 0 on success, error code otherwise
 */
int halow_auto_connect(void)
{
    char ssid[MAX_SSID_LEN];
    char password[MAX_PASSWORD_LEN];

    // Try to load saved network config
    if (!halow_load_network_config(ssid, password)) {
        ESP_LOGI(TAG, "No saved network config found, skipping auto-connect");
        return -1;
    }

    printf(COLOR_CYAN "Found saved network config, attempting auto-connect to '%s'...\n" COLOR_RESET, ssid);

    // Try to connect up to 3 times
    for (int attempt = 1; attempt <= AUTO_CONNECT_MAX_ATTEMPTS; attempt++) {
        printf("Auto-connect attempt %d/%d...\n> ", attempt, AUTO_CONNECT_MAX_ATTEMPTS);
        fflush(stdout);

        // Restore password from NVS storage
        const char* connect_password = (strlen(password) > 0) ? password : NULL;

        // Attempt connection
        if (halow_connect(ssid, connect_password) == 0) {
            // Wait for connection result (5 seconds)
            EventBits_t bits = xEventGroupWaitBits(halow_event_group,
                                                  HALOW_CONNECTED_BIT,
                                                  pdFALSE,
                                                  pdFALSE,
                                                  pdMS_TO_TICKS(5000));

            if (bits & HALOW_CONNECTED_BIT) {
                printf(COLOR_GREEN "Auto-connect successful: %s\n" COLOR_RESET, ssid);
                return 0;
            } else {
                printf(COLOR_YELLOW "Auto-connect attempt %d failed, still trying...\n" COLOR_RESET, attempt);
            }
        } else {
            printf(COLOR_RED "Auto-connect attempt %d failed to initiate\n" COLOR_RESET, attempt);
        }

        // Wait between attempts (except for the last one)
        if (attempt < AUTO_CONNECT_MAX_ATTEMPTS) {
            vTaskDelay(pdMS_TO_TICKS(AUTO_CONNECT_RETRY_DELAY_MS));
        }
    }

    printf(COLOR_RED "Auto-connect failed after %d attempts. Manual connect required.\n" COLOR_RESET, AUTO_CONNECT_MAX_ATTEMPTS);
    return -1;
}

/**
 * @brief Connect to a HaLow network
 * @param ssid Network SSID to connect to
 * @param password Password (optional for open networks)
 * @return 0 on success, error code otherwise
 */
int halow_connect(const char* ssid, const char* password)
{
    if (!halow_started) {
        ESP_LOGE(TAG, "HaLow not started. Use 'halow on' first.");
        return -1;
    }

    if (!ssid || strlen(ssid) == 0) {
        ESP_LOGE(TAG, "Invalid SSID");
        return -1;
    }

    // Store the password for auto-save on successful connection
    if (password && strlen(password) > 0) {
        strncpy(halow_last_password, password, sizeof(halow_last_password) - 1);
        halow_last_password[sizeof(halow_last_password) - 1] = '\0';
    } else {
        halow_last_password[0] = '\0';  // Empty password for open networks
    }

    enum mmwlan_status status;
    struct mmwlan_sta_args sta_args = MMWLAN_STA_ARGS_INIT;

    // Configure STA arguments
    sta_args.ssid_len = strlen(ssid);
    if (sta_args.ssid_len > MMWLAN_SSID_MAXLEN) {
        ESP_LOGE(TAG, "SSID too long");
        return -1;
    }
    memcpy(sta_args.ssid, ssid, sta_args.ssid_len);

    if (password && strlen(password) > 0) {
        sta_args.passphrase_len = strlen(password);
        if (sta_args.passphrase_len > MMWLAN_PASSPHRASE_MAXLEN) {
            ESP_LOGE(TAG, "Password too long");
            return -1;
        }
        memcpy(sta_args.passphrase, password, sta_args.passphrase_len);
        sta_args.security_type = MMWLAN_SAE;  // Use SAE (WPA3) for secured networks
    } else {
        sta_args.security_type = MMWLAN_OWE;  // Use OWE for open networks
    }

    printf("Connecting to HaLow network: %s\n> ", ssid);
    fflush(stdout);

    // Store the connected SSID for status display
    strncpy(halow_connected_ssid, ssid, sizeof(halow_connected_ssid) - 1);
    halow_connected_ssid[sizeof(halow_connected_ssid) - 1] = '\0';

    // Store SSID and password for potential auto-save when connection succeeds
    strncpy(halow_save_pending_ssid, ssid, sizeof(halow_save_pending_ssid) - 1);
    halow_save_pending_ssid[sizeof(halow_save_pending_ssid) - 1] = '\0';

    if (password && strlen(password) > 0) {
        strncpy(halow_save_pending_password, password, sizeof(halow_save_pending_password) - 1);
        halow_save_pending_password[sizeof(halow_save_pending_password) - 1] = '\0';
    } else {
        halow_save_pending_password[0] = '\0';  // Empty for open networks
    }

    ESP_LOGI(TAG, "Set pending save config - SSID='%s', password='%s'",
             halow_save_pending_ssid, halow_save_pending_password[0] ? "[SET]" : "[OPEN]");

    // Enable STA mode and start connection
    status = mmwlan_sta_enable(&sta_args, halow_sta_status_handler);
    if (status != MMWLAN_SUCCESS) {
        ESP_LOGE(TAG, "Failed to enable STA mode: status %d", status);
        // Clear SSID if connection failed
        halow_connected_ssid[0] = '\0';
        // Clear pending save config
        halow_save_pending_ssid[0] = '\0';
        halow_save_pending_password[0] = '\0';
        return -1;
    }

    ESP_LOGI(TAG, "HaLow connection initiated to: %s", ssid);
    return 0;
}

/**
 * @brief Scan for available HaLow networks
 * @return 0 on success, error code otherwise
 */
int halow_scan(void)
{
    if (!halow_started) {
        ESP_LOGE(TAG, "HaLow not started. Use 'halow on' first.");
        return -1;
    }

    printf("Starting HaLow scan...\n");
    printf("%-3s %-32s %-17s %-4s %-4s\n", "No", "SSID", "BSSID", "RSSI", "BW");
    printf("--- -------------------------------- ----------------- ---- ----\n");
    fflush(stdout);

    // Reset scan count
    scan_count = 0;

    // Configure and start scan
    struct mmwlan_scan_req scan_req = MMWLAN_SCAN_REQ_INIT;
    scan_req.scan_rx_cb = halow_scan_rx_callback;
    scan_req.scan_complete_cb = halow_scan_complete_callback;

    enum mmwlan_status status = mmwlan_scan_request(&scan_req);
    if (status != MMWLAN_SUCCESS) {
        ESP_LOGE(TAG, "Failed to start HaLow scan: status %d", status);
        return -1;
    }

    ESP_LOGI(TAG, "HaLow scan initiated");
    return 0;
}

/**
 * @brief Display HaLow version information
 * @return 0 on success, error code otherwise
 */
int halow_version(void)
{
    if (!halow_booted) {
        ESP_LOGE(TAG, "HaLow not booted. Use 'halow on' first.");
        return -1;
    }

    enum mmwlan_status status;
    struct mmwlan_version version;
    struct mmwlan_bcf_metadata bcf_metadata;

    printf("------- HaLow Version Information -------\n");

    // Get BCF metadata
    status = mmwlan_get_bcf_metadata(&bcf_metadata);
    if (status == MMWLAN_SUCCESS) {
        printf("BCF API version:         %u.%u.%u\n",
               bcf_metadata.version.major, bcf_metadata.version.minor, bcf_metadata.version.patch);
        if (bcf_metadata.build_version[0] != '\0') {
            printf("BCF build version:       %s\n", bcf_metadata.build_version);
        }
        if (bcf_metadata.board_desc[0] != '\0') {
            printf("BCF board description:   %s\n", bcf_metadata.board_desc);
        }
    } else {
        printf("!! BCF metadata retrieval failed !!\n");
    }

    // Get firmware version and chip ID
    status = mmwlan_get_version(&version);
    if (status == MMWLAN_SUCCESS) {
        printf("Morselib version:        %s\n", version.morselib_version);
        printf("Morse firmware version:  %s\n", version.morse_fw_version);
        printf("Morse chip ID:           0x%04lx\n", version.morse_chip_id);
    } else {
        printf("!! Error occurred whilst retrieving version info !!\n");
        return -1;
    }

    // Get MAC address
    uint8_t mac_addr[MMWLAN_MAC_ADDR_LEN];
    status = mmwlan_get_mac_addr(mac_addr);
    if (status == MMWLAN_SUCCESS) {
        printf("MAC address:             %02x:%02x:%02x:%02x:%02x:%02x\n",
               mac_addr[0], mac_addr[1], mac_addr[2],
               mac_addr[3], mac_addr[4], mac_addr[5]);
    } else {
        printf("!! Failed to get MAC address !!\n");
    }

    printf("------------------------------------------\n> ");
    fflush(stdout);

    ESP_LOGI(TAG, "HaLow version information displayed");
    return 0;
}

/**
 * @brief Check if HaLow is currently initialized
 * @return true if initialized, false otherwise
 */
bool halow_is_initialized(void)
{
    return halow_initialized;
}

/**
 * @brief Check if HaLow is currently started
 * @return true if started, false otherwise
 */
bool halow_is_started(void)
{
    return halow_started;
}

/**
 * @brief Console command handler for halow commands
 */
static int halow_cmd(int argc, char **argv)
{
    if (argc < 2) {
        printf(COLOR_CYAN "Usage:\n" COLOR_RESET);
        printf("  halow on              - Start HaLow networking\n");
        printf("  halow off             - Stop HaLow networking\n");
        printf("  halow scan            - Scan for available networks\n");
        printf("  halow connect <ssid> [password] - Connect to network\n");
        printf("  halow version         - Display version information\n");
        printf("  halow status          - Show current status\n");
        printf("  halow refresh         - Refresh network status (polls for IP updates)\n");
        return 0;
    }

    const char *subcmd = argv[1];

    if (strcmp(subcmd, "on") == 0) {
        if (halow_start() == 0) {
            printf(COLOR_GREEN "HaLow started successfully\n" COLOR_RESET);
        } else {
            printf(COLOR_RED "Failed to start HaLow\n" COLOR_RESET);
            return 1;
        }
    }
    else if (strcmp(subcmd, "off") == 0) {
        if (halow_stop() == 0) {
            printf(COLOR_GREEN "HaLow stopped successfully\n" COLOR_RESET);
        } else {
            printf(COLOR_RED "Failed to stop HaLow\n" COLOR_RESET);
            return 1;
        }
    }
    else if (strcmp(subcmd, "scan") == 0) {
        halow_scan();
    }
    else if (strcmp(subcmd, "connect") == 0) {
        if (argc < 3) {
            printf(COLOR_RED "Error: halow connect requires SSID\n" COLOR_RESET);
            return 1;
        }
        const char *ssid = argv[2];
        const char *password = (argc >= 4) ? argv[3] : NULL;

        if (halow_connect(ssid, password) == 0) {
            printf(COLOR_GREEN "Connection initiated to '%s'\n" COLOR_RESET, ssid);
        } else {
            printf(COLOR_RED "Failed to connect to '%s'\n" COLOR_RESET, ssid);
            return 1;
        }
    }
    else if (strcmp(subcmd, "version") == 0) {
        halow_version();
    }
    else if (strcmp(subcmd, "status") == 0) {
        // Check connection status using event group (set by callbacks)
        EventBits_t bits = xEventGroupGetBits(halow_event_group);

        if (bits & HALOW_CONNECTED_BIT) {
            printf("Connected:   " COLOR_GREEN "Yes" COLOR_RESET "\n");

            // Display connected network SSID
            if (halow_connected_ssid[0] != '\0') {
                printf("SSID:        %s\n", halow_connected_ssid);
            } else {
                printf("SSID:        " COLOR_YELLOW "Unknown" COLOR_RESET "\n");
            }

            // Get and display IP address from network stack
            struct mmipal_ip_config ip_config;
            enum mmipal_status ip_status = mmipal_get_ip_config(&ip_config);

            if (ip_status == MMIPAL_SUCCESS) {
                // Check if we have a valid IP (not 0.0.0.0 which indicates DHCP in progress)
                if (strcmp(ip_config.ip_addr, "0.0.0.0") == 0 || strcmp(ip_config.ip_addr, "") == 0) {
                    printf("IP Address:  " COLOR_YELLOW "DHCP in progress... (wait a few seconds)" COLOR_RESET "\n");
                    printf("Netmask:     Waiting for DHCP\n");
                    printf("Gateway:     Waiting for DHCP\n");
                } else {
                    printf("IP Address:  %s\n", ip_config.ip_addr);
                    printf("Netmask:     %s\n", ip_config.netmask);
                    printf("Gateway:     %s\n", ip_config.gateway_addr);
                }
            } else {
                printf("IP Address:  " COLOR_RED "Failed to get IP config (%d)" COLOR_RESET "\n", ip_status);
                printf("Netmask:     N/A\n");
                printf("Gateway:     N/A\n");
            }
        } else {
            printf("Connected:   " COLOR_RED "No" COLOR_RESET "\n");
            printf("SSID:        N/A\n");
            printf("IP Address:  N/A\n");
            printf("Netmask:     N/A\n");
            printf("Gateway:     N/A\n");
        }
    }
    else {
        printf(COLOR_RED "Unknown command: %s\n" COLOR_RESET, subcmd);
        return 1;
    }

    return 0;
}

/**
 * @brief Register HaLow console commands
 */
void register_halow_commands(void)
{
    const esp_console_cmd_t halow_cmd_def = {
        .command = "halow",
        .help = "HaLow WiFi control: 'halow on|off|scan|connect <ssid> [pwd]|version|status'",
        .hint = NULL,
        .func = &halow_cmd,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&halow_cmd_def));
}
