#ifdef CYD_DISPLAY

#include "display_handler.h"
#include <Arduino.h>

// Global instance
DisplayHandler display;

// Touch callback implementations
void onMainButtonPress() { display.setPage(DisplayHandler::PAGE_MAIN); }
void onListButtonPress() { display.setPage(DisplayHandler::PAGE_LIST); }
void onStatsButtonPress() { display.setPage(DisplayHandler::PAGE_STATS); }
void onSettingsButtonPress() { display.setPage(DisplayHandler::PAGE_SETTINGS); }
void onClearButtonPress() { display.clearDetections(); }
void onBuzzerToggle() { /* Implement buzzer toggle */ }

DisplayHandler::DisplayHandler() {
    bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, BUS_SCK, BUS_MOSI, BUS_MISO, VSPI);
    gfx = new Arduino_ST7796(bus, TFT_RST, TFT_ROTATION);

    displayActive = true;
    needsRedraw = true;
    lastUpdate = 0;
    currentPage = PAGE_MAIN;
    brightness = 255;
    totalDetections = 0;
    flockDetections = 0;
    bleDetections = 0;
    lastTouchTime = 0;
    touchDebounce = false;
    currentChannel = 1;
    lastSSID = "";
    lastRSSI = 0;
    sdCardAvailable = false;
    currentLogFile = "";
    isFlashing = false;
    flashStartTime = 0;
    flashState = false;
}

bool DisplayHandler::begin() {
    // Backlight ON
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);

    // CS idle high
    pinMode(TFT_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);
    pinMode(TOUCH_CS, OUTPUT);
    digitalWrite(TOUCH_CS, HIGH);
    pinMode(TOUCH_IRQ, INPUT);

    // TFT hard reset
    pinMode(TFT_RST, OUTPUT);
    digitalWrite(TFT_RST, LOW);
    delay(20);
    digitalWrite(TFT_RST, HIGH);
    delay(120);

    // Initialize display
    if (!gfx->begin()) {
        Serial.println("gfx->begin FAILED");
        return false;
    }

    gfx->fillScreen(BG_COLOR);

    // Initialize touch
    touchBegin();

    // Show splash screen
    gfx->setTextColor(TEXT_COLOR);
    gfx->setTextSize(2);
    gfx->setCursor(60, gfx->height()/2 - 40);
    gfx->print("WHAT THE FLOCK");

    gfx->setTextSize(2);
    gfx->setTextColor(INFO_COLOR);
    gfx->setCursor(50, gfx->height()/2);
    gfx->print("Surveillance Detection");

    gfx->setTextSize(1);
    gfx->setTextColor(WARNING_COLOR);
    gfx->setCursor(gfx->width()/2 - 60, gfx->height()/2 + 40);
    gfx->print("ST7796 Edition v1.0");

    delay(2000);

    // Load or run calibration
    if (!loadCal()) {
        Serial.println("No saved calibration -> running calibration...");
        runCalibration();
    } else {
        Serial.println("Loaded calibration from NVS.");
    }

    // Initialize SD card for logging
    if (initSDCard()) {
        Serial.println("SD card initialized successfully");
        currentLogFile = createLogFileName();
        Serial.print("Logging to: ");
        Serial.println(currentLogFile);
    } else {
        Serial.println("SD card initialization failed - logging disabled");
    }

    clear();
    return true;
}

