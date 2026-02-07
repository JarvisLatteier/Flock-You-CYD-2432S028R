# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a fork of Flock You specifically adapted for the **ESP32-2432S028R** (2.8" CYD) board. It detects Flock Safety surveillance cameras and similar devices using WiFi and BLE scanning, with a full touchscreen interface.

**Target Hardware:** ESP32-2432S028R only (2.8" ILI9341 320x240 display)

## Build and Run Commands

### ESP32 Firmware

```bash
# Build with touchscreen UI
pio run -e esp32_cyd_28

# Build headless (serial + RGB LED only)
pio run -e esp32_cyd_28_headless

# Flash firmware
pio run -e esp32_cyd_28 -t upload

# Monitor serial output (115200 baud)
pio device monitor -e esp32_cyd_28
```

**macOS Note:** Upload speed must be 115200 (configured in platformio.ini). Higher speeds fail on Apple Silicon.

### Web Dashboard

```bash
cd api
python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt
python flockyou.py
```
Access dashboard at `http://localhost:5000`

## Architecture

### Source Files

| File | Purpose |
|------|---------|
| `src/main.cpp` | Detection engine, WiFi/BLE scanning, RGB LED state machine |
| `src/display_handler_28.cpp` | Touchscreen UI for ESP32-2432S028R |
| `src/display_handler_28.h` | Display handler class definition |

### Hardware Configuration

**Display (VSPI):**
- ILI9341 320x240, BGR color order
- SPI: SCK=14, MOSI=13, MISO=12, CS=15, DC=2, RST=4
- Dual backlight: GPIO 27 + 21 (both driven together via PWM)

**Touch (Separate HSPI - critical!):**
- XPT2046 on dedicated HSPI bus
- CLK=25, MOSI=32, MISO=39, CS=33, IRQ=36
- GPIO 36/39 are input-only, hence separate bus required

**RGB LED (Active LOW):**
- R=GPIO4, G=GPIO16, B=GPIO17
- PWM controlled via LEDC channels 0-2
- States: scanning (green), detection (red flash), alert (orange)

**SD Card:**
- CS=GPIO5, shares VSPI with display
- Stores `/touch_cal.txt` (calibration) and `/flockyou_detections.csv` (log)

**Speaker (SPEAK P4 Connector):**
- GPIO 26, PWM via LEDC channel 5
- Boot tone and UI feedback sounds

### Key Implementation Details

**Touch Calibration:**
- 4-point guided calibration (TL→TR→BL→BR)
- Validates against known good ranges (200-4000, span >2000)
- Persists to SD card, loads automatically on boot
- RAW_Y maps to Screen X, RAW_X maps to Screen Y (axes swapped)

**LED State Machine:**
- `LED_SCANNING`: Solid green at 50%
- `LED_DETECTED`: Red flashing, rate based on RSSI (50-400ms interval)
- `LED_ALERT`: Solid orange, transitions back to scanning after 15s timeout

**Display Pages (4-button nav: HOME, LIST, STATS, CONFIG):**
- PAGE_MAIN: Header with CH/BLE indicator, latest detection panel, LED status row
- PAGE_LIST: Scrollable detection history with color-coded indicators and signal bars
- PAGE_STATS: Detection counts, percentages, distribution bars, CLEAR button
- PAGE_SETTINGS: Custom layout (no header), SD status, Display/Sound/LED controls with +/-/MAX, CALIBRATE button
- PAGE_CALIBRATE: Full-screen 4-point calibration with CANCEL/SAVE buttons

### Detection Patterns

**SSID Patterns** (case-insensitive):
- `flock`, `fs ext battery`, `penguin`, `pigvision`

**MAC Prefixes:**
- FS Ext Battery: `58:8e:81`, `cc:cc:cc`, `ec:1b:bd`, `90:35:ea`, etc.
- Flock WiFi: `70:c9:4e`, `3c:91:80`, `d8:f3:bc`, `80:30:49`, etc.

**BLE Device Names:**
- `fs ext battery`, `penguin`, `flock`, `pigvision`

### Libraries

- **NimBLE-Arduino@^1.4.0** - BLE scanning
- **ArduinoJson@^6.21.0** - JSON serialization
- **TFT_eSPI@^2.5.43** - Display driver (configured via build flags)

## Development Notes

### Adding New Detection Patterns
Edit arrays in `src/main.cpp`:
- `wifi_ssid_patterns[]` - SSID substring matches
- `mac_prefixes[]` - First 3 octets of MAC address
- `device_name_patterns[]` - BLE advertised names

### Modifying UI Layout
Edit `src/display_handler_28.cpp`:
- `drawMainPage()` - Home screen layout
- `drawListPage()` - Detection list
- `drawStatsPage()` - Statistics view
- `drawSettingsPage()` - Configuration options
- `drawCalibrationPage()` - Touch calibration UI

### Touch Zone Registration
Touch zones are cleared each redraw cycle. Add zones in page draw functions:
```cpp
addTouchZone(x1, y1, x2, y2, callbackFunction, "label");
```

### Serial Output
JSON objects at 115200 baud, one per line. Key fields:
- `protocol`: "wifi" or "bluetooth_le"
- `detection_method`: "probe_request", "beacon", "mac_prefix", "device_name"
- `ssid`, `mac_address`, `rssi`, `channel`, `threat_score`
