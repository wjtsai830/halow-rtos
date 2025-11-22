# HaLow RTOS - IoT System with OTA Support

| Supported Targets | ESP32-S3 |
| ----------------- | -------- |

## Overview

HaLow RTOS is an advanced IoT system built on ESP-IDF, designed for HaLow (802.11ah) WiFi connectivity with comprehensive OTA (Over-The-Air) update capabilities. The system features secure login management, MQTT integration, and A/B partition switching for reliable firmware updates.

## Key Features

###  **A/B Partition OTA Updates**
- Dual partition architecture (OTA_0 & OTA_1) for safe firmware updates
- Automatic rollback protection
- Remote updates via HaLow MQTT
- Real-time partition switching and validation

###  **Secure Login System**
- First-time setup with custom credentials
- Secure credential storage in dedicated NVS partition
- TLS certificate management ready for future enhancements

###  **HaLow WiFi Integration** ✅ COMPLETED
- Complete 802.11ah WiFi networking system
- Long-range connectivity with automatic configuration management
- Network scanning and secure connection capabilities
- Automatic credential persistence to flash storage
- Intelligent reconnection on system reboot
- MQTT communication protocol ready

###  **Interactive Console**
- Rich command-line interface with color support
- System monitoring and diagnostics
- OTA testing and management commands
- Command history and auto-completion

###  **Multi-Partition Storage**
- **Config Partition (512KB)**: GPIO, HaLow WiFi, and MQTT settings
- **Certs Partition (3.375MB)**: Login credentials and TLS certificates
- **Optimized Layout**: 16MB flash with dual 6MB application partitions

## Hardware Requirements

- **ESP32-S3** development board
- **16MB Flash** memory
- **UART/USB** connection for console access

## Partition Layout

```
┌─────────────────┬──────────┬─────────────────────────┐
│ Partition       │ Size     │ Purpose                 │
├─────────────────┼──────────┼─────────────────────────┤
│ Bootloader      │ 32KB     │ ESP-IDF bootloader      │
│ NVS             │ 24KB     │ Default system storage  │
│ PHY Init        │ 4KB      │ WiFi calibration data   │
│ OTA Data        │ 8KB      │ A/B partition control   │
│ OTA_0 (A)       │ 6MB      │ Primary application     │
│ OTA_1 (B)       │ 6MB      │ Update application      │
│ Config          │ 512KB    │ System configuration    │
│ Certs           │ 3.375MB  │ Security credentials    │
└─────────────────┴──────────┴─────────────────────────┘
```

## Quick Start

### 1. Build and Flash

```bash
# Build the project
idf.py build

# Erase flash (required for new partition table)
idf.py erase-flash

# Flash firmware and start monitor
idf.py flash monitor
```

### 2. First Time Setup

Upon first boot, the system will prompt for initial login credentials:

```
╔══════════════════════════════════════════════════════════════════╗
║                          LOGIN SYSTEM                            ║
║  First Time Setup:                                             ║
║  Please create your admin credentials.                            ║
╚══════════════════════════════════════════════════════════════════╝

Username: 
```

### 3. Available Commands

Once logged in, use these console commands:

#### System Commands
- `help` - Show all available commands
- `version` - Display system and partition information
- `free` - Show memory usage statistics
- `uptime` - Display system uptime
- `restart` - Restart the system

### **HaLow WiFi Commands** ✅ NEW
- `halow on` - Start HaLow networking service and attempt auto-connect
- `halow off` - Stop HaLow networking and disconnect
- `halow scan` - Scan for available HaLow networks
- `halow connect <ssid> [password]` - Connect to network with auto-save
- `halow status` - Display connection status, IP, and network info
- `halow version` - Show HaLow firmware and hardware version

#### OTA Commands
- `ota_info` - Show OTA partition information
- `ota_copy` - Copy current firmware to other partition
- `ota_switch` - Switch to other partition (requires restart)
- `ota_valid` - Mark current partition as valid
- `ota_test` - Run full A/B partition switching test

## OTA Testing

### A/B Partition Switching Test

Test the OTA functionality without requiring actual firmware updates:

```bash
# Check current partition status
esp32s3> ota_info

# Run complete A/B switching test
esp32s3> ota_test

# Restart to switch partitions
esp32s3> restart
```

The `ota_test` command will:
1. Display current partition information
2. Copy running firmware to the inactive partition (~1-2 minutes)
3. Switch boot partition to the copied firmware
4. Mark the new partition as valid

After restart, the system will boot from the alternate partition, demonstrating successful A/B switching.

## Configuration

### Debug Mode

Enable login debug messages:
```bash
idf.py menuconfig
# Component config → HaLow RTOS Config → Enable login debug output
```

### Console Interface

The system supports multiple console interfaces:
- **UART** (default)
- **USB Serial JTAG** 
- **USB CDC**

Configure via `idf.py menuconfig` under ESP System Settings.

## Project Structure

```
halow-rtos/
├── main/
│   ├── task_main.c          # Main application and console
│   ├── task_login.c/.h      # Login system implementation
│   ├── config_manager.h     # Configuration management API
│   ├── ota_manager.h        # OTA management framework
│   ├── ota_test.c/.h        # OTA testing utilities
│   └── CMakeLists.txt       # Build configuration
├── partitions.csv           # Custom partition table
├── sdkconfig               # ESP-IDF configuration
└── README.md               # This file
```

## Development Notes

### Extending the System

- **Configuration Management**: Implement `config_manager.c` for structured settings
- **MQTT Integration**: Add HaLow WiFi and MQTT connectivity
- **TLS Security**: Enhance certificate management in certs partition
- **Web Interface**: Add HTTP server for remote configuration

### Troubleshooting

**Login Issues**: 
- Enable debug mode to see credential storage details
- Use `idf.py erase-flash` to clear all stored data

**OTA Issues**:
- Ensure both OTA partitions are available (`ota_info`)
- Check sufficient free memory before running `ota_test`
- Verify partition table is correctly flashed

**Console Issues**:
- Use Windows Terminal or Putty for full escape sequence support
- Check baud rate configuration (115200 default)

## Future Enhancements

- [ ] HaLow WiFi connectivity implementation
- [ ] MQTT communication protocol
- [ ] Remote OTA updates via MQTT
- [ ] Web-based configuration interface
- [ ] Advanced security features
- [ ] Real-time monitoring dashboard

## License

This project is licensed under the terms specified in the LICENSE file.

---

**Built with ESP-IDF** | **Optimized for ESP32-S3** | **Ready for Production**
