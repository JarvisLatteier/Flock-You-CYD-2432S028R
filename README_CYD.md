# Flock You - ESP32-2432S028R Edition

This fork is specifically adapted for the **ESP32-2432S028R** "Cheap Yellow Display" - a 2.8" touchscreen ESP32 development board. It provides a complete touchscreen interface with RGB LED alerts, SD card logging, and touch calibration persistence.

## Hardware Specifications

### ESP32-2432S028R Board
| Component | Specification |
|-----------|---------------|
| **MCU** | ESP32-WROOM-32 |
| **Display** | 2.8" ILI9341 TFT LCD, 320x240 pixels |
| **Touch** | XPT2046 resistive touchscreen |
| **Flash** | 4MB |
| **USB** | CH340C USB-to-Serial |
| **Power** | 5V via USB-C or Micro-USB |

### Key Hardware Notes

**Display SPI Bus (VSPI):**
- SCK: GPIO 14
- MOSI: GPIO 13
- MISO: GPIO 12
- CS: GPIO 15
- DC: GPIO 2
- RST: GPIO 4
- Backlight: GPIO 27 + GPIO 21 (dual backlight, active HIGH)

**Touch Controller (Separate HSPI Bus):**
The XPT2046 touch controller is on a **separate HSPI bus** - this is critical and differs from many other CYD boards:
- CS: GPIO 33
- CLK: GPIO 25
- MOSI: GPIO 32
- MISO: GPIO 39
- IRQ: GPIO 36 (input-only, no internal pullup)

**RGB LED (Active LOW):**
- Red: GPIO 4
- Green: GPIO 16
- Blue: GPIO 17
- Note: HIGH = LED off, LOW = LED on

**SD Card:**
- CS: GPIO 5
- Shares VSPI bus with display

**Available for Extensions:**
- GPIO 22: Buzzer output (active HIGH)

## Features

### Touchscreen Interface
- **5-page navigation**: HOME, LIST, STAT, CONF, CAL
- **Main dashboard**: Real-time detection stats, latest detection panel, LED status key
- **Detection list**: Scrollable list with color-coded threat indicators
- **Statistics page**: Detection counts, percentages, distribution bars, CLEAR button
- **Settings page**: Display brightness, RGB LED brightness, auto-brightness toggle, LED alert toggle, SD card status
- **Calibration page**: 4-point guided touch calibration with validation

### RGB LED Alert System
The RGB LED provides visual status at a glance:

| State | Color | Behavior |
|-------|-------|----------|
| Scanning | Green (50%) | Solid - system is actively scanning |
| Detection | Red | Flashing - speed based on signal strength |
| Alert | Orange | Solid - recent detection, signal lost |

Flash rate scales with RSSI: stronger signals flash faster (50ms interval) while weak signals flash slower (400ms interval).

### Touch Calibration System
First boot triggers a guided 4-point calibration:
1. Tap targets at each corner (TL → TR → BL → BR)
2. System validates against known good ranges (200-4000, span >2000)
3. Invalid calibrations are rejected with restart prompt
4. Valid calibrations save to `/touch_cal.txt` on SD card
5. Subsequent boots load calibration automatically

Manual recalibration available via CAL button in footer.

### SD Card Features
- **Detection logging**: All detections logged to `/flockyou_detections.csv`
- **Calibration persistence**: Touch calibration saved to `/touch_cal.txt`
- **Format**: CSV with timestamp, SSID, MAC, RSSI, type

## Installation

