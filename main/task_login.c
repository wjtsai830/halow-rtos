/**
 * @file task_login.c
 * @brief Implementation of two-stage login system for Halow RTOS
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "task_login.h"

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

static login_state_t current_state = LOGIN_STATE_USERNAME;
static char temp_username[MAX_USERNAME_LEN + 1] = {0};
static char temp_password[MAX_PASSWORD_LEN + 1] = {0};

// Debug logging macros
#ifdef CONFIG_LOGIN_DEBUG_ENABLE
    static const char* TAG = "login";
    #define LOGIN_LOGD(format, ...) ESP_LOGI(TAG, format, ##__VA_ARGS__)
    #define LOGIN_LOGE(format, ...) ESP_LOGE(TAG, format, ##__VA_ARGS__)
#else
    static const char* TAG = "login";
    #define LOGIN_LOGD(format, ...) // Debug disabled  
    #define LOGIN_LOGE(format, ...) ESP_LOGE(TAG, format, ##__VA_ARGS__) // Always show critical errors
#endif

esp_err_t login_init(void)
{
    // NVS should already be initialized in main
    current_state = LOGIN_STATE_USERNAME;
    memset(temp_username, 0, sizeof(temp_username));
    memset(temp_password, 0, sizeof(temp_password));
    
    LOGIN_LOGD("Login system initialized");
    return ESP_OK;
}

bool is_first_time_login(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;
    size_t required_size = 0;
    
    // Try certs partition first
    LOGIN_LOGD("is_first_time_login() - trying certs partition...");
    err = nvs_open_from_partition("certs", CREDS_NAMESPACE, NVS_READONLY, &nvs_handle);
    LOGIN_LOGD("nvs_open_from_partition(certs) result: %s", esp_err_to_name(err));
    
    if (err != ESP_OK) {
        LOGIN_LOGD("Certs partition not available, trying default NVS...");
        
        // Fallback to default NVS partition
        err = nvs_open(CREDS_NAMESPACE, NVS_READONLY, &nvs_handle);
        LOGIN_LOGD("nvs_open(default) result: %s", esp_err_to_name(err));
        if (err != ESP_OK) {
            LOGIN_LOGD("No NVS available anywhere, first time login");
            return true;
        }
    } else {
        LOGIN_LOGD("Certs partition opened successfully for reading");
    }
    
    // Check if username exists
    LOGIN_LOGD("Checking for username key '%s'...", USERNAME_KEY);
    err = nvs_get_str(nvs_handle, USERNAME_KEY, NULL, &required_size);
    
    LOGIN_LOGD("NVS get_str result: %s, required_size: %zu", esp_err_to_name(err), required_size);
    
    nvs_close(nvs_handle);
    
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        LOGIN_LOGD("No stored credentials found, first time login");
        return true;
    } else if (err == ESP_OK && required_size > 0) {
        LOGIN_LOGD("Stored credentials found, existing user login");
        return false;
    }
    
    LOGIN_LOGD("Unexpected NVS error: %s", esp_err_to_name(err));
    return true; // Default to first time if unsure
}

esp_err_t store_credentials(const char* username, const char* password)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;
    
    if (!username || !password) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Try to use certs partition first
    LOGIN_LOGD("Trying to open certs partition for credential storage...");
    err = nvs_open_from_partition("certs", CREDS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    LOGIN_LOGD("nvs_open_from_partition result: %s", esp_err_to_name(err));
    
    if (err != ESP_OK) {
        LOGIN_LOGD("Failed to open certs partition: %s, falling back to default NVS", esp_err_to_name(err));
        
        // Fallback to default NVS partition
        err = nvs_open(CREDS_NAMESPACE, NVS_READWRITE, &nvs_handle);
        if (err != ESP_OK) {
            LOGIN_LOGE("Failed to open default NVS handle: %s", esp_err_to_name(err));
            return err;
        }
    } else {
        LOGIN_LOGD("Certs partition opened successfully for credential storage");
    }
    
    // Store username
    err = nvs_set_str(nvs_handle, USERNAME_KEY, username);
    if (err != ESP_OK) {
        LOGIN_LOGE("Failed to store username: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    // Store password
    err = nvs_set_str(nvs_handle, PASSWORD_KEY, password);
    if (err != ESP_OK) {
        LOGIN_LOGE("Failed to store password: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    // Commit changes
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    
    if (err == ESP_OK) {
        LOGIN_LOGD("Credentials stored successfully for user: %s", username);
    }
    
    return err;
}

bool verify_credentials(const char* username, const char* password)
{
    if (!username || !password) {
        return false;
    }
    
    // Check admin credentials first
    if (strcmp(username, ADMIN_USERNAME) == 0) {
        return (strcmp(password, ADMIN_PASSWORD) == 0);
    }
    
    // Check stored user credentials
    nvs_handle_t nvs_handle;
    esp_err_t err;
    char stored_username[MAX_USERNAME_LEN + 1] = {0};
    char stored_password[MAX_PASSWORD_LEN + 1] = {0};
    size_t required_size;
    
    err = nvs_open_from_partition("certs", CREDS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        // Fallback to default NVS partition
        err = nvs_open(CREDS_NAMESPACE, NVS_READONLY, &nvs_handle);
        if (err != ESP_OK) {
            return false;
        }
    }
    
    // Get stored username
    required_size = sizeof(stored_username);
    err = nvs_get_str(nvs_handle, USERNAME_KEY, stored_username, &required_size);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return false;
    }
    
    // Get stored password
    required_size = sizeof(stored_password);
    err = nvs_get_str(nvs_handle, PASSWORD_KEY, stored_password, &required_size);
    nvs_close(nvs_handle);
    
    if (err != ESP_OK) {
        return false;
    }
    
    // Compare credentials (case sensitive)
    return (strcmp(username, stored_username) == 0 && strcmp(password, stored_password) == 0);
}

static bool is_valid_input(const char* input, size_t max_len)
{
    if (!input || strlen(input) == 0 || strlen(input) > max_len) {
        return false;
    }
    
    // Check for printable ASCII characters only
    for (size_t i = 0; i < strlen(input); i++) {
        if (!isprint((unsigned char)input[i]) || isspace((unsigned char)input[i])) {
            return false;
        }
    }
    
    return true;
}

login_state_t handle_login_input(const char* input, login_result_t* result)
{
    if (!input || !result) {
        return LOGIN_STATE_FAILED;
    }
    
    // Initialize result
    memset(result, 0, sizeof(login_result_t));
    
    switch (current_state) {
        case LOGIN_STATE_USERNAME:
            // Validate username input
            if (!is_valid_input(input, MAX_USERNAME_LEN)) {
                printf(COLOR_RED " Invalid username. Must be 1-%d printable characters, no spaces.\n" COLOR_RESET, MAX_USERNAME_LEN);
                return LOGIN_STATE_USERNAME;
            }
            
            // Check if trying to register admin with wrong password on first login
            if (is_first_time_login() && strcmp(input, ADMIN_USERNAME) == 0) {
                printf(COLOR_RED " Cannot register 'admin' account. Please choose a different username.\n" COLOR_RESET);
                return LOGIN_STATE_USERNAME;
            }
            
            strncpy(temp_username, input, MAX_USERNAME_LEN);
            temp_username[MAX_USERNAME_LEN] = '\0';
            
            printf(COLOR_CYAN " Password (max %d chars, hidden): " COLOR_RESET, MAX_PASSWORD_LEN);
            current_state = LOGIN_STATE_PASSWORD;
            return LOGIN_STATE_PASSWORD;
            
        case LOGIN_STATE_PASSWORD:
            // Validate password input
            if (!is_valid_input(input, MAX_PASSWORD_LEN)) {
                printf(COLOR_RED " Invalid password. Must be 1-%d printable characters, no spaces.\n" COLOR_RESET, MAX_PASSWORD_LEN);
                printf(COLOR_CYAN " Password (max %d chars): " COLOR_RESET, MAX_PASSWORD_LEN);
                return LOGIN_STATE_PASSWORD;
            }
            
            strncpy(temp_password, input, MAX_PASSWORD_LEN);
            temp_password[MAX_PASSWORD_LEN] = '\0';
            
            // Check if this is truly first time login (no credentials exist)
            bool is_first_time = is_first_time_login();
            
            if (is_first_time) {
                // First time setup - register new credentials
                LOGIN_LOGD("Attempting to store credentials for user: %s", temp_username);
                esp_err_t err = store_credentials(temp_username, temp_password);
                if (err != ESP_OK) {
                    printf(COLOR_RED " Failed to store credentials. Error: %s\n" COLOR_RESET, esp_err_to_name(err));
                    current_state = LOGIN_STATE_USERNAME;
                    return LOGIN_STATE_FAILED;
                }
                
                printf(COLOR_GREEN " First-time setup complete! Credentials stored.\n" COLOR_RESET);
                LOGIN_LOGD("Verifying storage by checking is_first_time_login() again...");
                bool verify_check = is_first_time_login();
                LOGIN_LOGD("After storage, is_first_time_login() = %s", 
                          verify_check ? "TRUE (PROBLEM!)" : "FALSE (OK)");
                result->success = true;
                result->is_first_time = true;
                strncpy(result->username, temp_username, MAX_USERNAME_LEN);
                result->is_admin = false;
                current_state = LOGIN_STATE_LOGGED_IN;
                return LOGIN_STATE_LOGGED_IN;
            } else {
                // System already has registered user - only allow login with existing credentials
                if (verify_credentials(temp_username, temp_password)) {
                    printf(COLOR_GREEN " Login successful! Welcome, %s!\n" COLOR_RESET, temp_username);
                    result->success = true;
                    result->is_first_time = false;
                    strncpy(result->username, temp_username, MAX_USERNAME_LEN);
                    result->is_admin = (strcmp(temp_username, ADMIN_USERNAME) == 0);
                    current_state = LOGIN_STATE_LOGGED_IN;
                    return LOGIN_STATE_LOGGED_IN;
                } else {
                    printf(COLOR_RED " Invalid username or password.\n" COLOR_RESET);
                    printf(COLOR_YELLOW "💡 System already configured. Use existing credentials or admin account.\n" COLOR_RESET);
                    current_state = LOGIN_STATE_USERNAME;
                    return LOGIN_STATE_FAILED;
                }
            }
            
        default:
            return LOGIN_STATE_FAILED;
    }
}

void get_login_prompt(const char* username, char* prompt_buffer, size_t buffer_size)
{
    if (!username || !prompt_buffer) {
        return;
    }
    
    snprintf(prompt_buffer, buffer_size, "%s>", username);
}

void display_login_banner(void)
{
    bool is_first_time = is_first_time_login();
    
    LOGIN_LOGD("is_first_time_login() returned: %s", 
               is_first_time ? "TRUE (first time)" : "FALSE (credentials exist)");
    
    printf("\n");
    printf(COLOR_CYAN COLOR_BOLD "╔══════════════════════════════════════════════════════════════════╗\n");
    printf("║" COLOR_WHITE "                          LOGIN SYSTEM                            " COLOR_CYAN "║\n");
    printf("║" COLOR_YELLOW "                        Halow RTOS Access                         " COLOR_CYAN "║\n");
    printf("╠══════════════════════════════════════════════════════════════════╣\n");
    
    if (is_first_time) {
        printf("║  " COLOR_GREEN "  First Time Setup:    "COLOR_CYAN"                                         ║\n");
        printf("║     " COLOR_WHITE "• Create your login credentials                              " COLOR_CYAN "║\n");
        printf("║     " COLOR_WHITE "• Username & Password: max 16 chars each                     " COLOR_CYAN "║\n");
        printf("║     " COLOR_WHITE "• Case sensitive, no spaces allowed                          " COLOR_CYAN "║\n");
        // printf("║     " COLOR_RED "• Cannot use 'admin' as username                             " COLOR_CYAN "║\n");
    } else {
        printf("║  " COLOR_BLUE "  System Login:  " COLOR_CYAN "                                               ║\n");
        printf("║     " COLOR_WHITE "• System already configured                                  " COLOR_CYAN "║\n");
        printf("║     " COLOR_WHITE "• Use existing user credentials                              " COLOR_CYAN "║\n");
        printf("║     " COLOR_WHITE "• Or use admin account for system access                     " COLOR_CYAN "║\n");
        printf("║     " COLOR_RED "• New user registration is disabled                          " COLOR_CYAN "║\n");
    }
    
    printf("║                                                                  ║\n");
    // printf("║  " COLOR_MAGENTA "  Admin account: Always available for system management        " COLOR_CYAN "║\n");
    printf("╚══════════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");
    printf("\n");
    printf(COLOR_CYAN " Username (max %d chars): " COLOR_RESET, MAX_USERNAME_LEN);
}