void DisplayHandler::update() {
    uint32_t now = millis();

    // Handle flash alert (10 seconds of rapid flashing)
    if (isFlashing) {
        uint32_t flashDuration = now - flashStartTime;

        if (flashDuration > 10000) {
            // Flash duration over, stop flashing
            isFlashing = false;
            digitalWrite(TFT_BL, HIGH); // Ensure backlight is on
            needsRedraw = true;
        } else {
            // Toggle flash state every 150ms for rapid flashing
            static uint32_t lastFlashToggle = 0;
            if (now - lastFlashToggle > 150) {
                flashState = !flashState;
                digitalWrite(TFT_BL, flashState ? HIGH : LOW);
                lastFlashToggle = now;
            }
        }
    }

    // Check for touch input
    int16_t sx, sy;
    if (touchReadScreen(sx, sy) && !touchDebounce) {
        handleTouch();
        touchDebounce = true;
        lastTouchTime = now;
    }

    // Reset touch debounce after 200ms
    if (touchDebounce && (now - lastTouchTime > 200)) {
        touchDebounce = false;
    }

    // Update display if needed (every second)
    if (needsRedraw || (now - lastUpdate > 1000)) {
        drawMainPage(); // Simplified - just draw main page
        needsRedraw = false;
        lastUpdate = now;
    }
}

void DisplayHandler::clear() {
    gfx->fillScreen(BG_COLOR);
    needsRedraw = true;
}

