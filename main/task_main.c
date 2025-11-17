/* Halow RTOS System */

#include <stdio.h>
#include <string.h>
#include "esp_system.h"
#include "esp_log.h"
#include "esp_console.h"
#include "esp_vfs_dev.h"
#include "esp_chip_info.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

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

static const char* TAG = "standalone_console";
#define PROMPT_STR CONFIG_IDF_TARGET

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
    printf("║     " COLOR_WHITE "• GPIO Configuration & Control                               " COLOR_CYAN COLOR_BOLD"║\n");
    printf("║     " COLOR_WHITE "• HaLow WiFi (802.11ah) Connectivity                         " COLOR_CYAN COLOR_BOLD"║\n");
    printf("║     " COLOR_WHITE "• MQTT Communication Protocol                                " COLOR_CYAN COLOR_BOLD"║\n");
    printf("║     " COLOR_WHITE "• Interactive Console Commands                               " COLOR_CYAN COLOR_BOLD"║\n");
    printf("║                                                                  ║\n");
    printf("║  " COLOR_BLUE "Available Commands:" COLOR_CYAN "                                             ║\n");
    printf("║     " COLOR_YELLOW "• help      " COLOR_WHITE "- Show all available commands                    " COLOR_CYAN COLOR_BOLD"║\n");
    printf("║     " COLOR_YELLOW "• version   " COLOR_WHITE "- Display system information                     " COLOR_CYAN COLOR_BOLD"║\n");
    printf("║     " COLOR_YELLOW "• free      " COLOR_WHITE "- Show memory usage statistics                   " COLOR_CYAN COLOR_BOLD"║\n");
    printf("║     " COLOR_YELLOW "• uptime    " COLOR_WHITE "- Display system uptime                          " COLOR_CYAN COLOR_BOLD"║\n");
    printf("║     " COLOR_YELLOW "• restart   " COLOR_WHITE "- Restart the system                             " COLOR_CYAN COLOR_BOLD"║\n");
    printf("║                                                                  ║\n");
    printf("║  " COLOR_MAGENTA "💡 Tip: Type 'help' for complete command list                   " COLOR_CYAN COLOR_BOLD"║\n");
    printf("╚══════════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");
    
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    
    printf(COLOR_GREEN "🔧 Hardware Info:" COLOR_RESET "\n");
    printf("   Chip: " COLOR_BOLD COLOR_WHITE "%s" COLOR_RESET " Rev " COLOR_YELLOW "%d" COLOR_RESET 
           " | Cores: " COLOR_CYAN "%d" COLOR_RESET " | Features: " COLOR_CYAN "Halow-Wifi" COLOR_RESET, 
           CONFIG_IDF_TARGET, chip_info.revision, chip_info.cores);
    printf("\n");
    
    printf(COLOR_BLUE "📟 ESP-IDF Version: " COLOR_YELLOW "%s" COLOR_RESET "\n", esp_get_idf_version());
    printf(COLOR_GREEN "💾 Free Heap: " COLOR_CYAN "%lu" COLOR_WHITE " bytes" COLOR_RESET "\n", esp_get_free_heap_size());
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
    
    printf("ESP-IDF Version: %s\n", esp_get_idf_version());
    printf("Chip: %s Rev %d\n", CONFIG_IDF_TARGET, chip_info.revision);
    printf("Features: WiFi%s%s\n",
           (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");
    printf("Cores: %d\n", chip_info.cores);
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
    const esp_console_cmd_t restart_cmd_def = {
        .command = "restart",
        .help = "Restart the system",
        .hint = NULL,
        .func = &restart_cmd,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&restart_cmd_def));

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

static void initialize_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK( nvs_flash_erase() );
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_main(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    
    /* Prompt to be printed before each line.
     * This can be customized, made dynamic, etc.
     */
    repl_config.prompt = PROMPT_STR ">";
    repl_config.max_cmdline_length = CONFIG_CONSOLE_MAX_COMMAND_LINE_LENGTH;

    initialize_nvs();

    ESP_LOGI(TAG, "Starting Halow RTOS System");
    ESP_LOGI(TAG, "Max command line length: %d", CONFIG_CONSOLE_MAX_COMMAND_LINE_LENGTH);

    // Display welcome banner
    print_welcome_banner();

    /* Register basic commands */
    esp_console_register_help_command();
    register_basic_commands();

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
