/**
 * @file display_handler_147.cpp
 * @brief Display UI for Waveshare ESP32-S3-LCD-1.47 (172x320 ST7789)
 */

#ifdef WAVESHARE_147

#include "display_handler_147.h"
#include <Arduino.h>

// Global instance
DisplayHandler display;

DisplayHandler::DisplayHandler() :
    tft(),
    needsRedraw(true),
    lastUpdate(0),
    brightness(200),
    totalDetections(0),
    flockDetections(0),
    bleDetections(0),
    scrollOffset(0),
    lastScrollTime(0),
    scrollPaused(false),
    currentChannel(1),
    bleScanning(false),
    sdCardPresent(false),
    lastSdCheck(0),
    detectionsLogged(0),
    ledState(0),
    lastLedUpdate(0),
    ledFlashState(false),
    detectionRssi(-100)
{
}

void DisplayHandler::setupBacklightPWM() {
    // ESP32-S3 uses LEDC for PWM
    ledcSetup(0, 5000, 8);  // Channel 0, 5kHz, 8-bit
    ledcAttachPin(TFT_BL, 0);
    applyBrightness();
}

void DisplayHandler::applyBrightness() {
    ledcWrite(0, brightness);
}

bool DisplayHandler::begin() {
    Serial.println("Waveshare 1.47\" Display initializing...");

    // Initialize TFT
    tft.init();
    tft.setRotation(0);  // Portrait mode
    tft.fillScreen(BG_COLOR);

    // Setup backlight
    setupBacklightPWM();

    // Initialize FastLED for WS2812
    FastLED.addLeds<WS2812, RGB_LED_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(50);
    leds[0] = CRGB::Black;
    FastLED.show();

    // Initialize SD Card (SDMMC)
    initSDCard();

    // Show boot animation
    showBootAnimation();

    needsRedraw = true;
    return true;
}

