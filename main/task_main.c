/* Halow RTOS System */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "esp_system.h"
#include "esp_log.h"
#include "esp_console.h"
#include "esp_vfs_dev.h"
#include "esp_chip_info.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_task_wdt.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "task_login.h"
#include "ota_test.h"
#include "task_gpio.h"
#ifndef HALOW_DISABLED
#include "task_halow.h"
#include "task_tool.h"
#endif

/*
 * We warn if a secondary serial console is enabled. A secondary serial console is always output-only and
 * hence not very useful for interactive console applications. If you encounter this warning, consider disabling
 * the secondary serial console in menuconfig unless you know what you are doing.
 */
#if SOC_USB_SERIAL_JTAG_SUPPORTED
#if !CONFIG_ESP_CONSOLE_SECONDARY_NONE
#warning "A secondary serial console is not useful when using the console component. Please disable it in menuconfig."
#endif
#endif

static const char* TAG = "halow_rtos";
#define PROMPT_STR CONFIG_IDF_TARGET

// Login state variables
static bool is_logged_in = false;
static char current_user[MAX_USERNAME_LEN + 1] = {0};
static char current_prompt[32] = {0};
static login_state_t current_state = LOGIN_STATE_USERNAME;
static esp_task_wdt_user_handle_t login_wdt_handle = NULL;

// ANSI Color Codes
#define COLOR_RESET     "\033[0m"
#define COLOR_BOLD      "\033[1m"
#define COLOR_RED       "\033[31m"
#define COLOR_GREEN     "\033[32m"
#define COLOR_YELLOW    "\033[33m"
#define COLOR_BLUE      "\033[34m"
#define COLOR_MAGENTA   "\033[35m"
#define COLOR_CYAN      "\033[36m"
#define COLOR_WHITE     "\033[37m"
#define COLOR_BG_BLUE   "\033[44m"
#define COLOR_BG_GREEN  "\033[42m"

// Welcome banner for Halow RTOS System
static void print_welcome_banner(void)
{
    printf("\n");
    printf(COLOR_CYAN COLOR_BOLD "╔══════════════════════════════════════════════════════════════════╗\n");
    printf("║" COLOR_BG_BLUE COLOR_WHITE "                       HALOW RTOS SYSTEM                          " COLOR_RESET COLOR_CYAN COLOR_BOLD "║\n");
    printf("║" COLOR_YELLOW "                     Advanced IoT Platform                        " COLOR_CYAN COLOR_BOLD"║\n");
    printf("╠══════════════════════════════════════════════════════════════════╣\n");
    printf("║  " COLOR_GREEN "System Features:"    COLOR_CYAN COLOR_BOLD"                                                ║\n");
    printf("║     " COLOR_WHITE "• HaLow WiFi (802.11ah) Long-Range Connectivity              " COLOR_CYAN COLOR_BOLD"║\n");
    printf("║     " COLOR_WHITE "• MQTT Communication & IoT Integration                       " COLOR_CYAN COLOR_BOLD"║\n");
    printf("║     " COLOR_WHITE "• A/B Partition OTA Updates via MQTT                         " COLOR_CYAN COLOR_BOLD"║\n");
    printf("║     " COLOR_WHITE "• Secure Login & TLS Certificate Management                  " COLOR_CYAN COLOR_BOLD"║\n");
    printf("║     " COLOR_WHITE "• GPIO Configuration & Real-time Control                     " COLOR_CYAN COLOR_BOLD"║\n");
    printf("║                                                                  ║\n");
    printf("║  " COLOR_BLUE "Available Commands:" COLOR_CYAN "                                             ║\n");
    printf("║     " COLOR_YELLOW "• help      " COLOR_WHITE "- Show all available commands                    " COLOR_CYAN COLOR_BOLD"║\n");
    printf("║     " COLOR_YELLOW "• version   " COLOR_WHITE "- Display system & partition information         " COLOR_CYAN COLOR_BOLD"║\n");
    printf("║     " COLOR_YELLOW "• free      " COLOR_WHITE "- Show memory usage statistics                   " COLOR_CYAN COLOR_BOLD"║\n");
    printf("║     " COLOR_YELLOW "• uptime    " COLOR_WHITE "- Display system uptime                          " COLOR_CYAN COLOR_BOLD"║\n");
    printf("║     " COLOR_YELLOW "• reboot    " COLOR_WHITE "- Reboot the system                              " COLOR_CYAN COLOR_BOLD"║\n");
    printf("║                                                                  ║\n");
    printf("║  " COLOR_MAGENTA "  Tip: Type 'help' for complete command list                    " COLOR_CYAN COLOR_BOLD"║\n");
    printf("╚══════════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");
    
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    
    printf(COLOR_GREEN " Hardware Info:" COLOR_RESET "\n");
    printf("   Chip: " COLOR_BOLD COLOR_WHITE "%s" COLOR_RESET " Rev " COLOR_YELLOW "%d" COLOR_RESET 
           " | Cores: " COLOR_CYAN "%d" COLOR_RESET " | Features: " COLOR_CYAN "Halow-Wifi" COLOR_RESET, 
           CONFIG_IDF_TARGET, chip_info.revision, chip_info.cores);
    printf("\n");
    
    // printf(COLOR_BLUE " ESP-IDF Version: " COLOR_YELLOW "%s" COLOR_RESET "\n", esp_get_idf_version());
    // printf(COLOR_GREEN " Free Heap: " COLOR_CYAN "%lu" COLOR_WHITE " bytes" COLOR_RESET "\n", esp_get_free_heap_size());
    printf("\n");
    printf(COLOR_BG_BLUE COLOR_YELLOW " Ready for commands. Type 'help' to get started! " COLOR_RESET "\n");
    printf("\n");
}

