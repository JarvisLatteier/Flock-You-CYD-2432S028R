# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a fork of Flock You that detects Flock Safety surveillance cameras and similar devices using WiFi and BLE scanning. Supports multiple ESP32 boards with display UIs.

**Supported Hardware:**
- **ESP32-2432S028R** (2.8" CYD) - ILI9341 320x240 touchscreen
- **Waveshare ESP32-S3-LCD-1.47** - ST7789 172x320, BOOT button nav, WS2812 RGB LED

## Build and Run Commands

### ESP32-2432S028R (2.8" CYD)

```bash
pio run -e esp32_cyd_28              # Build with touchscreen UI
pio run -e esp32_cyd_28_headless     # Build headless (serial + RGB LED only)
pio run -e esp32_cyd_28 -t upload    # Flash firmware
pio device monitor -e esp32_cyd_28   # Monitor serial output (115200 baud)
```

**macOS Note:** Upload speed must be 115200 (configured in platformio.ini). Higher speeds fail on Apple Silicon.

### Waveshare ESP32-S3-LCD-1.47

```bash
pio run -e waveshare_s3_147              # Build
pio run -e waveshare_s3_147 -t upload    # Flash firmware
pio device monitor -e waveshare_s3_147   # Monitor serial output (115200 baud)
```

Uses USB-CDC (921600 upload speed). Board auto-detected on macOS.

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
| `src/main.cpp` | Detection engine, WiFi/BLE scanning, shared across boards |
| `src/display_handler_28.cpp/.h` | Touchscreen UI for ESP32-2432S028R |
| `src/display_handler_147.cpp/.h` | Button-nav UI for Waveshare ESP32-S3-LCD-1.47 |

### Hardware: ESP32-2432S028R (2.8" CYD)

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

### Hardware: Waveshare ESP32-S3-LCD-1.47

**Display:**
- ST7789 172x320, landscape rotation (320x172)
- SPI: MOSI=45, SCLK=40, CS=42, DC=41, RST=39, BL=48
- Backlight via LEDC PWM channel 0

**Navigation:**
- BOOT button (GPIO 0) only - no touchscreen
- Short press: cycle pages / adjust values
- Long press: select setting for editing / exit to HOME

**RGB LED:**
- WS2812B addressable LED on GPIO 38
- Controlled via FastLED library

**SD Card (SDMMC):**
- 1-bit SDMMC mode (CMD=15, CLK=14, D0=16)
- Stores `/flockyou_detections.csv` (log) and `/settings.txt` (persistent config)

### Key Implementation Details

**Touch Calibration:**
- 4-point guided calibration (TL→TR→BL→BR)
- Validates against known good ranges (200-4000, span >2000)
- Persists to SD card, loads automatically on boot
- RAW_Y maps to Screen X, RAW_X maps to Screen Y (axes swapped)

**LED State Machine (both boards):**
- Scanning: Solid green
- Detection: Red flashing, rate based on RSSI (50-400ms interval)
- Alert: Solid orange

**CYD Display Pages (4-button touchscreen nav: HOME, LIST, STATS, CONFIG):**
- PAGE_MAIN: Header with CH/BLE indicator, latest detection panel, LED status row
- PAGE_LIST: Scrollable detection history with color-coded indicators and signal bars
- PAGE_STATS: Detection counts, percentages, distribution bars, CLEAR button
- PAGE_SETTINGS: Custom layout (no header), SD status, Display/Sound/LED controls with +/-/MAX, CALIBRATE button
- PAGE_CALIBRATE: Full-screen 4-point calibration with CANCEL/SAVE buttons

**Waveshare Display Pages (BOOT button nav: HOME, LIST, STATS, CONFIG):**
- PAGE_MAIN: Header, 3 stat boxes (WiFi/BLE/Threat), latest detection panel, footer
- PAGE_LIST: Scrollable detection list with auto-scroll, vendor lookup, signal bars
- PAGE_STATS: Count boxes with %, closest/last threat, unique MACs vs total, rate, top channel
- PAGE_SETTINGS: Display/LED brightness adjust, EXIT to HOME; short=cycle, long=edit/exit

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
- **FastLED@^3.6.0** - WS2812 RGB LED (Waveshare only)

## Development Notes

### Adding New Detection Patterns
Edit arrays in `src/main.cpp`:
- `wifi_ssid_patterns[]` - SSID substring matches
- `mac_prefixes[]` - First 3 octets of MAC address
- `device_name_patterns[]` - BLE advertised names

### Modifying UI Layout

**CYD** - Edit `src/display_handler_28.cpp`:
- `drawMainPage()`, `drawListPage()`, `drawStatsPage()`, `drawSettingsPage()`, `drawCalibrationPage()`
- Touch zones cleared each redraw: `addTouchZone(x1, y1, x2, y2, callback, "label")`

**Waveshare** - Edit `src/display_handler_147.cpp`:
- `drawHeader()`, `drawStatsPanel()`, `drawLatestDetection()` - HOME page components
- `drawDetectionList()` - LIST page
- `drawFullStatsList()` - STATS page
- `drawSettingsPage()` - CONFIG page
- `handleButton()` - All navigation logic (short/long press)

### Serial Output
JSON objects at 115200 baud, one per line. Key fields:
- `protocol`: "wifi" or "bluetooth_le"
- `detection_method`: "probe_request", "beacon", "mac_prefix", "device_name"
- `ssid`, `mac_address`, `rssi`, `channel`, `threat_score`