void DisplayHandler::drawMainPage() {
    // Don't clear entire screen every time - causes flicker
    // Only clear areas we're updating

    // Title bar - cyberpunk style with larger text
    gfx->fillRect(0, 0, gfx->width(), 40, HEADER_COLOR);
    gfx->drawFastHLine(0, 40, gfx->width(), ACCENT_COLOR);  // Neon line
    gfx->setTextSize(3);
    gfx->setTextColor(ACCENT_COLOR);
    // Faux bold effect - print twice with 1px offset
    gfx->setCursor(5, 8);
    gfx->print(">WHAT THE FLOCK");
    gfx->setCursor(6, 8);
    gfx->print(">WHAT THE FLOCK");

    // Detection counts area - centered
    gfx->fillRect(0, 40, gfx->width(), 90, BG_COLOR);
    gfx->setTextSize(2);
    gfx->setTextColor(WARNING_COLOR);
    // Center "CAMERA DETECTIONS: X"
    String detectionsText = "CAMERA DETECTIONS: " + String(totalDetections);
    int textWidth = detectionsText.length() * 12;  // 12 pixels per char at size 2
    gfx->setCursor((gfx->width() - textWidth) / 2, 50);
    gfx->print(detectionsText);

    // WiFi and BLE counts on same line
    gfx->setTextColor(SUCCESS_COLOR);
    gfx->setCursor(10, 70);
    gfx->print("WiFi: ");
    gfx->print(flockDetections);

    gfx->setTextColor(ACCENT_COLOR);
    gfx->setCursor(gfx->width() / 2, 70);
    gfx->print("BLE: ");
    gfx->print(bleDetections);

    // Latest detection area - larger font
    gfx->fillRect(0, 95, gfx->width(), 130, BG_COLOR);
    if (!detections.empty()) {
        Detection& latest = detections.back();

        gfx->setTextSize(2);
        gfx->setTextColor(WARNING_COLOR);
        // Center "LATEST DETECTION:"
        int latestTextWidth = 18 * 12;  // 18 chars * 12 pixels per char at size 2
        gfx->setCursor((gfx->width() - latestTextWidth) / 2, 100);
        gfx->print("LATEST DETECTION:");

        gfx->setTextSize(2);
        gfx->setTextColor(TEXT_COLOR);
        gfx->setCursor(10, 120);
        gfx->print("SSID:");
        gfx->setTextSize(2);
        gfx->setCursor(90, 120);
        gfx->print(latest.ssid.substring(0, 20));

        gfx->setTextSize(2);
        gfx->setCursor(10, 145);
        gfx->print("MAC:");
        gfx->setTextSize(2);
        gfx->setCursor(75, 145);
        gfx->print(latest.mac.substring(0, 17));

        gfx->setTextSize(2);
        gfx->setCursor(10, 170);
        gfx->print("RSSI:");
        gfx->setTextSize(2);
        gfx->setCursor(90, 170);
        gfx->print(latest.rssi);
        gfx->print(" dBm");

        gfx->setCursor(10, 195);
        gfx->print("Time: ");
        gfx->print(latest.timestamp / 1000);
        gfx->print("s");
    } else {
        gfx->setTextSize(2);
        gfx->setTextColor(WARNING_COLOR);
        gfx->setCursor(10, 140);
        gfx->print("No detections yet");

        gfx->setTextSize(1);
        gfx->setTextColor(TEXT_COLOR);
        gfx->setCursor(10, 170);
        gfx->print("Waiting for surveillance");
        gfx->setCursor(10, 185);
        gfx->print("cameras...");
    }

    // Split area into WiFi (left) and BLE (right)
    int splitX = gfx->width() / 2;
    int listTop = 225;
    int listHeight = gfx->height() - 225 - 25;

    // Sort WiFi networks by signal strength (strongest first)
    std::vector<SeenSSID> sortedSSIDs = seenSSIDs;
    std::sort(sortedSSIDs.begin(), sortedSSIDs.end(), [](const SeenSSID &a, const SeenSSID &b) {
        return a.rssi > b.rssi; // Higher RSSI = stronger signal
    });

    // WiFi Networks List (LEFT COLUMN) - cyberpunk panel
    gfx->fillRect(0, listTop, splitX - 2, listHeight, PANEL_DARK);
    gfx->drawRect(0, listTop, splitX - 2, listHeight, SUCCESS_COLOR);  // Neon green border
    gfx->setTextSize(1);
    gfx->setTextColor(SUCCESS_COLOR);
    // Center "WIFI" header
    int wifiTextWidth = 6 * 6;  // "[WIFI]" = 6 chars * 6 pixels per char at size 1
    gfx->setCursor((splitX - 2 - wifiTextWidth) / 2, listTop + 5);
    gfx->print("[WIFI]");

    if (!sortedSSIDs.empty()) {
        int displayCount = min(16, (int)sortedSSIDs.size());
        int yPos = listTop + 18;

        for (int i = 0; i < displayCount; i++) {
            SeenSSID &ssid = sortedSSIDs[i];

            // Draw signal bars
            uint16_t barColor = ssid.rssi > -60 ? GREEN : (ssid.rssi > -75 ? YELLOW : RED);
            int bars = ssid.rssi > -60 ? 3 : (ssid.rssi > -75 ? 2 : 1);
            for (int b = 0; b < bars; b++) {
                gfx->fillRect(6, yPos - 2 - (b * 3), 2, b + 2, barColor);
            }

            // Draw SSID name (truncate for half width)
            gfx->setTextColor(TEXT_COLOR);
            gfx->setCursor(12, yPos);
            String displaySSID = ssid.ssid.length() > 13 ? ssid.ssid.substring(0, 13) + "." : ssid.ssid;
            gfx->print(displaySSID);

            // Draw RSSI
            gfx->setTextColor(barColor);
            gfx->setCursor(splitX - 38, yPos);
            gfx->print(ssid.rssi);

            yPos += 13;
        }
    } else {
        gfx->setTextColor(TEXT_COLOR);
        gfx->setCursor(5, listTop + 25);
        gfx->print("Scanning...");
    }

    // Sort BLE devices by signal strength (strongest first)
    std::vector<SeenBLE> sortedBLE = seenBLE;
    std::sort(sortedBLE.begin(), sortedBLE.end(), [](const SeenBLE &a, const SeenBLE &b) {
        return a.rssi > b.rssi; // Higher RSSI = stronger signal
    });

    // BLE Devices List (RIGHT COLUMN) - cyberpunk panel
    gfx->fillRect(splitX + 2, listTop, splitX - 2, listHeight, PANEL_DARKER);
    gfx->drawRect(splitX + 2, listTop, splitX - 2, listHeight, ACCENT_COLOR);  // Neon magenta border
    gfx->setTextColor(ACCENT_COLOR);
    // Center "BLE" header
    int bleTextWidth = 5 * 6;  // "[BLE]" = 5 chars * 6 pixels per char at size 1
    gfx->setCursor(splitX + 2 + (splitX - 2 - bleTextWidth) / 2, listTop + 5);
    gfx->print("[BLE]");

    if (!sortedBLE.empty()) {
        int displayCount = min(16, (int)sortedBLE.size());
        int yPos = listTop + 18;

        for (int i = 0; i < displayCount; i++) {
            SeenBLE &ble = sortedBLE[i];

            // Draw signal bars
            uint16_t barColor = ble.rssi > -60 ? GREEN : (ble.rssi > -75 ? YELLOW : RED);
            int bars = ble.rssi > -60 ? 3 : (ble.rssi > -75 ? 2 : 1);
            for (int b = 0; b < bars; b++) {
                gfx->fillRect(splitX + 4, yPos - 2 - (b * 3), 2, b + 2, barColor);
            }

            // Draw device name
            gfx->setTextColor(TEXT_COLOR);
            gfx->setCursor(splitX + 10, yPos);
            String displayName = ble.name.length() > 13 ? ble.name.substring(0, 13) + "." : ble.name;
            gfx->print(displayName);

            // Draw RSSI
            gfx->setTextColor(barColor);
            gfx->setCursor(gfx->width() - 38, yPos);
            gfx->print(ble.rssi);

            yPos += 13;
        }
    } else {
        gfx->setTextColor(TEXT_COLOR);
        gfx->setCursor(splitX + 7, listTop + 25);
        gfx->print("Scanning...");
    }

    // Status bar at bottom - cyberpunk style
    gfx->drawFastHLine(0, gfx->height() - 25, gfx->width(), ACCENT_COLOR);  // Neon line
    gfx->fillRect(0, gfx->height() - 24, gfx->width(), 24, HEADER_COLOR);
    gfx->setTextSize(1);
    gfx->setTextColor(SUCCESS_COLOR);
    gfx->setCursor(10, gfx->height() - 18);
    gfx->print("[CH:");
    gfx->print(currentChannel);
    gfx->print("] SCANNING...");

    // Show uptime
    gfx->setCursor(gfx->width() - 100, gfx->height() - 18);
    gfx->print("Up: ");
    gfx->print(millis() / 1000);
    gfx->print("s");
}