void DisplayHandler::showBootAnimation() {
    tft.fillScreen(TFT_BLACK);

    // Scan lines effect
    for (int y = 0; y < 320; y += 4) {
        tft.drawFastHLine(0, y, 172, 0x0841);
        delay(5);
    }

    delay(150);

    // Title - centered for 172px width
    tft.setTextSize(2);
    tft.setTextColor(ALERT_COLOR);
    const char* title = "FLOCK YOU";
    int titleX = (172 - strlen(title) * 12) / 2;
    for (int i = 0; title[i]; i++) {
        tft.setCursor(titleX + i * 12, 30);
        tft.print(title[i]);
        delay(80);
    }

    delay(150);

    // Subtitle
    tft.setTextSize(1);
    tft.setTextColor(ACCENT_COLOR);
    tft.setCursor(20, 55);
    tft.print("SURVEILLANCE DETECTOR");

    delay(300);

    // Radar animation - smaller for narrow screen
    int centerX = 86, centerY = 150, radius = 45;

    // Draw radar grid
    tft.drawCircle(centerX, centerY, radius, 0x2945);
    tft.drawCircle(centerX, centerY, radius * 2 / 3, 0x2104);
    tft.drawCircle(centerX, centerY, radius / 3, 0x2104);
    tft.drawFastHLine(centerX - radius, centerY, radius * 2, 0x2945);
    tft.drawFastVLine(centerX, centerY - radius, radius * 2, 0x2945);

    // Corner markers
    tft.drawLine(centerX - radius + 3, centerY - radius + 3, centerX - radius + 10, centerY - radius + 3, ACCENT_COLOR);
    tft.drawLine(centerX - radius + 3, centerY - radius + 3, centerX - radius + 3, centerY - radius + 10, ACCENT_COLOR);
    tft.drawLine(centerX + radius - 3, centerY - radius + 3, centerX + radius - 10, centerY - radius + 3, ACCENT_COLOR);
    tft.drawLine(centerX + radius - 3, centerY - radius + 3, centerX + radius - 3, centerY - radius + 10, ACCENT_COLOR);
    tft.drawLine(centerX - radius + 3, centerY + radius - 3, centerX - radius + 10, centerY + radius - 3, ACCENT_COLOR);
    tft.drawLine(centerX - radius + 3, centerY + radius - 3, centerX - radius + 3, centerY + radius - 10, ACCENT_COLOR);
    tft.drawLine(centerX + radius - 3, centerY + radius - 3, centerX + radius - 10, centerY + radius - 3, ACCENT_COLOR);
    tft.drawLine(centerX + radius - 3, centerY + radius - 3, centerX + radius - 3, centerY + radius - 10, ACCENT_COLOR);

    // Radar sweep
    for (int angle = 0; angle < 360; angle += 6) {
        float rad = angle * 3.14159 / 180.0;
        int x2 = centerX + cos(rad) * radius;
        int y2 = centerY - sin(rad) * radius;

        tft.drawLine(centerX, centerY, x2, y2, SUCCESS_COLOR);
        delay(8);
        tft.drawLine(centerX, centerY, x2, y2, 0x0320);

        if (angle % 60 == 0 && angle > 0) {
            float detectRad = (angle - 45) * 3.14159 / 180.0;
            int bx = centerX + cos(detectRad) * (radius * 0.6);
            int by = centerY - sin(detectRad) * (radius * 0.6);
            tft.fillCircle(bx, by, 3, ALERT_COLOR);
        }
    }

    delay(300);

    // Clear radar area
    tft.fillRect(centerX - radius - 5, centerY - radius - 5, radius * 2 + 10, radius * 2 + 10, TFT_BLACK);

    // Status messages
    const char* messages[] = {"WiFi init", "BLE init", "Patterns", "SD Card", "Ready"};
    uint16_t msgColors[] = {WIFI_COLOR, BLE_COLOR, ALERT_COLOR, ACCENT_COLOR, SUCCESS_COLOR};

    tft.setTextSize(1);
    int msgY = 120;
    for (int i = 0; i < 5; i++) {
        tft.setTextColor(msgColors[i]);
        tft.setCursor(15, msgY + i * 14);
        for (int c = 0; messages[i][c]; c++) {
            tft.print(messages[i][c]);
            delay(20);
        }
        tft.setTextColor(TEXT_DIM);
        for (int d = 0; d < 3; d++) {
            tft.print(".");
            delay(100);
        }
        tft.setTextColor(SUCCESS_COLOR);
        tft.print(" OK");
        delay(150);
    }

    // Final hunting message
    delay(400);
    tft.fillScreen(TFT_BLACK);

    tft.setTextSize(2);
    tft.setTextColor(SUCCESS_COLOR);
    tft.setCursor(23, 140);
    tft.print("HUNTING");

    tft.setTextSize(1);
    tft.setTextColor(ALERT_COLOR);
    tft.setCursor(12, 170);
    tft.print("for surveillance...");

    // Animated brackets
    for (int i = 0; i < 3; i++) {
        tft.setTextColor(i % 2 ? SUCCESS_COLOR : ACCENT_COLOR);
        tft.setTextSize(2);
        tft.setCursor(5, 140);
        tft.print(">");
        tft.setCursor(155, 140);
        tft.print("<");
        delay(250);
    }

    delay(500);
}

void DisplayHandler::update() {
    uint32_t now = millis();

    // Check SD card periodically
    checkSDCard();

    // Update LED state
    updateLED();

    // Auto-scroll detection list every 3 seconds
    if (!scrollPaused && detections.size() > 4 && now - lastScrollTime > 3000) {
        scrollOffset++;
        if (scrollOffset >= (int)detections.size()) {
            scrollOffset = 0;
        }
        lastScrollTime = now;
        needsRedraw = true;
    }

    // Pause scrolling briefly after new detection
    if (scrollPaused && now - lastScrollTime > 5000) {
        scrollPaused = false;
    }

    // Redraw if needed
    if (needsRedraw || now - lastUpdate > 1000) {
        drawHeader();
        drawStatsPanel();
        drawDetectionList();
        drawFooter();
        lastUpdate = now;
        needsRedraw = false;
    }
}

