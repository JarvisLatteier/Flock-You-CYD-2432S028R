# Flock You - Multi-Board Edition

Detects Flock Safety surveillance cameras and similar devices using WiFi and BLE scanning. Supports two ESP32 boards with full display UIs, RGB LED alerts, SD card logging, and OUI vendor lookup.

## Supported Boards

### ESP32-2432S028R (2.8" CYD)
- "2432S028" in the board name (NOT 2432S035 or 3248S035)
- 2.8" ILI9341 display, 320x240, resistive touchscreen
- Yellow PCB with USB connector on the short edge
- Sold by DIYMall, board may have a GUiTiON logo

### Waveshare ESP32-S3-LCD-1.47
- 1.47" ST7789 display, 172x320 (used in landscape: 320x172)
- BOOT button navigation (no touchscreen)
- WS2812B addressable RGB LED on GPIO 38
- SDMMC card slot, USB-C

## Hardware Specifications

### ESP32-2432S028R (CYD)
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

**Speaker (SPEAK P4 Connector):**
- GPIO 26: Speaker output (PWM via LEDC channel 5)

**Available for Extensions:**
- GPIO 22: Buzzer output (active HIGH)

### Waveshare ESP32-S3-LCD-1.47
| Component | Specification |
|-----------|---------------|
| **MCU** | ESP32-S3 (QFN56) |
| **Display** | 1.47" ST7789 TFT LCD, 172x320 pixels |
| **Input** | BOOT button (GPIO 0) |
| **RGB LED** | WS2812B addressable on GPIO 38 |
| **SD Card** | SDMMC 1-bit (CLK=14, CMD=15, D0=16) |
| **Flash** | 8MB QD |
| **USB** | USB-C (USB-Serial/JTAG) |

### Key Waveshare Hardware Notes

**Display SPI Bus:**
- MOSI: GPIO 45
- SCLK: GPIO 40
- CS: GPIO 42
- DC: GPIO 41
- RST: GPIO 39
- Backlight: GPIO 48 (PWM via LEDC channel 0)

**Navigation:**
- BOOT button (GPIO 0) is the only input
- Short press: cycle pages or adjust values
- Long press: select setting for editing or exit to HOME

**SD Card (SDMMC):**
- Requires explicit `SD_MMC.setPins(14, 15, 16)` before `begin()` (ESP32-S3 has no default SDMMC pins)
- 1-bit SDMMC mode

## Features

### Display Interface

**CYD (Touchscreen):**
- **4-button touchscreen navigation**: HOME, LIST, STATS, CONFIG
- **Main dashboard**: Real-time detection stats, channel/BLE indicator, latest detection panel
- **Detection list**: Scrollable list with color-coded threat indicators and signal strength
- **Statistics page**: Detection counts, percentages, distribution bars, CLEAR button
- **Config page**: Display/Sound/LED brightness controls with +/-/MAX buttons, SD card status, CALIBRATE button
- **Calibration page**: Full-screen 4-point guided touch calibration with validation

**Waveshare (BOOT Button):**
- **Single-button navigation**: BOOT button cycles HOME → LIST → STATS → CONFIG
- **HOME page**: Stats panel (WiFi/BLE/Threat counts), latest detection with 2-second flash animation on new detections
- **LIST page**: Auto-scrolling detection list with hit counts, time since last seen, vendor names, signal bars
- **STATS page**: Two-column layout — summary stats (unique MACs vs total, detection rate, top channel) and persistent threat list with per-device RSSI and time ago
- **CONFIG page**: Display/LED brightness controls, SD card file status (log size, settings, OUI database), short press cycles options, long press edits or exits to HOME

### Speaker/Sound Support (CYD only)
- Boot tone on startup (3-note ascending sequence)
- Sound toggle and volume control (0-100%)
- Speaker connected to GPIO 26 (SPEAK P4 connector)

### Detection Engine (both boards)

**WiFi Frame Capture:**
- Probe requests (devices scanning for networks)
- Probe responses (APs responding to scans)
- Beacons (APs advertising their network)

**BLE Scanning:**
- Device name pattern matching
- MAC prefix matching

**TrackedDevice System:**
- Per-device metadata: RSSI min/max/avg, hit count, probe intervals, channel, timing
- Detection TTL (5 min): devices are re-reported after 5 minutes of silence
- Channel memory: sticky channel (5s) after detection + detection-weighted dwell time
- Enriched JSON serial output with signal trending (`stable`, `moderate`, `moving`)

**Buffered SD Logging (CYD):**
- Async batch writes every 5 seconds (no SD I/O in detection pipeline)
- Session tracking with random session ID per boot
- Enriched CSV: timestamp, session, SSID, MAC, vendor, RSSI, type, RSSI min/max/avg, hits, probe interval, channel

### RGB LED Alert System (both boards)
The RGB LED provides visual status at a glance:

| State | Color | Behavior |
|-------|-------|----------|
| Scanning | Green | Solid - system is actively scanning |
| Detection | Red | Flashing - speed based on signal strength |
| Alert | Orange | Solid - signal lost after detection |

Flash rate scales with RSSI: stronger signals flash faster (50ms interval) while weak signals flash slower (400ms interval).

**CYD**: Alert returns to scanning after 15 seconds.
**Waveshare**: Alert is persistent until reboot. New detections while orange return to red flashing.