void DisplayHandler::handleTouch() {
    int16_t sx, sy;
    if (!touchReadScreen(sx, sy)) return;

    // Simple touch handling - recalibrate on top-left corner hold
    static uint32_t holdT0 = 0;
    bool inTopLeft = (sx < 40 && sy < 40);

    if (inTopLeft) {
        if (!holdT0) holdT0 = millis();
        if (millis() - holdT0 > 1500) {
            runCalibration();
            clear();
            holdT0 = 0;
            return;
        }
    } else {
        holdT0 = 0;
    }
}

void DisplayHandler::addDetection(String ssid, String mac, int8_t rssi, String type) {
    Detection det;
    det.ssid = ssid;
    det.mac = mac;
    det.rssi = rssi;
    det.type = type;
    det.timestamp = millis();
    det.isNew = true;

    detections.push_back(det);

    // Limit list size
    if (detections.size() > 100) {
        detections.erase(detections.begin());
    }

    totalDetections++;

    if (type == "BLE") {
        bleDetections++;
    } else {
        // All non-BLE detections are WiFi
        flockDetections++;
    }

    // Trigger flash alert for 10 seconds
    isFlashing = true;
    flashStartTime = millis();
    flashState = false;

    // Save detection to SD card
    saveDetectionToSD(det);

    needsRedraw = true;
}

void DisplayHandler::clearDetections() {
    detections.clear();
    totalDetections = 0;
    flockDetections = 0;
    bleDetections = 0;
    clear();
}

void DisplayHandler::setPage(DisplayPage page) {
    currentPage = page;
    clear();
}

void DisplayHandler::nextPage() {
    currentPage = (DisplayPage)((currentPage + 1) % 5);
    clear();
}

void DisplayHandler::previousPage() {
    currentPage = (DisplayPage)((currentPage + 4) % 5);
    clear();
}