void DisplayHandler::drawHeader() {
    tft.fillRect(0, 0, 172, HEADER_HEIGHT, HEADER_COLOR);

    // Title
    tft.setTextSize(1);
    tft.setTextColor(TEXT_COLOR);
    tft.setCursor(4, 4);
    tft.print("FLOCK YOU");

    // Channel indicator
    tft.setTextColor(bleScanning ? BLE_COLOR : WIFI_COLOR);
    tft.setCursor(4, 14);
    tft.printf("%s CH:%d", bleScanning ? "BLE" : "WiFi", currentChannel);

    // SD indicator
    tft.setCursor(130, 4);
    if (sdCardPresent) {
        tft.setTextColor(SUCCESS_COLOR);
        tft.print("SD");
    } else {
        tft.setTextColor(ALERT_COLOR);
        tft.print("--");
    }

    // Detection count
    tft.setTextColor(flockDetections > 0 ? ALERT_COLOR : TEXT_DIM);
    tft.setCursor(130, 14);
    tft.printf("%02d", flockDetections);
}

void DisplayHandler::drawStatsPanel() {
    int y = HEADER_HEIGHT + 2;

    // Stats background
    tft.fillRect(0, y, 172, STAT_BOX_HEIGHT, BG_DARK);

    // Three stat boxes
    int boxW = 54;
    int boxH = STAT_BOX_HEIGHT - 4;

    // WiFi detections
    tft.drawRect(2, y + 2, boxW, boxH, WIFI_COLOR);
    tft.setTextSize(2);
    tft.setTextColor(WIFI_COLOR);
    tft.setCursor(12, y + 8);
    tft.printf("%d", totalDetections - bleDetections - flockDetections);
    tft.setTextSize(1);
    tft.setCursor(12, y + 30);
    tft.print("WiFi");

    // BLE detections
    tft.drawRect(59, y + 2, boxW, boxH, BLE_COLOR);
    tft.setTextSize(2);
    tft.setTextColor(BLE_COLOR);
    tft.setCursor(69, y + 8);
    tft.printf("%d", bleDetections);
    tft.setTextSize(1);
    tft.setCursor(69, y + 30);
    tft.print("BLE");

    // Flock/threat detections
    tft.drawRect(116, y + 2, boxW, boxH, ALERT_COLOR);
    tft.setTextSize(2);
    tft.setTextColor(flockDetections > 0 ? ALERT_COLOR : TEXT_DIM);
    tft.setCursor(126, y + 8);
    tft.printf("%d", flockDetections);
    tft.setTextSize(1);
    tft.setTextColor(ALERT_COLOR);
    tft.setCursor(120, y + 30);
    tft.print("THREAT");
}

void DisplayHandler::drawDetectionList() {
    int startY = HEADER_HEIGHT + STAT_BOX_HEIGHT + 4;
    int listHeight = 320 - startY - FOOTER_HEIGHT - 2;
    int maxItems = listHeight / LIST_ITEM_HEIGHT;

    // Clear list area
    tft.fillRect(0, startY, 172, listHeight, BG_COLOR);

    if (detections.empty()) {
        tft.setTextSize(1);
        tft.setTextColor(TEXT_DIM);
        tft.setCursor(30, startY + listHeight / 2 - 4);
        tft.print("No detections");
        return;
    }

    // Draw visible items
    for (int i = 0; i < maxItems && i + scrollOffset < (int)detections.size(); i++) {
        Detection& d = detections[i + scrollOffset];
        int itemY = startY + i * LIST_ITEM_HEIGHT;

        // Color bar on left
        uint16_t barColor = WIFI_COLOR;
        if (d.type.indexOf("flock") >= 0 || d.type.indexOf("Flock") >= 0 ||
            d.type.indexOf("penguin") >= 0 || d.type.indexOf("pigvision") >= 0) {
            barColor = ALERT_COLOR;
        } else if (d.type == "ble") {
            barColor = BLE_COLOR;
        }
        tft.fillRect(0, itemY, 3, LIST_ITEM_HEIGHT - 2, barColor);

        // SSID or vendor (truncated for narrow screen)
        tft.setTextSize(1);
        tft.setTextColor(d.isNew ? TEXT_COLOR : TEXT_DIM);
        tft.setCursor(6, itemY + 2);
        String label = d.vendor.length() > 0 ? d.vendor : d.ssid;
        if (label.length() > 18) {
            label = label.substring(0, 15) + "...";
        }
        tft.print(label);

        // RSSI and signal bars
        tft.setTextColor(TEXT_DIM);
        tft.setCursor(6, itemY + 14);
        tft.printf("%ddBm", d.rssi);
        drawSignalBars(140, itemY + 12, d.rssi);

        // Mark as not new after display
        d.isNew = false;
    }

    // Scroll indicator
    if (detections.size() > maxItems) {
        int indicatorH = listHeight * maxItems / detections.size();
        int indicatorY = startY + (listHeight - indicatorH) * scrollOffset / (detections.size() - maxItems);
        tft.fillRect(169, indicatorY, 3, indicatorH, TEXT_DIM);
    }
}

