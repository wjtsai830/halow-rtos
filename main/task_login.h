/**
 * @file task_login.h
 * @brief Two-stage login system for Halow RTOS
 * 
 * Features:
 * - Username/Password authentication (max 16 chars each)
 * - First-time login creates credentials stored in certs partition
 * - Hidden admin account (admin/12345678) 
 * - Prevents admin account registration by users
 * - Dynamic prompt based on logged-in user
 */

#ifndef TASK_LOGIN_H
#define TASK_LOGIN_H

#include <stdbool.h>

// Login configuration
#define MAX_USERNAME_LEN    16
#define MAX_PASSWORD_LEN    16
#define ADMIN_USERNAME      "admin"
#define ADMIN_PASSWORD      "12345678"

// Certs partition namespaces (3.375MB total)
#define CREDS_NAMESPACE     "login_creds"    // User login credentials
#define TLS_NAMESPACE       "tls_certs"      // Future: HaLow WiFi TLS certificates
#define DEVICE_NAMESPACE    "device_certs"   // Future: Device identity certificates

// Login credential keys
#define USERNAME_KEY        "username"
#define PASSWORD_KEY        "password"

// Future TLS certificate keys (预留)
#define TLS_CA_CERT_KEY     "ca_cert"        // CA certificate for HaLow
#define TLS_CLIENT_CERT_KEY "client_cert"    // Client certificate
#define TLS_CLIENT_KEY_KEY  "client_key"     // Client private key

// Login states
typedef enum {
    LOGIN_STATE_USERNAME,
    LOGIN_STATE_PASSWORD,
    LOGIN_STATE_LOGGED_IN,
    LOGIN_STATE_FAILED
} login_state_t;

// Login result structure
typedef struct {
    bool success;
    char username[MAX_USERNAME_LEN + 1];
    bool is_admin;
    bool is_first_time;
} login_result_t;

/**
 * @brief Initialize login system
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t login_init(void);

/**
 * @brief Check if this is first time login (no credentials stored)
 * @return true if first time, false if credentials exist
 */
bool is_first_time_login(void);

/**
 * @brief Handle login process
 * @param input User input string
 * @param result Pointer to login result structure
 * @return Current login state
 */
login_state_t handle_login_input(const char* input, login_result_t* result);

/**
 * @brief Store user credentials in NVS (certs partition)
 * @param username Username to store
 * @param password Password to store
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t store_credentials(const char* username, const char* password);

/**
 * @brief Verify login credentials
 * @param username Username to verify
 * @param password Password to verify
 * @return true if credentials are valid, false otherwise
 */
bool verify_credentials(const char* username, const char* password);

/**
 * @brief Get current login prompt string
 * @param username Current logged-in username
 * @param prompt_buffer Buffer to store prompt string
 * @param buffer_size Size of prompt buffer
 */
void get_login_prompt(const char* username, char* prompt_buffer, size_t buffer_size);

/**
 * @brief Display login instructions
 */
void display_login_banner(void);

#endif // TASK_LOGIN_H