void DisplayHandler::setBrightness(uint8_t level) {
    brightness = level;
}

void DisplayHandler::sleep() {
    displayActive = false;
}

void DisplayHandler::wake() {
    displayActive = true;
    needsRedraw = true;
}

void DisplayHandler::setRotation(uint8_t rotation) {
    clear();
}

void DisplayHandler::enableTouch(bool enable) {
    // Touch is always enabled
}

void DisplayHandler::setUpdateInterval(uint32_t interval) {
    // Not implemented
}

void DisplayHandler::showAlert(String message, uint16_t color) {
    gfx->fillRect(50, gfx->height()/2 - 30, gfx->width() - 100, 60, color);
    gfx->setTextSize(2);
    gfx->setTextColor(TEXT_COLOR);
    gfx->setCursor(60, gfx->height()/2 - 10);
    gfx->print(message);
    delay(2000);
    needsRedraw = true;
}

void DisplayHandler::showInfo(String message) {
    showAlert(message, INFO_COLOR);
}

void DisplayHandler::showProgress(String message, float progress) {
    gfx->fillRect(50, gfx->height()/2 - 40, gfx->width() - 100, 80, BG_COLOR);
    gfx->setTextSize(1);
    gfx->setTextColor(TEXT_COLOR);
    gfx->setCursor(60, gfx->height()/2 - 30);
    gfx->print(message);

    // Progress bar
    int barWidth = gfx->width() - 120;
    int fillWidth = barWidth * progress;
    gfx->drawRect(60, gfx->height()/2, barWidth, 20, TEXT_COLOR);
    gfx->fillRect(61, gfx->height()/2 + 1, fillWidth - 2, 18, SUCCESS_COLOR);
}

void DisplayHandler::updateStatus(String status) {
    // Simple status at bottom
}

void DisplayHandler::updateChannelInfo(uint8_t channel) {
    currentChannel = channel;
}

void DisplayHandler::updateScanStatus(bool isScanning) {
    // Not displayed in simple version
}

void DisplayHandler::showDebugSSID(String ssid, int8_t rssi, uint8_t channel) {
    lastSSID = ssid;
    lastRSSI = rssi;
    currentChannel = channel;

    // Add or update SSID in seen list
    bool found = false;
    for (auto &seen : seenSSIDs) {
        if (seen.ssid == ssid) {
            seen.rssi = rssi;
            seen.channel = channel;
            seen.lastSeen = millis();
            found = true;
            break;
        }
    }

    if (!found) {
        SeenSSID newSSID;
        newSSID.ssid = ssid;
        newSSID.rssi = rssi;
        newSSID.channel = channel;
        newSSID.lastSeen = millis();
        seenSSIDs.push_back(newSSID);

        // Keep only the 16 most recent
        if (seenSSIDs.size() > 16) {
            seenSSIDs.erase(seenSSIDs.begin());
        }
    }

    needsRedraw = true;
}

void DisplayHandler::showDebugBLE(String name, String mac, int8_t rssi) {
    // Add or update BLE device in seen list
    bool found = false;
    for (auto &seen : seenBLE) {
        if (seen.mac == mac) {
            seen.name = name.length() > 0 ? name : mac;  // Show MAC if no name
            seen.rssi = rssi;
            seen.lastSeen = millis();
            found = true;
            break;
        }
    }

    if (!found) {
        SeenBLE newBLE;
        newBLE.name = name.length() > 0 ? name : mac;  // Show MAC if no name
        newBLE.mac = mac;
        newBLE.rssi = rssi;
        newBLE.lastSeen = millis();
        seenBLE.push_back(newBLE);

        // Keep only the 16 most recent
        if (seenBLE.size() > 16) {
            seenBLE.erase(seenBLE.begin());
        }
    }

    needsRedraw = true;
}

// ===================== Touch handling methods (from review code) =====================