### Touch Calibration System
First boot triggers a guided 4-point calibration:
1. Full-screen calibration with crosshair targets at each corner
2. Tap targets in order: TL → TR → BL → BR
3. System validates against known good ranges (200-4000, span >2000)
4. Invalid calibrations are rejected with restart prompt
5. Valid calibrations save to `/touch_cal.txt` on SD card
6. Subsequent boots load calibration automatically

Manual recalibration available via CALIBRATE button on CONFIG page.

### SD Card Setup

**Format:** FAT32 (required for both boards)

**Files to copy from this repo:**
| File | Purpose | Required? |
|------|---------|-----------|
| `oui.csv` | MAC vendor lookup database (37K entries) | Optional but recommended |

**Files created automatically:**
| File | Board | Purpose |
|------|-------|---------|
| `touch_cal.txt` | CYD | Touch calibration data (4 values) |
| `settings.txt` | Both | Persistent settings (brightness, LED) |
| `flockyou_detections.csv` | Both | Detection log (CYD: enriched with session, RSSI stats, probe intervals, channel) |

**Quick setup:**
1. Format SD card as FAT32
2. Copy `oui.csv` from repo root to SD card root
3. Insert SD card into board
4. CYD: first boot will prompt for touch calibration (saved to SD)
5. Waveshare: settings and detections log automatically

### OUI Vendor Lookup
The system identifies device manufacturers from MAC addresses:

**Embedded lookup (instant, no SD required):**
- Flock Safety / surveillance device OUIs
- Common cameras: Hikvision, Dahua, Amcrest
- Common IoT: Apple, Espressif, Raspberry Pi

**SD card lookup (comprehensive):**
- Requires `oui.csv` on SD card root
- 37,000+ IEEE OUI entries
- Binary search (~10ms per lookup)

**Where vendor names appear:**
- Main page: Latest detection panel
- List page: Replaces MAC when vendor is known
- CSV log: Vendor column in detection records

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

# --- ESP32-2432S028R (CYD) ---
pio run -e esp32_cyd_28 -t upload       # Build and flash
pio device monitor -e esp32_cyd_28      # Serial monitor (115200 baud)

# --- Waveshare ESP32-S3-LCD-1.47 ---
pio run -e waveshare_s3_147 -t upload   # Build and flash
pio device monitor -e waveshare_s3_147  # Serial monitor (115200 baud)
```

### Alternative: Headless Build (CYD only)
If you don't need the display interface (e.g., using the web dashboard only):

```bash
pio run -e esp32_cyd_28_headless -t upload
```

This excludes all display code but retains RGB LED alerts and serial JSON output.

## Build Environments

| Environment | Board | Description |
|-------------|-------|-------------|
| `esp32_cyd_28` | CYD | Full build with touchscreen UI |
| `esp32_cyd_28_headless` | CYD | No display, RGB LED + serial only |
| `waveshare_s3_147` | Waveshare | Full build with BOOT button UI |

**macOS Notes:**
- CYD upload speed must be 115200 baud (higher speeds fail on Apple Silicon)
- Waveshare uses USB-CDC at 921600 baud, auto-detected

## Usage

### First Boot
1. Insert SD card (optional but recommended for calibration persistence)
2. Power on via USB
3. Complete 4-point touch calibration if prompted
4. System begins scanning automatically

### Navigation
Touch the footer buttons to navigate:
- **HOME**: Main dashboard with channel/BLE indicator and latest detection
- **LIST**: Scrollable detection history with signal strength bars
- **STATS**: Detection statistics and CLEAR button
- **CONFIG**: Brightness/Sound/LED controls, SD status, CALIBRATE button

### Config Page Settings
- **Display brightness**: AUTO/MAN toggle, +/-/MAX buttons (10% increments)
- **Auto brightness**: Uses LDR sensor on GPIO 34
- **Sound**: ON/OFF toggle, volume +/-/MAX buttons
- **RGB LED**: ON/OFF toggle, brightness +/-/MAX buttons
- **SD Card status**: Shows SD, calibration file, OUI file, and log count
- **CALIBRATE button**: Launch touch calibration

All settings are automatically saved to SD card and restored on boot.

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
| 26 | Speaker | PWM via LEDC channel 5 |
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
| Target boards | Xiao ESP32 S3 / 3.5" CYD | ESP32-2432S028R (2.8") + Waveshare ESP32-S3-LCD-1.47 |
| Displays | ILI9488 480x320 | ILI9341 320x240 (CYD), ST7789 172x320 (Waveshare) |
| Touch | Shared VSPI | Separate HSPI (CYD), BOOT button (Waveshare) |
| Touch calibration | Hardcoded | 4-point with SD persistence (CYD) |
| RGB LED | N/A | PWM state machine (CYD), WS2812B (Waveshare) |
| Threat tracking | Basic | Persistent threat list, hit counts, closest RSSI |
| SD card | Basic | Detection logging, settings persistence, OUI lookup |

## Legal Disclaimer

This tool is for educational and research purposes only. Users must comply with local laws regarding wireless monitoring and privacy. Detection of surveillance devices should only be performed where legally permitted.

## Credits

- Original [Flock-You](https://github.com/fuzzzy-kyle/flock-you-cyd) project
- Detection patterns from [DeFlock](https://deflock.me) crowdsourced database
- Hardware analysis and adaptation for ESP32-2432S028R
- Inspired by the work of fuzzzy-kyle, [Flock You - CYD](https://github.com/fuzzzy-kyle/flock-you-cyd)
- 802.11 frame type reference by Jeremy Sharp: [802.11 Frame Types and Formats](https://howiwifi.com/2020/07/13/802-11-frame-types-and-formats/)
