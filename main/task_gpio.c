/**
 * @file task_gpio.c
 * @brief GPIO control system implementation for Halow RTOS
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "task_gpio.h"
#include "esp_log.h"
#include "esp_console.h"
#include "driver/gpio.h"

static const char* TAG = "task_gpio";

// ANSI Color Codes
#define COLOR_RESET     "\033[0m"
#define COLOR_BOLD      "\033[1m"
#define COLOR_RED       "\033[31m"
#define COLOR_GREEN     "\033[32m"
#define COLOR_YELLOW    "\033[33m"
#define COLOR_BLUE      "\033[34m"
#define COLOR_CYAN      "\033[36m"
#define COLOR_WHITE     "\033[37m"

// GPIO pin state tracking
static task_gpio_pin_state_t gpio_states[GPIO_MAX_PIN + 1];

/**
 * @brief Check if GPIO pin is valid for use
 * Some pins are reserved or input-only on ESP32
 */
bool task_gpio_is_valid_pin(uint8_t pin)
{
    // ESP32 specific pin restrictions
    // GPIO 34-39 are input only (no output, no pullup/pulldown)
    // GPIO 6-11 are connected to flash (should not be used)
    // GPIO 20, 24, 28-31 do not exist on ESP32
    
    if (pin > GPIO_MAX_PIN) {
        return false;
    }
    
    // Flash pins - do not use
    if (pin >= 6 && pin <= 11) {
        return false;
    }
    
    // Non-existent pins on ESP32
    if (pin == 20 || pin == 24 || (pin >= 28 && pin <= 31)) {
        return false;
    }
    
    return true;
}

/**
 * @brief Check if GPIO pin supports output mode
 */
static bool gpio_supports_output(uint8_t pin)
{
    // GPIO 34-39 are input only
    if (pin >= 34 && pin <= 39) {
        return false;
    }
    return true;
}

/**
 * @brief Check if GPIO pin supports pull resistors
 */
static bool gpio_supports_pull(uint8_t pin)
{
    // GPIO 34-39 do not have internal pull resistors
    if (pin >= 34 && pin <= 39) {
        return false;
    }
    return true;
}

/**
 * @brief Initialize GPIO system
 * Only initializes the state tracking, does not configure hardware
 */
esp_err_t task_gpio_init(void)
{
    ESP_LOGI(TAG, "Initializing GPIO system...");
    
    // Initialize state tracking for all pins
    for (uint8_t pin = GPIO_MIN_PIN; pin <= GPIO_MAX_PIN; pin++) {
        gpio_states[pin].pin = pin;
        gpio_states[pin].is_valid = task_gpio_is_valid_pin(pin);
        gpio_states[pin].direction = TASK_GPIO_DIR_INPUT;
        gpio_states[pin].pull_mode = TASK_GPIO_PULL_NONE;
        gpio_states[pin].level = 0;
        gpio_states[pin].label[0] = '\0';  // Empty label
    }
    
    ESP_LOGI(TAG, "GPIO system initialized (on-demand configuration)");
    return ESP_OK;
}

/**
 * @brief Set GPIO direction
 */