void DisplayHandler::touchBegin() {
    pinMode(TOUCH_CS, OUTPUT);
    digitalWrite(TOUCH_CS, HIGH);
    SPI.begin(BUS_SCK, BUS_MISO, BUS_MOSI, TOUCH_CS);
}

static uint16_t med5(uint16_t a, uint16_t b, uint16_t c, uint16_t d, uint16_t e) {
    uint16_t v[5] = {a, b, c, d, e};
    for (int i = 0; i < 5; i++)
        for (int j = i + 1; j < 5; j++)
            if (v[j] < v[i]) {
                uint16_t t = v[i];
                v[i] = v[j];
                v[j] = t;
            }
    return v[2];
}

bool DisplayHandler::xptReadRaw(uint16_t &rx, uint16_t &ry, uint16_t &z) {
    digitalWrite(TFT_CS, HIGH);
    SPI.beginTransaction(SPISettings(500000, MSBFIRST, SPI_MODE0));
    digitalWrite(TOUCH_CS, LOW);

    auto tr = [](uint8_t b) { return SPI.transfer(b); };

    uint16_t xs[5], ys[5], zs[5];
    for (int i = 0; i < 5; i++) {
        tr(0xB1);
        uint8_t h1 = tr(0x00), l1 = tr(0x00);
        tr(0xC1);
        uint8_t h2 = tr(0x00), l2 = tr(0x00);
        uint16_t z1 = ((uint16_t)h1 << 5) | (l1 >> 3);
        uint16_t z2 = ((uint16_t)h2 << 5) | (l2 >> 3);
        uint16_t zz = (z1 && z2) ? (z2 + (4095 - z1)) : 0;

        tr(0x91);
        uint8_t hx = tr(0x00), lx = tr(0x00);
        tr(0xD1);
        uint8_t hy = tr(0x00), ly = tr(0x00);
        uint16_t xr = ((uint16_t)hx << 5) | (lx >> 3);
        uint16_t yr = ((uint16_t)hy << 5) | (ly >> 3);

        xs[i] = xr;
        ys[i] = yr;
        zs[i] = zz;
    }

    digitalWrite(TOUCH_CS, HIGH);
    SPI.endTransaction();

    rx = med5(xs[0], xs[1], xs[2], xs[3], xs[4]);
    ry = med5(ys[0], ys[1], ys[2], ys[3], ys[4]);
    z = med5(zs[0], zs[1], zs[2], zs[3], zs[4]);

    if (z < 40) return false;
    if (rx == 0 || rx == 4095 || ry == 0 || ry == 4095) return false;
    return true;
}

static bool invert3x3(const float m[9], float invOut[9]) {
    float a00 = m[0], a01 = m[1], a02 = m[2];
    float a10 = m[3], a11 = m[4], a12 = m[5];
    float a20 = m[6], a21 = m[7], a22 = m[8];
    float b01 = a22 * a11 - a12 * a21;
    float b11 = -a22 * a10 + a12 * a20;
    float b21 = a21 * a10 - a11 * a20;
    float det = a00 * b01 + a01 * b11 + a02 * b21;
    if (fabsf(det) < 1e-6f) return false;
    float invDet = 1.0f / det;
    invOut[0] = b01 * invDet;
    invOut[1] = (-a22 * a01 + a02 * a21) * invDet;
    invOut[2] = (a12 * a01 - a02 * a11) * invDet;
    invOut[3] = b11 * invDet;
    invOut[4] = (a22 * a00 - a02 * a20) * invDet;
    invOut[5] = (-a12 * a00 + a02 * a10) * invDet;
    invOut[6] = b21 * invDet;
    invOut[7] = (-a21 * a00 + a01 * a20) * invDet;
    invOut[8] = (a11 * a00 - a01 * a10) * invDet;
    return true;
}

