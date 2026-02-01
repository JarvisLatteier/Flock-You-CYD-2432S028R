# Changelog

All notable changes to this fork are documented here.

This fork targets the **ESP32-2432S028R** (2.8" CYD) specifically.

## [1.0.0] - 2025-02-01

### Added
- **Touchscreen UI** for ESP32-2432S028R (ILI9341 320x240)
  - 5-page navigation: HOME, LIST, STAT, CONF, CAL
  - Main dashboard with stats, latest detection, LED status key
  - Scrollable detection list with color-coded threat indicators
  - Statistics page with distribution bars and CLEAR button
  - Settings page with brightness controls and SD status

- **Touch Calibration System**
  - 4-point guided calibration (TL→TR→BL→BR)
  - Validation against known good ranges
  - Persistence to SD card (`/touch_cal.txt`)
  - Auto-load on boot, manual recalibration via CAL button

- **RGB LED Alert System**
  - Scanning state: solid green at 50%
  - Detection state: red flashing (rate scales with signal strength)
  - Alert state: solid orange (recent detection, signal lost)
  - PWM brightness control via settings page
  - Toggle to disable LED alerts

- **SD Card Integration**
  - Detection logging to `/flockyou_detections.csv`
  - Touch calibration persistence
  - Status display in settings page

- **OUI Vendor Lookup**
  - Embedded lookup for surveillance OUIs (Flock Safety, Hikvision, Dahua, etc.)
  - SD card lookup via `oui.csv` for 37,000+ IEEE OUI entries
  - Vendor names shown in detection list and main panel
  - Vendor column added to CSV log output

- **Display Handler** (`display_handler_28.cpp/h`)
  - Separate HSPI bus for XPT2046 touch controller
  - Dual backlight PWM (GPIO 27 + 21)
  - Auto-brightness via LDR on GPIO 34
  - BGR color order configuration

- **Build Environments**
  - `esp32_cyd_28`: Full build with touchscreen UI
  - `esp32_cyd_28_headless`: Serial + RGB LED only

### Changed
- Upload speed set to 115200 for macOS compatibility
- Channel hopping interval increased to 1000ms
- Main.cpp refactored for display integration

### Removed
- Unused display handlers from upstream (display_handler.cpp, display_handler_simple.cpp)
- Test files not applicable to this board

### Hardware Notes
This fork is specifically for ESP32-2432S028R. Key differences from other CYD boards:
- Touch on separate HSPI bus (GPIO 25/32/39/33/36)
- GPIO 36/39 are input-only (no internal pullup)
- Dual backlight pins (27 + 21)
- RGB LED active LOW on GPIO 4/16/17

## Credits
- Original [Flock-You](https://github.com/fuzzzy-kyle/flock-you-cyd) by fuzzzy-kyle
- Detection patterns from [DeFlock](https://deflock.me)