### Prerequisites
- [PlatformIO](https://platformio.org/) CLI or IDE
- USB cable (Micro-USB or USB-C depending on board variant)
- **macOS M1/M2/M3 Note**: Upload speed must be 115200 baud (921600 fails on Apple Silicon)

### Build & Flash

```bash
# Clone the repository
git clone https://github.com/JarvisLatteier/Flock-You-CYD-2432S028R.git
cd Flock-You-CYD-2432S028R

# Build firmware
pio run -e esp32_cyd_28

# Flash to device
pio run -e esp32_cyd_28 -t upload

# Monitor serial output (115200 baud)
pio device monitor -e esp32_cyd_28
```

### Alternative: Headless Build
If you don't need the display interface (e.g., using the web dashboard only):

```bash
pio run -e esp32_cyd_28_headless -t upload
```

This excludes all display code but retains RGB LED alerts and serial JSON output.

## Build Environments

| Environment | Description |
|-------------|-------------|
| `esp32_cyd_28` | Full build with touchscreen UI |
| `esp32_cyd_28_headless` | No display, RGB LED + serial only |

## Usage

### First Boot
1. Insert SD card (optional but recommended for calibration persistence)
2. Power on via USB
3. Complete 4-point touch calibration if prompted
4. System begins scanning automatically

### Navigation
Touch the footer buttons to navigate:
- **HOME**: Main dashboard with stats and latest detection
- **LIST**: Scrollable detection history
- **STAT**: Statistics and CLEAR button
- **CONF**: Brightness controls, LED toggle, SD status
- **CAL**: Recalibrate touchscreen

### Settings
- **Display brightness**: +/- buttons (10% increments, 10-100%)
- **Auto brightness**: Toggle to use LDR sensor (GPIO 34)
- **RGB LED brightness**: +/- buttons for alert LED intensity
- **LED alerts**: Toggle RGB LED on/off

### Understanding Detections

**Color coding in list view:**
- Red left bar: Flock/Penguin/Pigvision threat
- Purple left bar: BLE device
- Blue left bar: WiFi device

**Signal strength bars:**
- 4 bars: Excellent (> -50 dBm)
- 3 bars: Good (-50 to -60 dBm)
- 2 bars: Fair (-60 to -70 dBm)
- 1 bar: Weak (-70 to -80 dBm)
- 0 bars: Very weak (< -80 dBm)

## Pin Reference

### Complete GPIO Map

| GPIO | Function | Notes |
|------|----------|-------|
| 2 | TFT DC | Data/Command |
| 4 | TFT RST / RGB Red | Directly driving red LED  |
| 5 | SD Card CS | |
| 12 | TFT MISO | VSPI |
| 13 | TFT MOSI | VSPI |
| 14 | TFT SCK | VSPI |
| 15 | TFT CS | |
| 16 | RGB Green | Active LOW |
| 17 | RGB Blue | Active LOW |
| 21 | TFT Backlight 2 | PWM, paired with GPIO 27 |
| 22 | Buzzer | Optional, active HIGH |
| 25 | Touch CLK | HSPI |
| 27 | TFT Backlight 1 | PWM |
| 32 | Touch MOSI | HSPI |
| 33 | Touch CS | |
| 34 | LDR | Analog input for auto-brightness |
| 36 | Touch IRQ | Input-only, no pullup |
| 39 | Touch MISO | Input-only |

### Why Separate Touch SPI?
The ESP32-2432S028R uses a **separate HSPI bus for touch** (unlike some CYD variants that share VSPI). This is because:
- GPIO 36/39 are input-only pins used for touch MISO/IRQ
- Sharing would require complex CS toggling and cause display artifacts
- Separate buses allow simultaneous display updates and touch reads

## Troubleshooting

### Display Issues
| Problem | Solution |
|---------|----------|
| Blank screen | Check USB power (needs 500mA+) |
| Wrong colors | Verify `TFT_RGB_ORDER=TFT_BGR` in build flags |
| Dim display | Both backlight pins (27, 21) must be driven |

### Touch Issues
| Problem | Solution |
|---------|----------|
| Touch not responding | Check HSPI wiring (separate from display) |
| Touch inverted/offset | Run calibration (CAL button) |
| Calibration fails | Tap precisely on crosshair centers |

### Upload Issues
| Problem | Solution |
|---------|----------|
| Upload fails on Mac | Ensure `upload_speed = 115200` |
| Port not found | Install CH340 driver if needed |
| Timeout errors | Hold BOOT button while uploading |

### SD Card Issues
| Problem | Solution |
|---------|----------|
| SD not detected | Format as FAT32, check GPIO 5 CS |
| Calibration not saving | Ensure SD card is writable |

## Web Dashboard

The Flask web dashboard (`api/`) works with this board via USB serial:

```bash
cd api
python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt
python flockyou.py
```

Access at `http://localhost:5000` for:
- Real-time detection display
- GPS integration
- CSV/KML export
- Detection history

## Differences from Upstream

This fork differs from the main Flock-You project:

| Feature | Upstream | This Fork |
|---------|----------|-----------|
| Target board | Xiao ESP32 S3 / 3.5" CYD | ESP32-2432S028R (2.8") |
| Display | ILI9488 480x320 | ILI9341 320x240 |
| Touch bus | Shared VSPI | Separate HSPI |
| Touch calibration | Hardcoded | 4-point with SD persistence |
| RGB LED | N/A | Full PWM control with state machine |
| Backlight | Single pin | Dual pin PWM |

## Legal Disclaimer

This tool is for educational and research purposes only. Users must comply with local laws regarding wireless monitoring and privacy. Detection of surveillance devices should only be performed where legally permitted.

## Credits

- Original [Flock-You](https://github.com/fuzzzy-kyle/flock-you-cyd) project
- Detection patterns from [DeFlock](https://deflock.me) crowdsourced database
- Hardware analysis and adaptation for ESP32-2432S028R
- Inspired by the work of fuzzzy-kyle, [Flock You - CYD](https://github.com/fuzzzy-kyle/flock-you-cyd)
