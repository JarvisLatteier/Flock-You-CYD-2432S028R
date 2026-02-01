# Flock You - CYD (Cheap Yellow Display) Edition

This version of Flock You has been adapted to work with the ESP32-2432S035C "Cheap Yellow Display" - a 3.5" touchscreen ESP32 development board.

## Hardware Requirements

### CYD Board Specifications
- **Board**: ESP32-2432S035C (3.5" version)
- **Display**: 480x320 ILI9488 TFT LCD
- **Touch**: XPT2046 resistive touchscreen
- **Processor**: ESP32-WROOM-32
- **Flash**: 4MB
- **RAM**: 520KB SRAM

### Optional Hardware
- **External Buzzer**: Connect to GPIO22 for audio alerts (optional)
- **MicroSD Card**: For data logging (optional)

## Features

### Visual Interface
- **Main Dashboard**: Real-time detection display with statistics
- **Detection List**: Scrollable list of all detected devices
- **Statistics Page**: Visual charts and detection analytics
- **Settings Page**: Configure scanning parameters
- **About Page**: System information

### Touch Controls
- Navigation buttons at bottom of screen
- Touch-enabled settings adjustments
- Clear detection history with one tap
- Page switching via touch zones

### Detection Display
- Color-coded alerts (Red for Flock, Yellow for suspicious, Green for safe)
- Signal strength indicators
- Real-time channel hopping display
- Detection counters and statistics
- Progress bars for detection distribution

## Installation

### Prerequisites
1. Install PlatformIO:
```bash
# Install via pip
pip install platformio

# Or install PlatformIO IDE for VSCode
# https://platformio.org/install/ide?install=vscode
```

2. Install USB drivers for ESP32 (if needed):
- Windows: CH340 driver
- Linux/Mac: Usually works out of the box

### Compiling and Uploading

1. Clone the repository:
```bash
git clone [repository-url]
cd flock-you-cyd
```

2. Build for CYD:
```bash
# Build the firmware
platformio run -e esp32_cyd_35

# Upload to device (connect via USB first)
platformio run -e esp32_cyd_35 --target upload

# Monitor serial output
platformio device monitor
```

3. Alternative method using Arduino IDE:
- Install ESP32 board support
- Install required libraries: TFT_eSPI, XPT2046_Touchscreen, NimBLE-Arduino, ArduinoJson
- Configure TFT_eSPI for ILI9488 driver
- Compile and upload

## Usage

### Initial Setup
1. Power on the device via USB-C
2. The display will show a splash screen
3. System will automatically start scanning

### Navigation
- **MAIN**: Overview dashboard with latest detections
- **LIST**: Scrollable list of all detections
- **STATS**: Detection statistics and graphs
- **SET**: Configuration options
- **CLR**: Clear all detection history

### Understanding Detections

#### Color Codes
- **Red**: Confirmed Flock Safety device
- **Yellow**: Suspicious surveillance device
- **Cyan**: BLE advertisement
- **Green**: Strong signal
- **White**: General information

#### Detection Information
- **SSID**: Network name (for WiFi devices)
- **MAC**: Hardware address
- **RSSI**: Signal strength in dBm
- **Type**: WiFi or BLE
- **Channel**: Current WiFi channel being scanned

### Configuration Options
- **Audio Alerts**: Enable/disable buzzer (if connected)
- **Brightness**: Adjust display brightness
- **Scan Speed**: Fast/Medium/Slow scanning
- **Auto Clear**: Automatically clear old detections
- **Rotation**: Change display orientation

## Troubleshooting

### Display Issues
- If display is blank: Check USB power supply (needs 5V, 500mA minimum)
- If colors are inverted: Change ILI9488_DRIVER to ST7796_DRIVER in platformio.ini
- If touch is not working: Recalibrate touch in display_handler.h

### Compilation Errors
- Missing libraries: Run `platformio lib install` to get dependencies
- Board not found: Install ESP32 platform `platformio platform install espressif32`
- Upload fails: Check USB connection and correct port selection

### Detection Issues
- No detections: Ensure WiFi and BLE are not disabled
- False positives: Adjust detection patterns in main.cpp
- Missing detections: Increase scan duration or decrease channel hop interval

## Pin Connections

### Display Pins (Pre-connected on CYD)
- MOSI: GPIO 23
- MISO: GPIO 19
- SCLK: GPIO 18
- CS: GPIO 15
- DC: GPIO 2
- RST: GPIO 4
- BL: GPIO 21

### Touch Pins (Pre-connected on CYD)
- T_CS: GPIO 33
- T_IRQ: GPIO 36
- T_CLK: Shared with display SCLK
- T_DIN: Shared with display MOSI
- T_DO: GPIO 19

### Available GPIO for Extensions
- GPIO 22: Buzzer output
- GPIO 27: Available
- GPIO 35: Input only (no pull-up)

## Performance Notes

- Display updates are throttled to 1Hz to maintain scanning performance
- Touch debouncing implemented with 200ms delay
- Channel hopping continues during display updates
- BLE scanning runs in parallel with WiFi monitoring

## Legal Disclaimer

This tool is for educational and research purposes only. Users must comply with local laws regarding wireless monitoring and privacy. The detection of surveillance devices should only be done where legally permitted.

## Credits

Based on the original Flock You project, adapted for ESP32-2432S035C hardware with enhanced visual interface and touch controls.