// Simple custom commands instead of using external components

static int restart_cmd(int argc, char **argv)
{
    ESP_LOGI(TAG, "Restarting in 3 seconds...");
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
    return 0;
}

static int free_mem_cmd(int argc, char **argv)
{
    printf("Free heap: %lu bytes\n", esp_get_free_heap_size());
    printf("Min free heap: %lu bytes\n", esp_get_minimum_free_heap_size());
    return 0;
}

static int version_cmd(int argc, char **argv)
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    
    printf("\n" COLOR_CYAN COLOR_BOLD "=== HALOW RTOS SYSTEM INFORMATION ===" COLOR_RESET "\n\n");
    
    // System Information
    printf(COLOR_GREEN " System Info:" COLOR_RESET "\n");
    printf("   ESP-IDF Version: %s\n", esp_get_idf_version());
    printf("   Chip: %s Rev %d\n", CONFIG_IDF_TARGET, chip_info.revision);
    printf("   Features: WiFi%s%s + HaLow\n",
           (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");
    printf("   CPU Cores: %d\n", chip_info.cores);
    printf("   Flash: 16MB\n\n");
    
    // Partition Information
    printf(COLOR_BLUE " Partition Layout:" COLOR_RESET "\n");
    
    // Current running partition
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (running) {
        printf("   " COLOR_GREEN "▶ Current: %s (%.1fMB)" COLOR_RESET "\n", 
               running->label, running->size / 1024.0 / 1024.0);
    }
    
    // OTA partitions
    const esp_partition_t* ota_0 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    const esp_partition_t* ota_1 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
    
    if (ota_0) {
        printf("    OTA_0 (A): %.1fMB%s\n", ota_0->size / 1024.0 / 1024.0,
               (running && running == ota_0) ? " (ACTIVE)" : "");
    }
    if (ota_1) {
        printf("    OTA_1 (B): %.1fMB%s\n", ota_1->size / 1024.0 / 1024.0,
               (running && running == ota_1) ? " (ACTIVE)" : "");
    }
    
    // Data partitions
    printf("    Config: 512KB (HaLow/GPIO/MQTT)\n");
    printf("    Certs: 3.4MB (Login/TLS)\n\n");
    
    // Memory status
    printf(COLOR_YELLOW " Memory Status:" COLOR_RESET "\n");
    printf("   Free Heap: %lu bytes (%.1fKB)\n", 
           esp_get_free_heap_size(), esp_get_free_heap_size() / 1024.0);
    printf("   Min Free Heap: %lu bytes (%.1fKB)\n\n",
           esp_get_minimum_free_heap_size(), esp_get_minimum_free_heap_size() / 1024.0);
    
    return 0;
}

static int uptime_cmd(int argc, char **argv)
{
    TickType_t uptime_ticks = xTaskGetTickCount();
    int uptime_sec = uptime_ticks / configTICK_RATE_HZ;
    int hours = uptime_sec / 3600;
    int minutes = (uptime_sec % 3600) / 60;
    int seconds = uptime_sec % 60;
    
    printf("Uptime: %02d:%02d:%02d (%d seconds)\n", hours, minutes, seconds, uptime_sec);
    printf("Tick count: %lu (tick rate: %d Hz)\n", (unsigned long)uptime_ticks, configTICK_RATE_HZ);
    return 0;
}

static void register_basic_commands(void)
{
    const esp_console_cmd_t reboot_cmd_def = {
        .command = "reboot",
        .help = "Reboot the system",
        .hint = NULL,
        .func = &restart_cmd,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&reboot_cmd_def));

    const esp_console_cmd_t free_cmd_def = {
        .command = "free",
        .help = "Show free memory",
        .hint = NULL,
        .func = &free_mem_cmd,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&free_cmd_def));

    const esp_console_cmd_t version_cmd_def = {
        .command = "version",
        .help = "Show system version information",
        .hint = NULL,
        .func = &version_cmd,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&version_cmd_def));

    const esp_console_cmd_t uptime_cmd_def = {
        .command = "uptime",
        .help = "Show system uptime",
        .hint = NULL,
        .func = &uptime_cmd,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&uptime_cmd_def));
}

