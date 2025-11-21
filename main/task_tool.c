/**
 * @file task_tool.c
 * @brief Network tools and utilities implementation for ESP32 RTOS
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "esp_system.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "lwip/icmp.h"
#include "lwip/ip4_addr.h"
#include "lwip/raw.h"

#include "task_tool.h"
#include "esp_log.h"
#include "esp_console.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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
 * @brief Calculate checksum for ICMP packet
 * @param data Buffer to calculate checksum for
 * @param len Length of buffer
 * @return 16-bit checksum
 */
static uint16_t icmp_checksum(uint16_t *data, int len)
{
    uint32_t sum = 0;
    for (int i = 0; i < len / 2; i++) {
        sum += data[i];
    }
    if (len % 2) {
        sum += ((uint8_t*)data)[len - 1];
    }
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    return ~sum;
}

// ICMP Echo Request/Reply structures
#pragma pack(1)
typedef struct {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t id;
    uint16_t sequence;
    uint8_t data[56]; // Standard ping data size
} icmp_packet_t;
#pragma pack()

/**
 * @brief Alternative TCP-based connectivity test (fallback)
 * @param host IP address or hostname to test
 * @param count Number of packets to test
 * @param interval_ms Interval between tests in milliseconds
 * @return 0 on success, error code otherwise
 */
static int task_tool_tcp_ping(const char* host, int count, int interval_ms)
{
    printf("Using TCP connectivity test (ICMP not available):\n\n");

    // Resolve hostname to IP
    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(80); // HTTP port for connectivity test

    // Try to parse as IP address first
    if (inet_pton(AF_INET, host, &dest_addr.sin_addr) != 1) {
        // Not a valid IP address, try to resolve hostname
        struct hostent *hostent = gethostbyname(host);
        if (!hostent || !hostent->h_addr_list[0]) {
            printf(COLOR_RED "Error: Could not resolve hostname '%s'\n" COLOR_RESET, host);
            return -1;
        }

        memcpy(&dest_addr.sin_addr.s_addr, hostent->h_addr_list[0],
               sizeof(dest_addr.sin_addr.s_addr));
    }

    int success_count = 0;
    int fail_count = 0;
    uint32_t min_rtt = UINT32_MAX;
    uint32_t max_rtt = 0;
    uint64_t total_rtt = 0;

    for (int i = 0; i < count; i++) {
        TickType_t start_time = xTaskGetTickCount();

        // Create TCP socket and try to connect (as connectivity test)
        int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock < 0) {
            printf(COLOR_RED "Request %d: Failed to create socket\n" COLOR_RESET, i + 1);
            fail_count++;
            if (i < count - 1) vTaskDelay(pdMS_TO_TICKS(interval_ms));
            continue;
        }

        // Set connection timeout
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
            printf(COLOR_GREEN "TCP Connection to %s: succeeded (time=%ldms)\n" COLOR_RESET,
                   inet_ntoa(dest_addr.sin_addr), (long)rtt);

            min_rtt = (rtt < min_rtt) ? rtt : min_rtt;
            max_rtt = (rtt > max_rtt) ? rtt : max_rtt;
            total_rtt += rtt;
        } else {
            printf(COLOR_RED "TCP Connection to %s: failed\n" COLOR_RESET, inet_ntoa(dest_addr.sin_addr));
            fail_count++;
        }

        if (i < count - 1) {
            vTaskDelay(pdMS_TO_TICKS(interval_ms));
        }
    }

    // Print statistics
    printf("\nTCP connectivity statistics for %s:\n", inet_ntoa(dest_addr.sin_addr));
    printf("    Tests: Sent = %d, Successful = %d, Failed = %d (%d%% failure rate)\n",
           count, success_count, fail_count,
           count > 0 ? (fail_count * 100) / count : 0);

    if (success_count > 0) {
        printf("Approximate connection times in milli-seconds:\n");
        printf("    Minimum = %ldms, Maximum = %ldms, Average = %ldms\n",
               (long)min_rtt, (long)max_rtt, (long)(total_rtt / success_count));
        printf("Note: These results show TCP connectivity, not ICMP ping\n");
    }

    return success_count > 0 ? 0 : -1; // Return success if at least one connection succeeded
}

