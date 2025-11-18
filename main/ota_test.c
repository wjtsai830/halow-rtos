/**
 * @file ota_test.c
 * @brief OTA A/B partition switching test implementation
 */

#include "ota_test.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include <string.h>

// static const char *TAG = "ota_test";  // Currently unused

#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_BOLD    "\033[1m"

void ota_test_show_partition_info(void)
{
    printf("\n" COLOR_CYAN COLOR_BOLD "=== OTA PARTITION STATUS ===" COLOR_RESET "\n\n");
    
    // Get current running partition
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (!running) {
        printf(COLOR_RED " Failed to get running partition!\n" COLOR_RESET);
        return;
    }
    
    printf(COLOR_GREEN " Current Running Partition:\n" COLOR_RESET);
    printf("   Label: %s\n", running->label);
    printf("   Address: 0x%lx\n", running->address);
    printf("   Size: %.1fMB\n", running->size / 1024.0 / 1024.0);
    printf("   Type: %s\n", running->type == ESP_PARTITION_TYPE_APP ? "APP" : "OTHER");
    
    // Get boot partition
    const esp_partition_t* boot = esp_ota_get_boot_partition();
    printf("\n" COLOR_BLUE " Boot Partition:\n" COLOR_RESET);
    if (boot) {
        printf("   Label: %s\n", boot->label);
        printf("   Address: 0x%lx\n", boot->address);
        printf("   Same as running: %s\n", (boot == running) ? "Yes" : "No");
    } else {
        printf("   Boot partition not found\n");
    }
    
    // Find all OTA partitions
    printf("\n" COLOR_YELLOW " Available OTA Partitions:\n" COLOR_RESET);
    
    const esp_partition_t* ota_0 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    const esp_partition_t* ota_1 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
    
    if (ota_0) {
        printf("   OTA_0: 0x%lx (%.1fMB) %s\n", ota_0->address, ota_0->size / 1024.0 / 1024.0,
               (ota_0 == running) ? COLOR_GREEN "[ACTIVE]" COLOR_RESET : "");
    }
    if (ota_1) {
        printf("   OTA_1: 0x%lx (%.1fMB) %s\n", ota_1->address, ota_1->size / 1024.0 / 1024.0,
               (ota_1 == running) ? COLOR_GREEN "[ACTIVE]" COLOR_RESET : "");
    }
    
    // Check OTA data partition
    const esp_partition_t* otadata = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
    printf("\n" COLOR_BLUE "  OTA Data Partition:\n" COLOR_RESET);
    if (otadata) {
        printf("   Found at: 0x%lx (Size: %luB)\n", otadata->address, otadata->size);
    } else {
        printf("    OTA Data partition not found!\n");
    }
    
    printf("\n");
}

esp_err_t ota_test_mark_partition_valid(void)
{
    printf(COLOR_YELLOW " Marking current partition as valid...\n" COLOR_RESET);
    
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) {
        printf(COLOR_GREEN " Partition marked as valid (rollback canceled)\n" COLOR_RESET);
    } else {
        printf(COLOR_RED " Failed to mark partition as valid: %s\n" COLOR_RESET, esp_err_to_name(err));
    }
    
    return err;
}