static int ota_info_cmd(int argc, char **argv)
{
    ota_test_show_partition_info();
    return 0;
}

static int ota_copy_cmd(int argc, char **argv)
{
    esp_err_t err = ota_test_copy_firmware_to_other_partition();
    if (err != ESP_OK) {
        printf("Failed to copy firmware: %s\n", esp_err_to_name(err));
        return 1;
    }
    return 0;
}

static int ota_switch_cmd(int argc, char **argv)
{
    esp_err_t err = ota_test_switch_partition();
    if (err != ESP_OK) {
        printf("Failed to switch partition: %s\n", esp_err_to_name(err));
        return 1;
    }
    return 0;
}

static int ota_valid_cmd(int argc, char **argv)
{
    esp_err_t err = ota_test_mark_partition_valid();
    if (err != ESP_OK) {
        printf("Failed to mark partition as valid: %s\n", esp_err_to_name(err));
        return 1;
    }
    return 0;
}

static int ota_test_cmd(int argc, char **argv)
{
    esp_err_t err = ota_test_full_ab_cycle();
    if (err != ESP_OK) {
        printf("OTA test failed: %s\n", esp_err_to_name(err));
        return 1;
    }
    return 0;
}

static void register_ota_commands(void)
{
    const esp_console_cmd_t ota_info_cmd_def = {
        .command = "ota_info",
        .help = "Show OTA partition information",
        .hint = NULL,
        .func = &ota_info_cmd,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&ota_info_cmd_def));

    const esp_console_cmd_t ota_copy_cmd_def = {
        .command = "ota_copy",
        .help = "Copy current firmware to other partition",
        .hint = NULL,
        .func = &ota_copy_cmd,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&ota_copy_cmd_def));

    const esp_console_cmd_t ota_switch_cmd_def = {
        .command = "ota_switch",
        .help = "Switch to other partition (requires restart)",
        .hint = NULL,
        .func = &ota_switch_cmd,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&ota_switch_cmd_def));

    const esp_console_cmd_t ota_valid_cmd_def = {
        .command = "ota_valid",
        .help = "Mark current partition as valid",
        .hint = NULL,
        .func = &ota_valid_cmd,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&ota_valid_cmd_def));

    const esp_console_cmd_t ota_test_cmd_def = {
        .command = "ota_test",
        .help = "Run full A/B partition switching test",
        .hint = NULL,
        .func = &ota_test_cmd,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&ota_test_cmd_def));
}

/**
 * @brief Initialize a single NVS partition with error handling
 * @param partition_name Name of the partition to initialize
 * @param is_required Whether this partition is required for system operation
 * @return ESP_OK if successful or not required, error code otherwise
 */
static esp_err_t init_nvs_partition(const char* partition_name, bool is_required)
{
    esp_err_t err;
    
    if (strcmp(partition_name, "default") == 0) {
        // Default NVS partition initialization
        err = nvs_flash_init();
        if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_LOGW(TAG, "NVS partition corrupted, erasing...");
            ESP_ERROR_CHECK(nvs_flash_erase());
            err = nvs_flash_init();
        }
    } else {
        // Custom partition initialization
        err = nvs_flash_init_partition(partition_name);
        if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_LOGW(TAG, "NVS partition '%s' corrupted, erasing...", partition_name);
            ESP_ERROR_CHECK(nvs_flash_erase_partition(partition_name));
            err = nvs_flash_init_partition(partition_name);
        }
    }
    
    // Handle initialization results
    if (err == ESP_OK) {
        // ESP_LOGI(TAG, " NVS partition '%s' initialized successfully", partition_name);
    } else if (err == ESP_ERR_NOT_FOUND) {
        if (is_required) {
            ESP_LOGE(TAG, " Required partition '%s' not found in flash", partition_name);
            return err;
        } else {
            ESP_LOGW(TAG, "  Optional partition '%s' not found, skipping", partition_name);
            return ESP_OK; // Not an error for optional partitions
        }
    } else {
        ESP_LOGE(TAG, " Failed to initialize partition '%s': %s", 
                 partition_name, esp_err_to_name(err));
        if (is_required) {
            return err;
        }
    }
    
    return err;
}

