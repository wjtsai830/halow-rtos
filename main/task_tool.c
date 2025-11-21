/**
 * @file task_tool.c
 * @brief Network tools and utilities implementation for ESP32 RTOS
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "esp_system.h"
#include "soc/soc.h"
#include "soc/timer_group_struct.h"
#include "soc/timer_group_reg.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "lwip/icmp.h"
#include "lwip/ip4_addr.h"

#include "task_tool.h"
#include "esp_log.h"
#include "esp_console.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <fcntl.h>
#include <unistd.h>

static const char *TAG = "task_tool";

// ANSI Color Codes
#define COLOR_RESET     "\033[0m"
#define COLOR_RED       "\033[31m"
#define COLOR_GREEN     "\033[32m"
#define COLOR_YELLOW    "\033[33m"
#define COLOR_CYAN      "\033[36m"

#define PING_DEFAULT_COUNT 4
#define PING_DEFAULT_INTERVAL 1000
#define PING_TIMEOUT_MS   3000

/**
 * @brief Send a simple ping-like connectivity test to a remote host
 * @param host IP address or hostname to test
 * @param count Number of packets to send
 * @param interval_ms Interval between packets (not currently used for simplified ping)
 * @return 0 on success, error code otherwise
 */
int task_tool_ping(const char* host, int count, int interval_ms)
{
    if (!host || strlen(host) == 0) {
        printf(COLOR_RED "Error: Host address cannot be empty\n" COLOR_RESET);
        return -1;
    }

    if (count <= 0) {
        count = PING_DEFAULT_COUNT;
    }

    if (interval_ms <= 0) {
        interval_ms = PING_DEFAULT_INTERVAL;
    }

    printf("Testing connectivity to %s...\n", host);

    // Resolve hostname to IP
    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(80); // HTTP port for connectivity test

    // Try to parse as IP address first
    if (inet_pton(AF_INET, host, &dest_addr.sin_addr) != 1) {
        // Not a valid IP address, try to resolve hostname
        printf("Resolving hostname %s...\n", host);

        struct hostent *hostent = gethostbyname(host);
        if (!hostent || !hostent->h_addr_list[0]) {
            printf(COLOR_RED "Error: Could not resolve hostname '%s'\n" COLOR_RESET, host);
            return -1;
        }

        memcpy(&dest_addr.sin_addr.s_addr, hostent->h_addr_list[0],
               sizeof(dest_addr.sin_addr.s_addr));
        printf("Resolved to %s\n", inet_ntoa(dest_addr.sin_addr));
    }

    printf("Sending TCP connection test to %s...\n\n", inet_ntoa(dest_addr.sin_addr));

    int success_count = 0;
    int fail_count = 0;
    int64_t min_rtt = INT64_MAX;
    int64_t max_rtt = 0;
    int64_t total_rtt = 0;

    for (int i = 0; i < count; i++) {
        TickType_t start_time = xTaskGetTickCount();

        // Create TCP socket and try to connect (as a connectivity test)
        int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock < 0) {
            printf(COLOR_RED "Request %d: Failed to create socket\n" COLOR_RESET, i + 1);
            fail_count++;
            if (i < count - 1) vTaskDelay(pdMS_TO_TICKS(interval_ms));
            continue;
        }

        // Set connection timeout (3 seconds)
        struct timeval tv;
        tv.tv_sec = 3;
        tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        // Try to connect
        int result = connect(sock, (struct sockaddr*)&dest_addr, sizeof(dest_addr));

        TickType_t end_time = xTaskGetTickCount();
        uint32_t rtt = (end_time - start_time) * portTICK_PERIOD_MS; // Convert to milliseconds

        close(sock);

        if (result == 0) {
            success_count++;
            printf(COLOR_GREEN "Request %d: Connected successfully (time=%ldms)\n" COLOR_RESET,
                   i + 1, (long)rtt);

            min_rtt = (rtt < min_rtt) ? rtt : min_rtt;
            max_rtt = (rtt > max_rtt) ? rtt : max_rtt;
            total_rtt += rtt;
        } else {
            printf(COLOR_RED "Request %d: Connection failed\n" COLOR_RESET, i + 1);
            fail_count++;
        }

        if (i < count - 1) {
            vTaskDelay(pdMS_TO_TICKS(interval_ms));
        }
    }

    // Print statistics
    printf("\nConnectivity test statistics for %s:\n", host);
    printf("    Tests: Sent = %d, Successful = %d, Failed = %d (%d%% failure rate)\n",
           count, success_count, fail_count,
           count > 0 ? (fail_count * 100) / count : 0);

    if (success_count > 0) {
        printf("Approximate connection times in milli-seconds:\n");
        printf("    Minimum = %lldms, Maximum = %lldms, Average = %lldms\n",
               min_rtt, max_rtt, total_rtt / success_count);
    }

    printf("\nThis test verifies HaLow network connectivity via TCP connections.\n");

    return success_count > 0 ? 0 : -1; // Return success if at least one connection succeeded
}

/**
 * @brief Initialize network tools
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t task_tool_init(void)
{
    ESP_LOGI(TAG, "Network tools initialized");
    return ESP_OK;
}

/**
 * @brief Console command handler for ping command
 */
static int ping_cmd(int argc, char **argv)
{
    if (argc < 2) {
        printf(COLOR_CYAN "Usage: ping <host> [count] [interval_ms]\n" COLOR_RESET);
        printf("  host        - IP address or hostname to test\n");
        printf("  count       - Number of connection tests to run (default: 4)\n");
        printf("  interval_ms - Interval between tests in milliseconds (default: 1000)\n");
        printf(COLOR_YELLOW "\nNote: This ping implementation uses TCP connections to test\n" COLOR_RESET);
        printf(COLOR_YELLOW "      HaLow network connectivity.\n" COLOR_RESET);
        return 0;
    }

    const char *host = argv[1];
    int count = (argc >= 3) ? atoi(argv[2]) : PING_DEFAULT_COUNT;
    int interval_ms = (argc >= 4) ? atoi(argv[3]) : PING_DEFAULT_INTERVAL;

    printf(COLOR_GREEN "Testing HaLow network connectivity...\n\n" COLOR_RESET);

    int result = task_tool_ping(host, count, interval_ms);

    printf("\nConnectivity test %s\n",
           result == 0 ? COLOR_GREEN "PASSED" COLOR_RESET : COLOR_RED "FAILED" COLOR_RESET);

    return result;
}

/**
 * @brief Register network tools console commands
 */
void register_tool_commands(void)
{
    const esp_console_cmd_t ping_cmd_def = {
        .command = "ping",
        .help = "Test HaLow network connectivity: 'ping <host> [count] [interval_ms]'",
        .hint = NULL,
        .func = &ping_cmd,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&ping_cmd_def));
}