void DisplayHandler::drawSignalBars(uint16_t x, uint16_t y, int8_t rssi) {
    int bars = 0;
    if (rssi > -50) bars = 4;
    else if (rssi > -60) bars = 3;
    else if (rssi > -70) bars = 2;
    else if (rssi > -80) bars = 1;

    for (int i = 0; i < 4; i++) {
        uint16_t color = (i < bars) ? SUCCESS_COLOR : 0x2104;
        int h = 4 + i * 2;
        tft.fillRect(x + i * 6, y + (12 - h), 4, h, color);
    }
}

void DisplayHandler::drawFooter() {
    int y = 320 - FOOTER_HEIGHT;
    tft.fillRect(0, y, 172, FOOTER_HEIGHT, FOOTER_COLOR);

    // Total count
    tft.setTextSize(1);
    tft.setTextColor(TEXT_COLOR);
    tft.setCursor(4, y + 6);
    tft.printf("Total: %d", totalDetections);

    // Logged count
    if (sdCardPresent) {
        tft.setTextColor(SUCCESS_COLOR);
        tft.setCursor(100, y + 6);
        tft.printf("Log:%d", detectionsLogged);
    }
}

void DisplayHandler::clear() {
    tft.fillScreen(BG_COLOR);
    needsRedraw = true;
}

void DisplayHandler::setBrightness(uint8_t level) {
    brightness = level;
    applyBrightness();
}

bool DisplayHandler::initSDCard() {
    Serial.println("Initializing SD Card (SDMMC)...");

    // SDMMC 1-bit mode for Waveshare board
    if (!SD_MMC.begin("/sdcard", true)) {  // true = 1-bit mode
        Serial.println("SD Card mount failed");
        sdCardPresent = false;
        return false;
    }

    uint8_t cardType = SD_MMC.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("No SD Card attached");
        sdCardPresent = false;
        return false;
    }

    Serial.printf("SD Card Type: %s\n",
        cardType == CARD_MMC ? "MMC" :
        cardType == CARD_SD ? "SDSC" :
        cardType == CARD_SDHC ? "SDHC" : "UNKNOWN");
    Serial.printf("SD Card Size: %lluMB\n", SD_MMC.cardSize() / (1024 * 1024));

    sdCardPresent = true;

    // Setup log file
    logFileName = "/flockyou_detections.csv";
    if (!SD_MMC.exists(logFileName)) {
        fs::File file = SD_MMC.open(logFileName, FILE_WRITE);
        if (file) {
            file.println("timestamp,ssid,mac,vendor,rssi,type");
            file.close();
        }
    }

    return true;
}

void DisplayHandler::checkSDCard() {
    uint32_t now = millis();
    if (now - lastSdCheck < 5000) return;
    lastSdCheck = now;

    bool wasPresent = sdCardPresent;

    if (sdCardPresent) {
        // Test if card still accessible
        fs::File testFile = SD_MMC.open("/");
        if (!testFile) {
            sdCardPresent = false;
            SD_MMC.end();
            Serial.println("SD Card: Removed");
        } else {
            testFile.close();
        }
    } else {
        // Try to reinitialize
        if (SD_MMC.begin("/sdcard", true)) {
            fs::File testFile = SD_MMC.open("/");
            if (testFile) {
                testFile.close();
                sdCardPresent = true;
                Serial.println("SD Card: Inserted");
            }
        }
    }

    if (wasPresent != sdCardPresent) {
        needsRedraw = true;
    }
}