static void initialize_nvs(void)
{
    // Initialize default NVS partition (required for basic system operation)
    ESP_ERROR_CHECK(init_nvs_partition("default", true));
    
    // Initialize config partition (required for system configuration)
    // This will store WiFi settings, system parameters, etc.
    esp_err_t config_err = init_nvs_partition("config", false);
    if (config_err != ESP_OK && config_err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "Config partition initialization failed, system may have limited functionality");
    }
    
    // Initialize certs partition (required for login credentials)
    // This stores user authentication data
    esp_err_t certs_err = init_nvs_partition("certs", false);
    if (certs_err != ESP_OK && certs_err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "Certs partition initialization failed, login system may fallback to default NVS");
    }
    
    // ESP_LOGI(TAG, "🎯 NVS partition initialization complete");
    
    // Log partition status for debugging
    // ESP_LOGI(TAG, " Partition Status:");
    // ESP_LOGI(TAG, "   • Default NVS:  Ready");
    // ESP_LOGI(TAG, "   • Config: %s", (config_err == ESP_OK) ? " Ready" : " Not Available");
    // ESP_LOGI(TAG, "   • Certs: %s", (certs_err == ESP_OK) ? " Ready" : " Not Available");
    
    // Note: bootloader partition is managed by ESP-IDF and doesn't need NVS initialization
    // The factory app partition is the currently running firmware
    // OTA updates will use separate OTA partitions when implemented
}

/**
 * @brief Check partition availability for system features
 * This function can be used by other modules to determine available functionality
 */
static void check_partition_availability(void)
{
    // Check if we can use config partition for system settings
    nvs_handle_t config_handle;
    esp_err_t err = nvs_open_from_partition("config", "test", NVS_READWRITE, &config_handle);
    if (err == ESP_OK) {
        nvs_close(config_handle);
        // ESP_LOGI(TAG, " Config partition available - WiFi/MQTT settings supported");
    } else {
        // ESP_LOGW(TAG, " Config partition unavailable - using default settings only");
    }
    
    // Check if we can use certs partition for secure credentials
    nvs_handle_t certs_handle;
    err = nvs_open_from_partition("certs", "test", NVS_READWRITE, &certs_handle);
    if (err == ESP_OK) {
        nvs_close(certs_handle);
        // ESP_LOGI(TAG, " Certs partition available - secure login supported");
    } else {
        // ESP_LOGW(TAG, " Certs partition unavailable - fallback to default NVS");
    }
    
    // Check for OTA partitions availability
    const esp_partition_t* ota_0 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    const esp_partition_t* ota_1 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
    const esp_partition_t* otadata = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
    
    if (ota_0 && ota_1 && otadata) {
        // ESP_LOGI(TAG, " OTA partitions available - HaLow MQTT firmware updates supported");
        // ESP_LOGI(TAG, "   • OTA_0 (A): %zuMB, OTA_1 (B): %zuMB", ota_0->size / 1024 / 1024, ota_1->size / 1024 / 1024);
        
        // Check which partition we're currently running from
        const esp_partition_t* running = esp_ota_get_running_partition();
        if (running) {
            ESP_LOGI(TAG, "   • Currently running from: %s", running->label);
        }
    } else {
        ESP_LOGE(TAG, "❌ OTA partitions missing - firmware updates disabled!");
        if (!ota_0) ESP_LOGE(TAG, "    ota_0 partition not found");
        if (!ota_1) ESP_LOGE(TAG, "    ota_1 partition not found");
        if (!otadata) ESP_LOGE(TAG, "    otadata partition not found");
    }
}

// This function was removed as it's not used in the current implementation