bool DisplayHandler::mapRawToScreen(uint16_t rx, uint16_t ry, int16_t &sx, int16_t &sy) {
    if (!gCal.valid) return false;
    float xf = gCal.a * rx + gCal.b * ry + gCal.c;
    float yf = gCal.d * rx + gCal.e * ry + gCal.f;
    if (xf < 0) xf = 0;
    if (xf > gfx->width() - 1) xf = gfx->width() - 1;
    if (yf < 0) yf = 0;
    if (yf > gfx->height() - 1) yf = gfx->height() - 1;
    sx = (int16_t)lroundf(xf);
    sy = (int16_t)lroundf(yf);
    return true;
}

bool DisplayHandler::touchReadScreen(int16_t &sx, int16_t &sy) {
    uint16_t rx, ry, z;
    if (!xptReadRaw(rx, ry, z)) return false;
    return mapRawToScreen(rx, ry, sx, sy);
}

void DisplayHandler::saveCal() {
    prefs.begin("xptcal", false);
    prefs.putBytes("cal", &gCal, sizeof(gCal));
    prefs.end();
}

bool DisplayHandler::loadCal() {
    prefs.begin("xptcal", true);
    bool ok = prefs.getBytesLength("cal") == sizeof(gCal);
    if (ok) prefs.getBytes("cal", &gCal, sizeof(gCal));
    prefs.end();
    return ok && gCal.valid;
}

void DisplayHandler::runCalibration() {
    struct P { int16_t x, y; } T[3] = {
        {40, 40},
        {(int16_t)(gfx->width() - 40), (int16_t)(gfx->height() / 2)},
        {(int16_t)(gfx->width() / 2), (int16_t)(gfx->height() - 40)}
    };

    gfx->fillScreen(BG_COLOR);
    gfx->setTextSize(2);
    gfx->setTextColor(TEXT_COLOR);
    gfx->setCursor(10, 10);
    gfx->print("Touch each cross...");

    uint16_t rx[3] = {0}, ry[3] = {0};

    for (int i = 0; i < 3; i++) {
        gfx->fillRect(0, 32, gfx->width(), gfx->height() - 32, BG_COLOR);

        // Draw crosses
        for (int k = 0; k <= i; k++) {
            const int s = 10;
            gfx->drawLine(T[k].x - s, T[k].y, T[k].x + s, T[k].y, WHITE);
            gfx->drawLine(T[k].x, T[k].y - s, T[k].x, T[k].y + s, WHITE);
            gfx->drawRect(T[k].x - s - 2, T[k].y - s - 2, 2 * (s + 2), 2 * (s + 2), YELLOW);
        }

        gfx->setCursor(10, 32);
        gfx->printf("Point %d/3", i + 1);

        // Wait for touch
        uint16_t rrx, rry, rz;
        while (!xptReadRaw(rrx, rry, rz)) delay(4);

        // Collect samples
        uint32_t t0 = millis();
        uint32_t sx = 0, sy = 0, n = 0;
        while (millis() - t0 < 220) {
            if (xptReadRaw(rrx, rry, rz)) {
                sx += rrx;
                sy += rry;
                n++;
            } else
                break;
            delay(3);
        }
        if (n == 0) {
            i--;
            continue;
        }
        rx[i] = sx / n;
        ry[i] = sy / n;

        // Wait release
        while (xptReadRaw(rrx, rry, rz)) delay(6);
    }

    // Solve affine
    float M[9] = {
        (float)rx[0], (float)ry[0], 1.0f,
        (float)rx[1], (float)ry[1], 1.0f,
        (float)rx[2], (float)ry[2], 1.0f
    };

    float Minv[9];
    if (!invert3x3(M, Minv)) {
        gfx->setCursor(10, 64);
        gfx->setTextColor(RED);
        gfx->print("Calibration failed");
        delay(1200);
        return;
    }

    float Xv[3] = {(float)T[0].x, (float)T[1].x, (float)T[2].x};
    float Yv[3] = {(float)T[0].y, (float)T[1].y, (float)T[2].y};

    gCal.valid = true;
    gCal.a = Minv[0] * Xv[0] + Minv[1] * Xv[1] + Minv[2] * Xv[2];
    gCal.b = Minv[3] * Xv[0] + Minv[4] * Xv[1] + Minv[5] * Xv[2];
    gCal.c = Minv[6] * Xv[0] + Minv[7] * Xv[1] + Minv[8] * Xv[2];
    gCal.d = Minv[0] * Yv[0] + Minv[1] * Yv[1] + Minv[2] * Yv[2];
    gCal.e = Minv[3] * Yv[0] + Minv[4] * Yv[1] + Minv[5] * Yv[2];
    gCal.f = Minv[6] * Yv[0] + Minv[7] * Yv[1] + Minv[8] * Yv[2];

    saveCal();

    gfx->fillRect(0, 32, gfx->width(), gfx->height() - 32, BG_COLOR);
    gfx->setCursor(10, 40);
    gfx->setTextColor(GREEN);
    gfx->print("Calibration saved!");
    delay(700);
}