void DisplayHandler::logDetection(const String& ssid, const String& mac, int8_t rssi, const String& type) {
    if (!sdCardPresent) return;

    fs::File file = SD_MMC.open(logFileName, FILE_APPEND);
    if (file) {
        String vendor = lookupOUI(mac);
        file.printf("%lu,%s,%s,%s,%d,%s\n",
            millis() / 1000,
            ssid.c_str(),
            mac.c_str(),
            vendor.c_str(),
            rssi,
            type.c_str());
        file.close();
        detectionsLogged++;
    }
}

void DisplayHandler::addDetection(String ssid, String mac, int8_t rssi, String type) {
    // Look up vendor
    String vendor = lookupOUI(mac);

    // Check if this is a threat
    bool isThreat = (type.indexOf("flock") >= 0 || type.indexOf("Flock") >= 0 ||
                    type.indexOf("penguin") >= 0 || type.indexOf("pigvision") >= 0);

    // Check if already exists (update if so)
    for (auto& d : detections) {
        if (d.mac == mac) {
            d.rssi = rssi;
            d.timestamp = millis();
            d.isNew = true;
            needsRedraw = true;
            return;
        }
    }

    // Add new detection
    Detection d;
    d.ssid = ssid;
    d.mac = mac;
    d.vendor = vendor;
    d.rssi = rssi;
    d.type = type;
    d.timestamp = millis();
    d.isNew = true;

    detections.insert(detections.begin(), d);
    totalDetections++;

    if (isThreat) {
        flockDetections++;
        setLEDDetection(rssi);
    } else if (type == "ble") {
        bleDetections++;
    }

    // Limit list size
    if (detections.size() > 50) {
        detections.pop_back();
    }

    // Reset scroll on new detection
    scrollOffset = 0;
    scrollPaused = true;
    lastScrollTime = millis();

    // Log to SD
    logDetection(ssid, mac, rssi, type);

    needsRedraw = true;
}

void DisplayHandler::clearDetections() {
    detections.clear();
    totalDetections = 0;
    flockDetections = 0;
    bleDetections = 0;
    scrollOffset = 0;
    needsRedraw = true;
}

String DisplayHandler::lookupOUI(const String& mac) {
    if (mac.length() < 8) return "";
    String prefix = mac.substring(0, 8);
    prefix.toLowerCase();

    // Try embedded lookup first
    const char* embedded = lookupEmbeddedOUI(prefix.c_str());
    if (embedded) return String(embedded);

    // Try SD card lookup
    if (sdCardPresent) {
        return lookupOUIFromSD(prefix);
    }

    return "";
}

const char* DisplayHandler::lookupEmbeddedOUI(const char* prefix) {
    // Flock Safety / Surveillance OUIs
    if (strncasecmp(prefix, "58:8e:81", 8) == 0) return "Flock Safety";
    if (strncasecmp(prefix, "cc:cc:cc", 8) == 0) return "Flock Safety";
    if (strncasecmp(prefix, "ec:1b:bd", 8) == 0) return "Flock Safety";
    if (strncasecmp(prefix, "90:35:ea", 8) == 0) return "Flock Safety";
    if (strncasecmp(prefix, "70:c9:4e", 8) == 0) return "Flock Safety";
    if (strncasecmp(prefix, "3c:91:80", 8) == 0) return "Flock Safety";
    if (strncasecmp(prefix, "d8:f3:bc", 8) == 0) return "Flock Safety";
    if (strncasecmp(prefix, "80:30:49", 8) == 0) return "Flock Safety";

    // Common surveillance cameras
    if (strncasecmp(prefix, "54:c4:15", 8) == 0) return "Hikvision";
    if (strncasecmp(prefix, "44:47:cc", 8) == 0) return "Hikvision";
    if (strncasecmp(prefix, "c0:56:e3", 8) == 0) return "Hikvision";
    if (strncasecmp(prefix, "bc:ad:28", 8) == 0) return "Hikvision";
    if (strncasecmp(prefix, "a4:14:37", 8) == 0) return "Dahua";
    if (strncasecmp(prefix, "3c:ef:8c", 8) == 0) return "Dahua";
    if (strncasecmp(prefix, "9c:8e:cd", 8) == 0) return "Amcrest";

    // Common IoT
    if (strncasecmp(prefix, "24:0a:c4", 8) == 0) return "Espressif";
    if (strncasecmp(prefix, "30:ae:a4", 8) == 0) return "Espressif";
    if (strncasecmp(prefix, "a4:cf:12", 8) == 0) return "Espressif";
    if (strncasecmp(prefix, "b4:e6:2d", 8) == 0) return "Raspberry Pi";
    if (strncasecmp(prefix, "dc:a6:32", 8) == 0) return "Raspberry Pi";
    if (strncasecmp(prefix, "e4:5f:01", 8) == 0) return "Raspberry Pi";

    return nullptr;
}