esp_err_t ota_test_copy_firmware_to_other_partition(void)
{
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* target = NULL;
    
    if (!running) {
        printf(COLOR_RED " Cannot get running partition\n" COLOR_RESET);
        return ESP_FAIL;
    }
    
    // Find the other partition
    if (running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) {
        target = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
    } else if (running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1) {
        target = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    } else {
        printf(COLOR_RED " Currently not running from OTA partition\n" COLOR_RESET);
        return ESP_FAIL;
    }
    
    if (!target) {
        printf(COLOR_RED " Target partition not found\n" COLOR_RESET);
        return ESP_FAIL;
    }
    
    printf(COLOR_YELLOW " Copying firmware from %s to %s...\n" COLOR_RESET, running->label, target->label);
    printf("   This may take a while (copying %.1fMB)...\n", running->size / 1024.0 / 1024.0);
    
    // Erase target partition first
    printf("    Erasing target partition...\n");
    esp_err_t err = esp_partition_erase_range(target, 0, target->size);
    if (err != ESP_OK) {
        printf(COLOR_RED " Failed to erase target partition: %s\n" COLOR_RESET, esp_err_to_name(err));
        return err;
    }
    
    // Copy data in chunks
    const size_t chunk_size = 4096;
    uint8_t *buffer = malloc(chunk_size);
    if (!buffer) {
        printf(COLOR_RED " Failed to allocate buffer\n" COLOR_RESET);
        return ESP_ERR_NO_MEM;
    }
    
    printf("    Copying firmware data...\n");
    for (size_t offset = 0; offset < running->size; offset += chunk_size) {
        size_t read_size = (offset + chunk_size > running->size) ? (running->size - offset) : chunk_size;
        
        // Read from running partition
        err = esp_partition_read(running, offset, buffer, read_size);
        if (err != ESP_OK) {
            printf(COLOR_RED " Read failed at offset 0x%x: %s\n" COLOR_RESET, offset, esp_err_to_name(err));
            break;
        }
        
        // Write to target partition
        err = esp_partition_write(target, offset, buffer, read_size);
        if (err != ESP_OK) {
            printf(COLOR_RED " Write failed at offset 0x%x: %s\n" COLOR_RESET, offset, esp_err_to_name(err));
            break;
        }
        
        // Show progress every 1MB
        if (offset % (1024 * 1024) == 0) {
            printf("    Progress: %.1fMB / %.1fMB\n", offset / 1024.0 / 1024.0, running->size / 1024.0 / 1024.0);
        }
    }
    
    free(buffer);
    
    if (err == ESP_OK) {
        printf(COLOR_GREEN " Firmware copied successfully!\n" COLOR_RESET);
    }
    
    return err;
}

esp_err_t ota_test_switch_partition(void)
{
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* target = NULL;
    
    if (!running) {
        printf(COLOR_RED " Cannot get running partition\n" COLOR_RESET);
        return ESP_FAIL;
    }
    
    // Find the other partition
    if (running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) {
        target = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
    } else if (running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1) {
        target = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    } else {
        printf(COLOR_RED " Currently not running from OTA partition\n" COLOR_RESET);
        return ESP_FAIL;
    }
    
    if (!target) {
        printf(COLOR_RED " Target partition not found\n" COLOR_RESET);
        return ESP_FAIL;
    }
    
    printf(COLOR_YELLOW " Switching from %s to %s...\n" COLOR_RESET, running->label, target->label);
    
    // Set the boot partition
    esp_err_t err = esp_ota_set_boot_partition(target);
    if (err != ESP_OK) {
        printf(COLOR_RED " Failed to set boot partition: %s\n" COLOR_RESET, esp_err_to_name(err));
        return err;
    }
    
    printf(COLOR_GREEN " Boot partition switched to %s\n" COLOR_RESET, target->label);
    printf(COLOR_CYAN "  Restart system to boot from new partition\n" COLOR_RESET);
    
    return ESP_OK;
}

esp_err_t ota_test_full_ab_cycle(void)
{
    printf(COLOR_CYAN COLOR_BOLD "\n=== FULL A/B SWITCHING TEST ===" COLOR_RESET "\n\n");
    
    printf(COLOR_BLUE " Step 1: Show current partition status\n" COLOR_RESET);
    ota_test_show_partition_info();
    
    printf(COLOR_BLUE " Step 2: Copy current firmware to other partition\n" COLOR_RESET);
    esp_err_t err = ota_test_copy_firmware_to_other_partition();
    if (err != ESP_OK) {
        return err;
    }
    
    printf(COLOR_BLUE " Step 3: Switch boot partition\n" COLOR_RESET);
    err = ota_test_switch_partition();
    if (err != ESP_OK) {
        return err;
    }
    
    printf(COLOR_BLUE " Step 4: Mark current partition as valid\n" COLOR_RESET);
    ota_test_mark_partition_valid();
    
    printf(COLOR_GREEN COLOR_BOLD "\n A/B switching test complete!\n" COLOR_RESET);
    printf(COLOR_YELLOW " Run 'restart' command to boot from new partition\n" COLOR_RESET);
    
    return ESP_OK;
}