esp_err_t task_gpio_set_direction(uint8_t pin, task_gpio_direction_t direction)
{
    if (!task_gpio_is_valid_pin(pin)) {
        ESP_LOGE(TAG, "Invalid GPIO pin: %d", pin);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (direction == TASK_GPIO_DIR_OUTPUT && !gpio_supports_output(pin)) {
        ESP_LOGE(TAG, "GPIO %d does not support output mode (input only)", pin);
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    gpio_mode_t mode = (direction == TASK_GPIO_DIR_OUTPUT) ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT;
    esp_err_t err = gpio_set_direction(pin, mode);
    
    if (err == ESP_OK) {
        gpio_states[pin].direction = direction;
        ESP_LOGI(TAG, "GPIO %d set to %s", pin, 
                 direction == TASK_GPIO_DIR_OUTPUT ? "OUTPUT" : "INPUT");
    }
    
    return err;
}

/**
 * @brief Set GPIO pull mode
 */
esp_err_t task_gpio_set_pull(uint8_t pin, task_gpio_pull_mode_t pull_mode)
{
    if (!task_gpio_is_valid_pin(pin)) {
        ESP_LOGE(TAG, "Invalid GPIO pin: %d", pin);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!gpio_supports_pull(pin)) {
        ESP_LOGE(TAG, "GPIO %d does not support pull resistors", pin);
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    if (gpio_states[pin].direction != TASK_GPIO_DIR_INPUT) {
        ESP_LOGW(TAG, "GPIO %d is not in input mode, pull resistor may not work as expected", pin);
    }
    
    esp_err_t err = ESP_OK;
    
    switch (pull_mode) {
        case TASK_GPIO_PULL_UP:
            err = gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY);
            break;
        case TASK_GPIO_PULL_DOWN:
            err = gpio_set_pull_mode(pin, GPIO_PULLDOWN_ONLY);
            break;
        case TASK_GPIO_PULL_NONE:
            err = gpio_set_pull_mode(pin, GPIO_FLOATING);
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }
    
    if (err == ESP_OK) {
        gpio_states[pin].pull_mode = pull_mode;
        const char* pull_str = (pull_mode == TASK_GPIO_PULL_UP) ? "PULLUP" :
                               (pull_mode == TASK_GPIO_PULL_DOWN) ? "PULLDOWN" : "NONE";
        ESP_LOGI(TAG, "GPIO %d pull mode set to %s", pin, pull_str);
    }
    
    return err;
}

/**
 * @brief Set GPIO output level
 */
esp_err_t task_gpio_set_output_level(uint8_t pin, int level)
{
    if (!task_gpio_is_valid_pin(pin)) {
        ESP_LOGE(TAG, "Invalid GPIO pin: %d", pin);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (gpio_states[pin].direction != TASK_GPIO_DIR_OUTPUT) {
        ESP_LOGE(TAG, "GPIO %d is not configured as output", pin);
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t err = gpio_set_level(pin, level ? 1 : 0);
    
    if (err == ESP_OK) {
        gpio_states[pin].level = level ? 1 : 0;
        ESP_LOGI(TAG, "GPIO %d output set to %d", pin, gpio_states[pin].level);
    }
    
    return err;
}

/**
 * @brief Get GPIO input level
 */
int task_gpio_get_input_level(uint8_t pin)
{
    if (!task_gpio_is_valid_pin(pin)) {
        ESP_LOGE(TAG, "Invalid GPIO pin: %d", pin);
        return -1;
    }
    
    int level = gpio_get_level(pin);
    gpio_states[pin].level = level;
    return level;
}

/**
 * @brief Get GPIO pin state
 */
esp_err_t task_gpio_get_pin_state(uint8_t pin, task_gpio_pin_state_t* state)
{
    if (!task_gpio_is_valid_pin(pin) || state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Update current level
    gpio_states[pin].level = gpio_get_level(pin);
    
    // Copy state
    memcpy(state, &gpio_states[pin], sizeof(task_gpio_pin_state_t));
    
    return ESP_OK;
}

/**
 * @brief Display status of all GPIOs
 */
void task_gpio_display_status(void)
{
    printf("\n" COLOR_CYAN COLOR_BOLD "=== GPIO STATUS ===" COLOR_RESET "\n\n");
    printf(COLOR_YELLOW "Pin  Direction  Pull Mode  Level  Label" COLOR_RESET "\n");
    printf("---  ---------  ---------  -----  -----\n");
    
    for (uint8_t pin = GPIO_MIN_PIN; pin <= GPIO_MAX_PIN; pin++) {
        if (!task_gpio_is_valid_pin(pin)) {
            continue;
        }
        
        // Read real-time hardware level
        int current_level = gpio_get_level(pin);
        gpio_states[pin].level = current_level;
        
        const char* dir_str = (gpio_states[pin].direction == TASK_GPIO_DIR_OUTPUT) ? 
                              COLOR_GREEN "OUTPUT" COLOR_RESET : COLOR_BLUE "INPUT " COLOR_RESET;
        
        const char* pull_str;
        if (gpio_states[pin].pull_mode == TASK_GPIO_PULL_UP) {
            pull_str = "PULLUP  ";
        } else if (gpio_states[pin].pull_mode == TASK_GPIO_PULL_DOWN) {
            pull_str = "PULLDOWN";
        } else {
            pull_str = "NONE    ";
        }
        
        const char* level_str = current_level ? 
                                COLOR_GREEN "HIGH" COLOR_RESET : COLOR_WHITE "LOW " COLOR_RESET;
        
        // Display label if set, otherwise show notes
        const char* label_or_notes = gpio_states[pin].label[0] != '\0' ? 
                                     gpio_states[pin].label : 
                                     (pin >= 34 && pin <= 39) ? "(Input only)" : "";
        
        printf("%2d     %s   %s   %s   %s\n", 
               pin, dir_str, pull_str, level_str, label_or_notes);
    }
    
    printf("\n");
}

// Console command implementations

static int gpio_cmd(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage:\n");
        printf("  gpio status                   - Show all GPIO status\n");
        printf("  gpio set <pin> <input|output> - Set GPIO direction\n");
        printf("  gpio config <pin> <label>     - Set GPIO label (max 16 chars)\n");
        printf("  gpio <pin> <high|low>         - Set output high/low or pullup/pulldown\n");
        printf("\nExamples:\n");
        printf("  gpio status\n");
        printf("  gpio set 2 output\n");
        printf("  gpio config 5 led\n");
        printf("  gpio 2 high              (output: set HIGH, input: set PULLUP)\n");
        printf("  gpio 2 low               (output: set LOW, input: set PULLDOWN)\n");
        return 1;
    }
    
    // Handle "gpio status"
    if (strcmp(argv[1], "status") == 0) {
        task_gpio_display_status();
        return 0;
    }
    
    // Handle "gpio config <pin> <label>"
    if (strcmp(argv[1], "config") == 0) {
        if (argc < 4) {
            printf(COLOR_RED "Error: Usage: gpio config <pin> <label>\n" COLOR_RESET);
            return 1;
        }
        
        int pin = atoi(argv[2]);
        if (pin < GPIO_MIN_PIN || pin > GPIO_MAX_PIN) {
            printf(COLOR_RED "Error: Invalid pin number (0-%d)\n" COLOR_RESET, GPIO_MAX_PIN);
            return 1;
        }
        
        if (!task_gpio_is_valid_pin(pin)) {
            printf(COLOR_RED "Error: GPIO %d is not available\n" COLOR_RESET, pin);
            return 1;
        }
        
        // Copy label with length limit
        strncpy(gpio_states[pin].label, argv[3], GPIO_LABEL_MAX_LEN);
        gpio_states[pin].label[GPIO_LABEL_MAX_LEN] = '\0';  // Ensure null termination
        
        printf(COLOR_GREEN "GPIO %d label set to '%s'\n" COLOR_RESET, pin, gpio_states[pin].label);
        return 0;
    }
    
    // Handle "gpio set <pin> <input|output>"
    if (strcmp(argv[1], "set") == 0) {
        if (argc < 4) {
            printf(COLOR_RED "Error: Usage: gpio set <pin> <input|output>\n" COLOR_RESET);
            return 1;
        }
        
        int pin = atoi(argv[2]);
        if (pin < GPIO_MIN_PIN || pin > GPIO_MAX_PIN) {
            printf(COLOR_RED "Error: Invalid pin number (0-%d)\n" COLOR_RESET, GPIO_MAX_PIN);
            return 1;
        }
        
        task_gpio_direction_t direction;
        if (strcmp(argv[3], "input") == 0) {
            direction = TASK_GPIO_DIR_INPUT;
        } else if (strcmp(argv[3], "output") == 0) {
            direction = TASK_GPIO_DIR_OUTPUT;
        } else {
            printf(COLOR_RED "Error: Direction must be 'input' or 'output'\n" COLOR_RESET);
            return 1;
        }
        
        esp_err_t err = task_gpio_set_direction(pin, direction);
        if (err != ESP_OK) {
            printf(COLOR_RED "Error: Failed to set GPIO direction: %s\n" COLOR_RESET, 
                   esp_err_to_name(err));
            return 1;
        }
        
        printf(COLOR_GREEN "GPIO %d set to %s\n" COLOR_RESET, pin, argv[3]);
        return 0;
    }
    
    // Handle "gpio <pin> <high|low>"
    if (argc >= 3) {
        int pin = atoi(argv[1]);
        if (pin < GPIO_MIN_PIN || pin > GPIO_MAX_PIN) {
            printf(COLOR_RED "Error: Invalid pin number (0-%d)\n" COLOR_RESET, GPIO_MAX_PIN);
            return 1;
        }
        
        if (!task_gpio_is_valid_pin(pin)) {
            printf(COLOR_RED "Error: GPIO %d is not available\n" COLOR_RESET, pin);
            return 1;
        }
        
        bool is_high;
        if (strcmp(argv[2], "high") == 0) {
            is_high = true;
        } else if (strcmp(argv[2], "low") == 0) {
            is_high = false;
        } else {
            printf(COLOR_RED "Error: Must be 'high' or 'low'\n" COLOR_RESET);
            return 1;
        }
        
        // Check current direction
        if (gpio_states[pin].direction == TASK_GPIO_DIR_OUTPUT) {
            // Output mode: set level
            esp_err_t err = task_gpio_set_output_level(pin, is_high ? 1 : 0);
            if (err != ESP_OK) {
                printf(COLOR_RED "Error: Failed to set GPIO level: %s\n" COLOR_RESET, 
                       esp_err_to_name(err));
                return 1;
            }
            // Verify the level was set correctly
            int actual_level = gpio_get_level(pin);
            printf(COLOR_GREEN "GPIO %d output set to %s (actual: %s)\n" COLOR_RESET, pin, 
                   is_high ? "HIGH" : "LOW",
                   actual_level ? "HIGH" : "LOW");
        } else {
            // Input mode: set pull resistor
            task_gpio_pull_mode_t pull_mode = is_high ? TASK_GPIO_PULL_UP : TASK_GPIO_PULL_DOWN;
            esp_err_t err = task_gpio_set_pull(pin, pull_mode);
            if (err != ESP_OK) {
                printf(COLOR_RED "Error: Failed to set GPIO pull mode: %s\n" COLOR_RESET, 
                       esp_err_to_name(err));
                return 1;
            }
            printf(COLOR_GREEN "GPIO %d pull mode set to %s\n" COLOR_RESET, pin, 
                   is_high ? "PULLUP" : "PULLDOWN");
        }
        
        return 0;
    }
    
    printf(COLOR_RED "Error: Invalid command format\n" COLOR_RESET);
    return 1;
}

/**
 * @brief Register GPIO console commands
 */
void register_gpio_commands(void)
{
    const esp_console_cmd_t gpio_cmd_def = {
        .command = "gpio",
        .help = "GPIO control: 'gpio status' | 'gpio set <pin> <input|output>' | 'gpio config <pin> <label>' | 'gpio <pin> <high|low>'",
        .hint = NULL,
        .func = &gpio_cmd,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&gpio_cmd_def));
}
