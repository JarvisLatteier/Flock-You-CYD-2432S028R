# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Flock You is a surveillance camera detection system that identifies Flock Safety cameras and similar surveillance devices using WiFi and Bluetooth scanning on ESP32 microcontrollers. It supports multiple hardware platforms including Xiao ESP32 S3/C3 and ESP32-2432S035C (CYD - Cheap Yellow Display).

## Build and Run Commands

### ESP32 Firmware

```bash
# Build for specific environment
pio run -e xiao_esp32s3          # Xiao ESP32 S3 (primary target)
pio run -e xiao_esp32c3          # Xiao ESP32 C3
pio run -e esp32_cyd_35          # CYD 3.5" touchscreen display

# Flash firmware to device
pio run -e xiao_esp32s3 --target upload
pio run -e esp32_cyd_35 --target upload

# Monitor serial output
pio device monitor

# Test CYD display only
pio run -e cyd_test --target upload
```

### Web Dashboard

```bash
cd api
python3 -m venv venv
source venv/bin/activate  # Windows: venv\Scripts\activate
pip install -r requirements.txt
python flockyou.py
```
Access dashboard at `http://localhost:5000`

## Architecture

### Core Components

1. **ESP32 Firmware (`src/main.cpp`)**: Main detection engine
   - WiFi promiscuous mode captures probe requests (0x04) and beacon frames (0x08)
   - Channel hopping across all 13 WiFi channels (2.4GHz) every 500ms
   - BLE scanning with NimBLE (1 second scan duration, 5 second intervals)
   - Pattern matching against crowdsourced surveillance device signatures
   - JSON serial output at 115200 baud for detected devices
   - Audio alert system with boot sequence, detection alerts, and heartbeat pulses

2. **Display Handler (`src/display_handler.cpp/h`)**: CYD touchscreen interface (optional)
   - TFT_eSPI-based UI for ILI9488/ILI9341 displays
   - Real-time detection visualization
   - Touch navigation between pages (MAIN, LIST, STATS, SET, CLR)
   - Color-coded alerts and signal strength indicators
   - Only compiled when `CYD_DISPLAY` build flag is set

3. **Web Dashboard (`api/flockyou.py`)**: Flask-based monitoring interface
   - Real-time WebSocket updates from ESP32 serial connection
   - GPS integration via NMEA-compatible USB dongles (GPGGA parsing)
   - CSV/KML export functionality for geospatial analysis
   - Detection history persistence with filtering capabilities
   - API endpoints for detection management, GPS control, and data export

4. **Detection Datasets (`datasets/`)**: Real-world device signatures
   - `FS+Ext+Battery_*.csv`: Flock Safety Extended Battery devices (1.1M records)
   - `Penguin-*.csv`: Penguin surveillance devices (4.2M records)
   - `Pigvision.csv`: Pigvision surveillance systems (47K records)
   - `Flock-*.csv`: Standard Flock Safety cameras (174K records)
   - `maximum_dots.csv`: Additional detection patterns (307K records)
   - Sourced from deflock.me crowdsourced databases

### Hardware Platform Differences

**Xiao ESP32 S3/C3 (default):**
- Buzzer on GPIO3 (D2)
- 8MB flash (S3) or 4MB flash (C3)
- Huge app partitions for larger firmware
- USB CDC on boot for serial communication
- Primary target for wardriving/mobile use

**ESP32-2432S035C (CYD):**
- Buzzer on GPIO22 (optional external)
- 480x320 ILI9488 or 320x240 ILI9341 TFT display
- XPT2046 resistive touchscreen on GPIO33 (CS), GPIO36 (IRQ)
- Display on SPI: MOSI=23, MISO=19, SCLK=18, CS=15, DC=2, RST=4, BL=21
- 4MB flash with default partitions
- Built-in visual interface (see `display_handler.cpp`)

### Detection Pattern Implementation

Detection patterns are hardcoded in `src/main.cpp` based on dataset analysis:

**SSID Patterns** (case-insensitive substring match):
- `flock`, `Flock`, `FLOCK`
- `FS Ext Battery`
- `Penguin`
- `Pigvision`

**MAC Prefixes** (first 3 octets):
- FS Ext Battery: `58:8e:81`, `cc:cc:cc`, `ec:1b:bd`, `90:35:ea`, etc.
- Flock WiFi: `70:c9:4e`, `3c:91:80`, `d8:f3:bc`, `80:30:49`, etc.
- Note: Penguin devices use locally administered OUIs (commented out in code)

**BLE Device Names**:
- `FS Ext Battery`, `Penguin`, `Flock`, `Pigvision`

### Key Libraries

- **Firmware**:
  - NimBLE-Arduino@^1.4.0 (BLE scanning)
  - ArduinoJson@^6.21.0 (JSON serialization)
  - TFT_eSPI@^2.5.43 (display, CYD only)
  - XPT2046_Touchscreen (touch, CYD only)
- **Web**:
  - Flask, Flask-SocketIO (web server, WebSocket)
  - pyserial (ESP32 serial communication)
  - pynmea2 (GPS NMEA parsing)

## Development Notes

### Build System
- Uses PlatformIO with multiple environments in `platformio.ini`
- Build flags configure display drivers and pin mappings
- TFT_eSPI configured entirely via build flags (no User_Setup.h needed)

### WiFi Promiscuous Mode
- Callback function: `wifi_promiscuous_rx_cb()`
- Filters for management frames (type 0x0, subtype 0x04 probe requests or 0x08 beacons)
- Channel hopping prevents missing devices on different channels
- RSSI values extracted from packet metadata

### BLE Scanning
- NimBLE stack runs independently of WiFi scanning
- Active scan mode with 100ms intervals
- Advertisement callbacks process device names and MAC addresses
- Scan duration of 1 second every 5 seconds to balance battery life

### Audio Alert System
- Boot sequence: 200Hz → 800Hz (300ms each)
- Detection alert: 1000Hz × 3 beeps (150ms each, 50ms gaps)
- Heartbeat pulse: 600Hz × 2 beeps every 10 seconds while device in range
- Uses Arduino `tone()` function for PWM audio generation

### Serial Output Format
- JSON objects, one per line
- Fields: `timestamp`, `detection_time`, `protocol` (wifi/ble), `detection_method`, `ssid`, `mac_address`, `rssi`, `signal_strength`, `channel`
- Extended fields: `alert_level`, `device_category`, `threat_score`, `matched_patterns`, `device_info`

## Detection Datasets

The `datasets/` directory contains crowdsourced surveillance device signatures from deflock.me with real-world MAC addresses, SSIDs, and GPS coordinates. These CSV files inform the hardcoded detection patterns in `src/main.cpp`. To update detection patterns, analyze these datasets for new MAC prefixes or SSID patterns and add them to the firmware code.