String DisplayHandler::lookupOUIFromSD(const String& prefix) {
    fs::File file = SD_MMC.open("/oui.csv", FILE_READ);
    if (!file) return "";

    size_t fileSize = file.size();
    size_t recordSize = 35;
    size_t numRecords = fileSize / recordSize;

    size_t low = 0, high = numRecords;
    char buffer[64];

    while (low < high) {
        size_t mid = (low + high) / 2;
        file.seek(mid * recordSize);

        int len = file.readBytesUntil('\n', buffer, sizeof(buffer) - 1);
        if (len <= 0) break;
        buffer[len] = '\0';

        char* comma = strchr(buffer, ',');
        if (!comma) {
            low = mid + 1;
            continue;
        }

        *comma = '\0';
        int cmp = strncasecmp(buffer, prefix.c_str(), 8);

        if (cmp == 0) {
            file.close();
            return String(comma + 1);
        } else if (cmp < 0) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }

    file.close();
    return "";
}

void DisplayHandler::updateLED() {
    uint32_t now = millis();

    switch (ledState) {
        case 1:  // Scanning - solid green
            leds[0] = CRGB(0, 128, 0);
            break;

        case 2:  // Detection - flashing red
            {
                int flashInterval = map(constrain(detectionRssi, -90, -30), -90, -30, 400, 50);
                if (now - lastLedUpdate > flashInterval) {
                    ledFlashState = !ledFlashState;
                    lastLedUpdate = now;
                }
                leds[0] = ledFlashState ? CRGB::Red : CRGB::Black;
            }
            break;

        case 3:  // Alert - solid orange
            leds[0] = CRGB(255, 100, 0);
            break;

        default:  // Off
            leds[0] = CRGB::Black;
            break;
    }

    FastLED.show();
}

void DisplayHandler::setLEDScanning() {
    ledState = 1;
}

void DisplayHandler::setLEDDetection(int8_t rssi) {
    ledState = 2;
    detectionRssi = rssi;
}

void DisplayHandler::setLEDAlert() {
    ledState = 3;
}

void DisplayHandler::setLEDOff() {
    ledState = 0;
}

void DisplayHandler::showAlert(String message, uint16_t color) {
    int y = 320 / 2 - 20;
    tft.fillRect(10, y, 152, 40, BG_DARK);
    tft.drawRect(10, y, 152, 40, color);
    tft.setTextSize(1);
    tft.setTextColor(color);
    tft.setCursor(20, y + 15);
    tft.print(message);
}

void DisplayHandler::showInfo(String message) {
    showAlert(message, ACCENT_COLOR);
}

void DisplayHandler::updateChannelInfo(uint8_t channel) {
    currentChannel = channel;
    needsRedraw = true;
}

void DisplayHandler::updateScanMode(bool isBLE) {
    bleScanning = isBLE;
    needsRedraw = true;
}

void DisplayHandler::updateScanStatus(bool isScanning) {
    if (isScanning) {
        setLEDScanning();
    }
}

void DisplayHandler::showDebugSSID(String ssid, int8_t rssi, uint8_t channel) {
    // For debugging - could show in status area
}

void DisplayHandler::showDebugBLE(String name, String mac, int8_t rssi) {
    // For debugging
}

#endif // WAVESHARE_147
