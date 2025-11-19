/**
 * @file task_gpio.h
 * @brief GPIO control system for Halow RTOS
 * 
 * Features:
 * - Auto-initialize all GPIOs as input with pullup on boot
 * - Set GPIO direction (input/output)
 * - Configure GPIO pull mode (pullup/pulldown)
 * - Display status of all GPIOs
 */

#ifndef TASK_GPIO_H
#define TASK_GPIO_H

#include "esp_err.h"
#include "driver/gpio.h"

// GPIO configuration limits for ESP32
#define GPIO_MIN_PIN        0
#define GPIO_MAX_PIN        39
#define GPIO_LABEL_MAX_LEN  16

// GPIO direction modes
typedef enum {
    TASK_GPIO_DIR_INPUT = 0,
    TASK_GPIO_DIR_OUTPUT = 1
} task_gpio_direction_t;

// GPIO pull modes
typedef enum {
    TASK_GPIO_PULL_NONE = 0,
    TASK_GPIO_PULL_UP = 1,
    TASK_GPIO_PULL_DOWN = 2
} task_gpio_pull_mode_t;

// GPIO pin state structure
typedef struct {
    uint8_t pin;
    task_gpio_direction_t direction;
    task_gpio_pull_mode_t pull_mode;
    int level;  // Current level (0 or 1)
    bool is_valid;  // Whether this pin can be used
    char label[GPIO_LABEL_MAX_LEN + 1];  // User-defined label for this pin
} task_gpio_pin_state_t;

/**
 * @brief Initialize GPIO system
 * Automatically configures all valid GPIOs as input with pullup
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t task_gpio_init(void);

/**
 * @brief Set GPIO direction (input/output)
 * @param pin GPIO pin number
 * @param direction TASK_GPIO_DIR_INPUT or TASK_GPIO_DIR_OUTPUT
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t task_gpio_set_direction(uint8_t pin, task_gpio_direction_t direction);

/**
 * @brief Set GPIO pull mode (pullup/pulldown)
 * Only applicable for input pins
 * @param pin GPIO pin number
 * @param pull_mode TASK_GPIO_PULL_UP or TASK_GPIO_PULL_DOWN
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t task_gpio_set_pull(uint8_t pin, task_gpio_pull_mode_t pull_mode);

/**
 * @brief Set GPIO output level
 * Only applicable for output pins
 * @param pin GPIO pin number
 * @param level 0 or 1
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t task_gpio_set_output_level(uint8_t pin, int level);

/**
 * @brief Get GPIO input level
 * @param pin GPIO pin number
 * @return GPIO level (0 or 1), or -1 on error
 */
int task_gpio_get_input_level(uint8_t pin);

/**
 * @brief Get GPIO pin state
 * @param pin GPIO pin number
 * @param state Pointer to store pin state
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t task_gpio_get_pin_state(uint8_t pin, task_gpio_pin_state_t* state);

/**
 * @brief Display status of all GPIOs
 */
void task_gpio_display_status(void);

/**
 * @brief Check if GPIO pin is valid for use
 * @param pin GPIO pin number
 * @return true if valid, false otherwise
 */
bool task_gpio_is_valid_pin(uint8_t pin);

/**
 * @brief Register GPIO console commands
 */
void register_gpio_commands(void);

#endif // TASK_GPIO_H