// Function to handle login process before starting console
static void handle_login_process(void)
{
    login_result_t login_result;
    char input_buffer[64];
    
    // Configure watchdog to prevent timeout during login
    esp_task_wdt_config_t twdt_config = {
        .timeout_ms = 30000,  // 30 seconds
        .idle_core_mask = 0,  // Don't monitor idle tasks
        .trigger_panic = false
    };
    esp_task_wdt_init(&twdt_config);
    esp_task_wdt_add_user("main", &login_wdt_handle); // Add main task to watchdog
    
    while (!is_logged_in) {
        // Feed the watchdog
        if (login_wdt_handle) esp_task_wdt_reset_user(login_wdt_handle);
        
        // Read input with timeout handling
        fflush(stdout);
        
        // Use a non-blocking approach with short delays
        size_t input_pos = 0;
        memset(input_buffer, 0, sizeof(input_buffer));
        
        while (input_pos < sizeof(input_buffer) - 1) {
            int c = getchar();
            if (c == EOF) {
                vTaskDelay(pdMS_TO_TICKS(10)); // Small delay
                if (login_wdt_handle) esp_task_wdt_reset_user(login_wdt_handle); // Feed watchdog
                continue;
            }
            
            if (c == '\n' || c == '\r') {
                break; // End of input
            }
            
            if (c == '\b' || c == 127) { // Backspace
                if (input_pos > 0) {
                    input_pos--;
                    printf("\b \b"); // Erase character
                    fflush(stdout);
                }
                continue;
            }
            
            if (isprint(c)) {
                input_buffer[input_pos++] = c;
                // Echo character only for username, hide password
                if (current_state != LOGIN_STATE_PASSWORD) {
                    putchar(c);
                } else {
                    putchar('*'); // Show asterisk for password
                }
                fflush(stdout);
            }
            
            if (login_wdt_handle) esp_task_wdt_reset_user(login_wdt_handle); // Feed watchdog
        }
        
        printf("\n");
        input_buffer[input_pos] = '\0';
        
        if (strlen(input_buffer) == 0) {
            continue; // Empty input, prompt again
        }
        
        // Process login input
        login_state_t state = handle_login_input(input_buffer, &login_result);
        current_state = state; // Update current state for password hiding
        
        if (state == LOGIN_STATE_LOGGED_IN && login_result.success) {
            is_logged_in = true;
            strncpy(current_user, login_result.username, MAX_USERNAME_LEN);
            get_login_prompt(current_user, current_prompt, sizeof(current_prompt));
            
            printf("\n");
            print_welcome_banner();
            printf("\n");
            
            // Clean up watchdog after successful login
            if (login_wdt_handle) {
                esp_task_wdt_delete_user(login_wdt_handle);
                login_wdt_handle = NULL;
            }
            break;
        } else if (state == LOGIN_STATE_USERNAME) {
            printf(COLOR_CYAN "👤 Username (max %d chars): " COLOR_RESET, MAX_USERNAME_LEN);
        } else if (state == LOGIN_STATE_FAILED) {
            printf(COLOR_CYAN "👤 Username (max %d chars): " COLOR_RESET, MAX_USERNAME_LEN);
        }
        // For PASSWORD state, the prompt is handled in task_login.c
        
        fflush(stdout);
    }
}

void app_main(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    
    initialize_nvs();
    check_partition_availability();
    login_init();
    
    // Initialize GPIO system
    task_gpio_init();
    
#ifndef HALOW_DISABLED
    // Initialize HaLow system
    task_halow_init();

    // Initialize network tools (ping, traceroute, etc.)
    task_tool_init();
#endif

#ifdef CONFIG_SYSTEM_LOG_ENABLE
    ESP_LOGI(TAG, "Starting Halow RTOS System");
    ESP_LOGI(TAG, "Max command line length: %d", CONFIG_CONSOLE_MAX_COMMAND_LINE_LENGTH);
#else
    // Disable all system logs for clean output
    esp_log_level_set("*", ESP_LOG_NONE);
    
    // Small delay to let any remaining logs finish
    vTaskDelay(pdMS_TO_TICKS(200));
#endif

    // Handle login process first
    display_login_banner();
    handle_login_process();

#ifndef CONFIG_SYSTEM_LOG_ENABLE
    // Keep logs disabled for clean console experience
    esp_log_level_set("*", ESP_LOG_NONE);
#endif
    
    // Set console prompt based on logged-in user
    repl_config.prompt = current_prompt;
    repl_config.max_cmdline_length = CONFIG_CONSOLE_MAX_COMMAND_LINE_LENGTH;

    /* Register basic commands */
    esp_console_register_help_command();
    register_basic_commands();
    register_ota_commands();
    register_gpio_commands();
#ifndef HALOW_DISABLED
    register_halow_commands();
    register_tool_commands();
#endif

#if defined(CONFIG_ESP_CONSOLE_UART_DEFAULT) || defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
    esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&hw_config, &repl_config, &repl));

#elif defined(CONFIG_ESP_CONSOLE_USB_CDC)
    esp_console_dev_usb_cdc_config_t hw_config = ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_cdc(&hw_config, &repl_config, &repl));

#elif defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
    esp_console_dev_usb_serial_jtag_config_t hw_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&hw_config, &repl_config, &repl));

#else
#error Unsupported console type
#endif

    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