void DisplayHandler::addTouchZone(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, void (*callback)(), String label) {
    // Not implemented in simple version
}

void DisplayHandler::clearTouchZones() {
    // Not implemented in simple version
}

// ============================================================================
// SD CARD FUNCTIONS
// ============================================================================

bool DisplayHandler::initSDCard() {
    // Initialize SD card with SPI
    Serial.println("Initializing SD card...");

    // The CYD uses shared SPI bus with the display
    // SD card CS is typically GPIO 5
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);

    if (!SD.begin(SD_CS)) {
        Serial.println("SD Card initialization failed!");
        sdCardAvailable = false;
        return false;
    }

    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("No SD card attached");
        sdCardAvailable = false;
        return false;
    }

    Serial.print("SD Card Type: ");
    if (cardType == CARD_MMC) {
        Serial.println("MMC");
    } else if (cardType == CARD_SD) {
        Serial.println("SDSC");
    } else if (cardType == CARD_SDHC) {
        Serial.println("SDHC");
    } else {
        Serial.println("UNKNOWN");
    }

    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("SD Card Size: %lluMB\n", cardSize);

    sdCardAvailable = true;
    return true;
}

String DisplayHandler::createLogFileName() {
    // Create a log filename with date/time (using millis as timestamp)
    // Format: /detections_XXXXXX.csv where XXXXXX is millis/1000
    uint32_t timestamp = millis() / 1000;
    String filename = "/detections_" + String(timestamp) + ".csv";

    // Create file with CSV header if it doesn't exist
    if (!SD.exists(filename)) {
        File file = SD.open(filename, FILE_WRITE);
        if (file) {
            file.println("timestamp,ssid,mac_address,rssi,type,detection_time");
            file.close();
            Serial.print("Created new log file: ");
            Serial.println(filename);
        } else {
            Serial.println("Failed to create log file!");
        }
    }

    return filename;
}

void DisplayHandler::saveDetectionToSD(const Detection& det) {
    if (!sdCardAvailable || currentLogFile.length() == 0) {
        return; // SD card not available
    }

    File file = SD.open(currentLogFile, FILE_APPEND);
    if (!file) {
        Serial.println("Failed to open log file for writing!");
        return;
    }

    // Write CSV row: timestamp,ssid,mac_address,rssi,type,detection_time
    file.print(det.timestamp);
    file.print(",");

    // Escape commas in SSID
    String escapedSSID = det.ssid;
    escapedSSID.replace(",", ";");
    file.print("\"");
    file.print(escapedSSID);
    file.print("\",");

    file.print(det.mac);
    file.print(",");
    file.print(det.rssi);
    file.print(",");
    file.print(det.type);
    file.print(",");
    file.print(det.timestamp / 1000.0, 3); // Time in seconds with 3 decimal places
    file.println();

    file.close();

    Serial.print("Saved detection to SD: ");
    Serial.print(det.mac);
    Serial.print(" (");
    Serial.print(det.ssid);
    Serial.println(")");
}

#endif // CYD_DISPLAY