/**
 * @brief Send a real ICMP ping to a remote host
 * @param host IP address or hostname to test
 * @param count Number of packets to send
 * @param interval_ms Interval between packets in milliseconds
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

    printf("Pinging %s with 64 bytes of data:\n", host);

    // Resolve hostname to IP
    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;

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

    // Create raw ICMP socket (without ICMP headers in payload)
    int sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sock < 0) {
        printf(COLOR_RED "Error: Could not create ICMP socket. Raw sockets not available.\n" COLOR_RESET);
        printf(COLOR_YELLOW "Note: ICMP ping requires raw socket support. Using alternative test.\n" COLOR_RESET);

        // Fall back to TCP connection test
        return task_tool_tcp_ping(host, count, interval_ms);
    }

    // Set receive timeout
    struct timeval tv;
    tv.tv_sec = PING_TIMEOUT_MS / 1000;
    tv.tv_usec = (PING_TIMEOUT_MS % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    int success_count = 0;
    int fail_count = 0;
    uint32_t min_rtt = UINT32_MAX;
    uint32_t max_rtt = 0;
    uint64_t total_rtt = 0;

    uint16_t ping_id = (uint16_t)(esp_random() & 0xFFFF);

    for (int seq = 0; seq < count; seq++) {
        // Prepare ICMP Echo Request packet
        icmp_packet_t request;
        memset(&request, 0, sizeof(request));
        request.type = 8; // ICMP Echo Request
        request.code = 0;
        request.id = htons(ping_id);
        request.sequence = htons(seq);
        request.checksum = 0;

        // Fill data with pattern
        for (int i = 0; i < sizeof(request.data); i++) {
            request.data[i] = 'A' + (i % 26);
        }

        // Calculate checksum
        request.checksum = icmp_checksum((uint16_t*)&request, sizeof(request));

        // Send the packet
        TickType_t start_time = xTaskGetTickCount();
        int sent = sendto(sock, &request, sizeof(request), 0,
                         (struct sockaddr*)&dest_addr, sizeof(dest_addr));

        if (sent < 0) {
            printf(COLOR_RED "Request %d: Send failed\n" COLOR_RESET, seq + 1);
            fail_count++;
            if (seq < count - 1) {
                vTaskDelay(pdMS_TO_TICKS(interval_ms));
            }
            continue;
        }

        // Receive response with timeout
        uint8_t buffer[256]; // Buffer for IP header + ICMP packet
        struct sockaddr_in src_addr;
        socklen_t src_len = sizeof(src_addr);

        // Use select with timeout to avoid blocking indefinitely
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);

        struct timeval timeout;
        timeout.tv_sec = PING_TIMEOUT_MS / 1000;
        timeout.tv_usec = (PING_TIMEOUT_MS % 1000) * 1000;

        int select_result = select(sock + 1, &readfds, NULL, NULL, &timeout);
        int received = 0;

        if (select_result > 0 && FD_ISSET(sock, &readfds)) {
            // Data is available
            received = recvfrom(sock, buffer, sizeof(buffer), 0,
                               (struct sockaddr*)&src_addr, &src_len);
        } else if (select_result == 0) {
            // Timeout
            printf(COLOR_RED "Request %d: Request timed out\n" COLOR_RESET, seq + 1);
            fail_count++;
            if (seq < count - 1) {
                vTaskDelay(pdMS_TO_TICKS(interval_ms));
            }
            continue;
        } else {
            // Error
            printf(COLOR_RED "Request %d: Select error\n" COLOR_RESET, seq + 1);
            fail_count++;
            if (seq < count - 1) {
                vTaskDelay(pdMS_TO_TICKS(interval_ms));
            }
            continue;
        }

        TickType_t end_time = xTaskGetTickCount();
        uint32_t rtt = (end_time - start_time) * portTICK_PERIOD_MS; // Convert to milliseconds

        if (received < 0) {
            printf(COLOR_RED "Request %d: Request timed out\n" COLOR_RESET, seq + 1);
            fail_count++;
        } else {
            // Extract ICMP response from buffer (skip IP header)
            icmp_packet_t *response = (icmp_packet_t *)(buffer + 20); // Skip 20-byte IP header

            if (response->type == 0 &&  // ICMP Echo Reply
                response->code == 0 &&
                ntohs(response->id) == ping_id &&
                ntohs(response->sequence) == seq) {

                printf(COLOR_GREEN "Reply from %s: bytes=64 time=%ldms\n" COLOR_RESET,
                       inet_ntoa(src_addr.sin_addr), rtt / 1000);

                success_count++;

                min_rtt = (rtt / 1000) < min_rtt ? (rtt / 1000) : min_rtt;
                max_rtt = (rtt / 1000) > max_rtt ? (rtt / 1000) : max_rtt;
                total_rtt += rtt / 1000;
            } else {
                printf(COLOR_RED "Request %d: Invalid ICMP response (type=%d, code=%d, id=%d, seq=%d)\n" COLOR_RESET,
                       seq + 1, response->type, response->code,
                       ntohs(response->id), ntohs(response->sequence));
                fail_count++;
            }
        }

        if (seq < count - 1) {
            vTaskDelay(pdMS_TO_TICKS(interval_ms));
        }
    }

    close(sock);

    // Print final statistics
    printf("\nPing statistics for %s:\n", inet_ntoa(dest_addr.sin_addr));
    printf("    Packets: Sent = %d, Received = %d, Lost = %d (%d%% loss)\n",
           count, success_count, count - success_count,
           count > 0 ? ((count - success_count) * 100) / count : 0);

    if (success_count > 0) {
        printf("Approximate round trip times in milli-seconds:\n");
        printf("    Minimum = %ldms, Maximum = %ldms, Average = %ldms\n",
               (long)min_rtt, (long)max_rtt, (long)(total_rtt / success_count));
    }

    return success_count > 0 ? 0 : -1;
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
        printf(COLOR_YELLOW "\nNote: This ping implementation uses ICMP Echo packets to test\n" COLOR_RESET);
        printf(COLOR_YELLOW "      HaLow network connectivity at the IP layer.\n" COLOR_RESET);
        printf(COLOR_YELLOW "      With timeout protection to prevent hanging.\n" COLOR_RESET);
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
