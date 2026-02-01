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
    gfx->setTextSize(3);
    gfx->setCursor((gfx->width() - 18*9*3)/2, gfx->height()/2 - 40);
    gfx->print("FLOCK YOU");

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

    clear();
    return true;
}

void DisplayHandler::update() {
    uint32_t now = millis();

    // Check for touch input
    if (touchReadScreentirqTouched() && !touchDebounce) {
        handleTouch();
        touchDebounce = true;
        lastTouchTime = now;
    }

    // Reset touch debounce after 200ms
    if (touchDebounce && (now - lastTouchTime > 200)) {
        touchDebounce = false;
    }

    // Update display if needed
    if (needsRedraw || (now - lastUpdate > 1000)) {
        switch (currentPage) {
            case PAGE_MAIN:
                drawMainPage();
                break;
            case PAGE_LIST:
                drawListPage();
                break;
            case PAGE_STATS:
                drawStatsPage();
                break;
            case PAGE_SETTINGS:
                drawSettingsPage();
                break;
            case PAGE_ABOUT:
                drawAboutPage();
                break;
        }

        drawHeader();
        drawFooter();

        needsRedraw = false;
        lastUpdate = now;
    }
}

void DisplayHandler::clear() {
    gfx->fillScreen(BG_COLOR);
    needsRedraw = true;
}

void DisplayHandler::drawHeader() {
    // Draw header background
    gfx->fillRect(0, 0, gfx->width(), HEADER_HEIGHT, HEADER_COLOR);

    // Draw title
    gfx->setTextColor(TEXT_COLOR);
    gfx->setTextDatum(ML_DATUM);
    gfx->setTextSize(2);
    gfx->drawString("FLOCK YOU", 10, HEADER_HEIGHT / 2);

    // Draw detection counter
    gfx->setTextDatum(MR_DATUM);
    gfx->setTextSize(1);
    String counter = "Detections: " + String(totalDetections);
    gfx->drawString(counter, gfx->width() - 10, HEADER_HEIGHT / 2);
}

void DisplayHandler::drawFooter() {
    uint16_t y = gfx->height() - FOOTER_HEIGHT;

    // Draw footer background
    gfx->fillRect(0, y, gfx->width(), FOOTER_HEIGHT, HEADER_COLOR);

    // Draw navigation buttons
    uint16_t buttonWidth = gfx->width() / 5;

    clearTouchZones();

    // Main button
    drawButton(0, y, buttonWidth, FOOTER_HEIGHT, "MAIN", currentPage == PAGE_MAIN ? SUCCESS_COLOR : HEADER_COLOR);
    addTouchZone(0, y, buttonWidth, y + FOOTER_HEIGHT, onMainButtonPress, "MAIN");

    // List button
    drawButton(buttonWidth, y, buttonWidth, FOOTER_HEIGHT, "LIST", currentPage == PAGE_LIST ? SUCCESS_COLOR : HEADER_COLOR);
    addTouchZone(buttonWidth, y, buttonWidth * 2, y + FOOTER_HEIGHT, onListButtonPress, "LIST");

    // Stats button
    drawButton(buttonWidth * 2, y, buttonWidth, FOOTER_HEIGHT, "STATS", currentPage == PAGE_STATS ? SUCCESS_COLOR : HEADER_COLOR);
    addTouchZone(buttonWidth * 2, y, buttonWidth * 3, y + FOOTER_HEIGHT, onStatsButtonPress, "STATS");

    // Settings button
    drawButton(buttonWidth * 3, y, buttonWidth, FOOTER_HEIGHT, "SET", currentPage == PAGE_SETTINGS ? SUCCESS_COLOR : HEADER_COLOR);
    addTouchZone(buttonWidth * 3, y, buttonWidth * 4, y + FOOTER_HEIGHT, onSettingsButtonPress, "SET");

    // Clear button
    drawButton(buttonWidth * 4, y, buttonWidth, FOOTER_HEIGHT, "CLR", ALERT_COLOR);
    addTouchZone(buttonWidth * 4, y, gfx->width(), y + FOOTER_HEIGHT, onClearButtonPress, "CLR");
}

void DisplayHandler::drawMainPage() {
    uint16_t yPos = HEADER_HEIGHT + 20;

    gfx->setTextSize(2);
    gfx->setTextDatum(TL_DATUM);

    // Show scanning status
    gfx->setTextColor(SUCCESS_COLOR, BG_COLOR);
    gfx->drawString("Status: SCANNING", 10, yPos);
    yPos += 30;

    // Show detection summary
    gfx->setTextColor(TEXT_COLOR, BG_COLOR);
    gfx->drawString("Total: " + String(totalDetections), 10, yPos);
    yPos += 25;

    gfx->setTextColor(ALERT_COLOR, BG_COLOR);
    gfx->drawString("Flock: " + String(flockDetections), 10, yPos);
    yPos += 25;

    gfx->setTextColor(INFO_COLOR, BG_COLOR);
    gfx->drawString("BLE: " + String(bleDetections), 10, yPos);
    yPos += 40;

    // Show recent detection if available
    if (!detections.empty()) {
        Detection& latest = detections.back();

        gfx->setTextSize(1);
        gfx->setTextColor(WARNING_COLOR, BG_COLOR);
        gfx->drawString("LATEST DETECTION:", 10, yPos);
        yPos += 20;

        gfx->setTextColor(TEXT_COLOR, BG_COLOR);
        gfx->drawString("SSID: " + latest.ssid, 10, yPos);
        yPos += 15;

        gfx->drawString("MAC: " + latest.mac, 10, yPos);
        yPos += 15;

        gfx->drawString("RSSI: " + String(latest.rssi) + " dBm", 10, yPos);
        yPos += 15;

        // Draw signal strength indicator
        drawSignalStrength(200, yPos - 15, latest.rssi);
    }
}

void DisplayHandler::drawListPage() {
    uint16_t yPos = HEADER_HEIGHT + 10;
    uint16_t listHeight = gfx->height() - HEADER_HEIGHT - FOOTER_HEIGHT - 20;
    uint16_t maxItems = listHeight / LIST_ITEM_HEIGHT;

    gfx->setTextSize(1);
    gfx->setTextDatum(TL_DATUM);

    // Calculate starting index for pagination
    uint16_t startIdx = 0;
    if (detections.size() > maxItems) {
        startIdx = detections.size() - maxItems;
    }

    // Draw detection list
    for (size_t i = startIdx; i < detections.size() && i < startIdx + maxItems; i++) {
        Detection& det = detections[i];

        // Draw background for alternating rows
        if ((i - startIdx) % 2 == 0) {
            gfx->fillRect(5, yPos - 2, gfx->width() - 10, LIST_ITEM_HEIGHT - 2, 0x1082);
        }

        // Set color based on detection type
        if (det.type.indexOf("flock") >= 0) {
            gfx->setTextColor(ALERT_COLOR, BG_COLOR);
        } else if (det.type == "BLE") {
            gfx->setTextColor(INFO_COLOR, BG_COLOR);
        } else {
            gfx->setTextColor(TEXT_COLOR, BG_COLOR);
        }

        // Draw detection info
        String line1 = det.ssid.substring(0, 20);
        if (det.ssid.length() > 20) line1 += "...";
        gfx->drawString(line1, 10, yPos);

        String line2 = det.mac + " [" + String(det.rssi) + "dBm]";
        gfx->setTextColor(TEXT_COLOR, BG_COLOR);
        gfx->drawString(line2, 10, yPos + 15);

        // Draw signal indicator
        drawSignalStrength(gfx->width() - 40, yPos + 8, det.rssi);

        yPos += LIST_ITEM_HEIGHT;
    }

    // Show scroll indicator if needed
    if (detections.size() > maxItems) {
        gfx->setTextColor(WARNING_COLOR, BG_COLOR);
        gfx->setTextDatum(BC_DATUM);
        gfx->drawString("Showing " + String(maxItems) + " of " + String(detections.size()), gfx->width() / 2, gfx->height() - FOOTER_HEIGHT - 5);
    }
}

void DisplayHandler::drawStatsPage() {
    uint16_t yPos = HEADER_HEIGHT + 20;

    gfx->setTextSize(2);
    gfx->setTextDatum(TL_DATUM);
    gfx->setTextColor(TEXT_COLOR, BG_COLOR);

    gfx->drawString("STATISTICS", 10, yPos);
    yPos += 35;

    gfx->setTextSize(1);

    // Total detections
    gfx->setTextColor(SUCCESS_COLOR, BG_COLOR);
    gfx->drawString("Total Detections: " + String(totalDetections), 10, yPos);
    yPos += 20;

    // Flock detections
    gfx->setTextColor(ALERT_COLOR, BG_COLOR);
    gfx->drawString("Flock Cameras: " + String(flockDetections), 10, yPos);
    if (totalDetections > 0) {
        float percentage = (flockDetections * 100.0) / totalDetections;
        gfx->drawString("(" + String(percentage, 1) + "%)", 200, yPos);
    }
    yPos += 20;

    // BLE detections
    gfx->setTextColor(INFO_COLOR, BG_COLOR);
    gfx->drawString("BLE Devices: " + String(bleDetections), 10, yPos);
    if (totalDetections > 0) {
        float percentage = (bleDetections * 100.0) / totalDetections;
        gfx->drawString("(" + String(percentage, 1) + "%)", 200, yPos);
    }
    yPos += 20;

    // WiFi detections
    uint32_t wifiDetections = totalDetections - bleDetections;
    gfx->setTextColor(WARNING_COLOR, BG_COLOR);
    gfx->drawString("WiFi Devices: " + String(wifiDetections), 10, yPos);
    if (totalDetections > 0) {
        float percentage = (wifiDetections * 100.0) / totalDetections;
        gfx->drawString("(" + String(percentage, 1) + "%)", 200, yPos);
    }
    yPos += 30;

    // Draw progress bars
    gfx->setTextColor(TEXT_COLOR, BG_COLOR);
    gfx->drawString("Detection Distribution:", 10, yPos);
    yPos += 20;

    if (totalDetections > 0) {
        // Flock progress bar
        float flockProgress = (float)flockDetections / totalDetections;
        drawProgressBar(10, yPos, gfx->width() - 20, 20, flockProgress, ALERT_COLOR);
        yPos += 25;

        // BLE progress bar
        float bleProgress = (float)bleDetections / totalDetections;
        drawProgressBar(10, yPos, gfx->width() - 20, 20, bleProgress, INFO_COLOR);
        yPos += 25;

        // WiFi progress bar
        float wifiProgress = (float)wifiDetections / totalDetections;
        drawProgressBar(10, yPos, gfx->width() - 20, 20, wifiProgress, WARNING_COLOR);
    }
}

void DisplayHandler::drawSettingsPage() {
    uint16_t yPos = HEADER_HEIGHT + 20;

    gfx->setTextSize(2);
    gfx->setTextDatum(TL_DATUM);
    gfx->setTextColor(TEXT_COLOR, BG_COLOR);

    gfx->drawString("SETTINGS", 10, yPos);
    yPos += 35;

    gfx->setTextSize(1);

    // Buzzer setting
    gfx->setTextColor(TEXT_COLOR, BG_COLOR);
    gfx->drawString("Audio Alerts:", 10, yPos);
    drawButton(150, yPos - 5, 60, 25, "ON", SUCCESS_COLOR);
    yPos += 35;

    // Brightness setting
    gfx->drawString("Brightness:", 10, yPos);
    drawProgressBar(150, yPos, 100, 15, brightness / 255.0, INFO_COLOR);
    yPos += 35;

    // Scan interval
    gfx->drawString("Scan Speed:", 10, yPos);
    drawButton(150, yPos - 5, 60, 25, "FAST", WARNING_COLOR);
    yPos += 35;

    // Auto-clear
    gfx->drawString("Auto Clear:", 10, yPos);
    drawButton(150, yPos - 5, 60, 25, "OFF", ALERT_COLOR);
    yPos += 35;

    // Display rotation
    gfx->drawString("Rotation:", 10, yPos);
    drawButton(150, yPos - 5, 60, 25, String(TFT_ROTATION), INFO_COLOR);
}

void DisplayHandler::drawAboutPage() {
    uint16_t yPos = HEADER_HEIGHT + 20;

    gfx->setTextSize(2);
    gfx->setTextDatum(TC_DATUM);
    gfx->setTextColor(TEXT_COLOR, BG_COLOR);

    gfx->drawString("FLOCK YOU", gfx->width() / 2, yPos);
    yPos += 30;

    gfx->setTextSize(1);
    gfx->setTextColor(INFO_COLOR, BG_COLOR);
    gfx->drawString("CYD Edition v1.0", gfx->width() / 2, yPos);
    yPos += 25;

    gfx->setTextColor(TEXT_COLOR, BG_COLOR);
    gfx->drawString("Surveillance Detection System", gfx->width() / 2, yPos);
    yPos += 20;

    gfx->drawString("for ESP32-2432S035C", gfx->width() / 2, yPos);
    yPos += 30;

    gfx->setTextColor(WARNING_COLOR, BG_COLOR);
    gfx->drawString("Hardware:", gfx->width() / 2, yPos);
    yPos += 20;

    gfx->setTextColor(TEXT_COLOR, BG_COLOR);
    gfx->drawString("ESP32-WROOM-32", gfx->width() / 2, yPos);
    yPos += 15;
    gfx->drawString("3.5\" ILI9488 480x320", gfx->width() / 2, yPos);
    yPos += 15;
    gfx->drawString("XPT2046 Touch Controller", gfx->width() / 2, yPos);
    yPos += 30;

    gfx->setTextColor(SUCCESS_COLOR, BG_COLOR);
    gfx->drawString("Detecting:", gfx->width() / 2, yPos);
    yPos += 20;

    gfx->setTextColor(TEXT_COLOR, BG_COLOR);
    gfx->drawString("Flock Safety Cameras", gfx->width() / 2, yPos);
    yPos += 15;
    gfx->drawString("Surveillance Devices", gfx->width() / 2, yPos);
    yPos += 15;
    gfx->drawString("BLE Beacons", gfx->width() / 2, yPos);
}

void DisplayHandler::drawButton(uint16_t x, uint16_t y, uint16_t w, uint16_t h, String label, uint16_t color) {
    gfx->fillRect(x, y, w, h, color);
    gfx->drawRect(x, y, w, h, TEXT_COLOR);

    gfx->setTextDatum(MC_DATUM);
    gfx->setTextSize(1);
    gfx->setTextColor(TEXT_COLOR);
    gfx->drawString(label, x + w / 2, y + h / 2);
}

void DisplayHandler::drawProgressBar(uint16_t x, uint16_t y, uint16_t w, uint16_t h, float progress, uint16_t color) {
    // Draw border
    gfx->drawRect(x, y, w, h, TEXT_COLOR);

    // Draw fill
    uint16_t fillWidth = (uint16_t)(w * progress);
    if (fillWidth > 2) {
        gfx->fillRect(x + 1, y + 1, fillWidth - 2, h - 2, color);
    }

    // Draw percentage text
    gfx->setTextDatum(MC_DATUM);
    gfx->setTextSize(1);
    gfx->setTextColor(TEXT_COLOR);
    gfx->drawString(String((int)(progress * 100)) + "%", x + w / 2, y + h / 2);
}

void DisplayHandler::drawSignalStrength(uint16_t x, uint16_t y, int8_t rssi) {
    uint16_t color;
    uint8_t bars;

    if (rssi >= -50) {
        color = SUCCESS_COLOR;
        bars = 4;
    } else if (rssi >= -60) {
        color = SUCCESS_COLOR;
        bars = 3;
    } else if (rssi >= -70) {
        color = WARNING_COLOR;
        bars = 2;
    } else if (rssi >= -80) {
        color = WARNING_COLOR;
        bars = 1;
    } else {
        color = ALERT_COLOR;
        bars = 0;
    }

    // Draw signal bars
    for (uint8_t i = 0; i < 4; i++) {
        uint16_t barHeight = 3 + (i * 3);
        uint16_t barY = y + (12 - barHeight);

        if (i < bars) {
            gfx->fillRect(x + (i * 7), barY, 5, barHeight, color);
        } else {
            gfx->drawRect(x + (i * 7), barY, 5, barHeight, 0x4208);
        }
    }
}

void DisplayHandler::handleTouch() {
    if (!touchReadScreentouched()) return;

    TS_Point p = getTouchPoint();

    // Check touch zones
    for (auto& zone : touchZones) {
        if (p.x >= zone.x1 && p.x <= zone.x2 && p.y >= zone.y1 && p.y <= zone.y2) {
            if (zone.callback) {
                zone.callback();
            }
            break;
        }
    }
}

TS_Point DisplayHandler::getTouchPoint() {
    TS_Point p = touchReadScreengetPoint();

    // Calibrate and map touch coordinates to display coordinates
    int16_t x = map(p.x, TOUCH_MIN_X, TOUCH_MAX_X, 0, gfx->width());
    int16_t y = map(p.y, TOUCH_MIN_Y, TOUCH_MAX_Y, 0, gfx->height());

    // Constrain to display bounds
    x = constrain(x, 0, gfx->width() - 1);
    y = constrain(y, 0, gfx->height() - 1);

    p.x = x;
    p.y = y;

    return p;
}

void DisplayHandler::addTouchZone(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, void (*callback)(), String label) {
    TouchZone zone;
    zone.x1 = x1;
    zone.y1 = y1;
    zone.x2 = x2;
    zone.y2 = y2;
    zone.callback = callback;
    zone.label = label;
    touchZones.push_back(zone);
}

void DisplayHandler::clearTouchZones() {
    touchZones.clear();
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

    if (type.indexOf("flock") >= 0 || type.indexOf("Penguin") >= 0) {
        flockDetections++;
    }

    if (type == "BLE") {
        bleDetections++;
    }

    needsRedraw = true;
}

void DisplayHandler::clearDetections() {
    detections.clear();
    totalDetections = 0;
    flockDetections = 0;
    bleDetections = 0;
    clear();
}

void DisplayHandler::showAlert(String message, uint16_t color) {
    gfx->fillRect(10, gfx->height() / 2 - 30, gfx->width() - 20, 60, color);
    gfx->drawRect(10, gfx->height() / 2 - 30, gfx->width() - 20, 60, TEXT_COLOR);

    gfx->setTextDatum(MC_DATUM);
    gfx->setTextSize(2);
    gfx->setTextColor(TEXT_COLOR);
    gfx->drawString(message, gfx->width() / 2, gfx->height() / 2);

    delay(2000);
    needsRedraw = true;
}

void DisplayHandler::showInfo(String message) {
    showAlert(message, INFO_COLOR);
}

void DisplayHandler::showProgress(String message, float progress) {
    uint16_t y = gfx->height() / 2;

    gfx->fillRect(10, y - 40, gfx->width() - 20, 80, BG_COLOR);
    gfx->drawRect(10, y - 40, gfx->width() - 20, 80, TEXT_COLOR);

    gfx->setTextDatum(MC_DATUM);
    gfx->setTextSize(1);
    gfx->setTextColor(TEXT_COLOR);
    gfx->drawString(message, gfx->width() / 2, y - 20);

    drawProgressBar(20, y, gfx->width() - 40, 20, progress, SUCCESS_COLOR);
}

void DisplayHandler::updateStatus(String status) {
    gfx->fillRect(0, HEADER_HEIGHT, gfx->width(), 20, BG_COLOR);
    gfx->setTextDatum(TC_DATUM);
    gfx->setTextSize(1);
    gfx->setTextColor(INFO_COLOR, BG_COLOR);
    gfx->drawString(status, gfx->width() / 2, HEADER_HEIGHT + 2);
}

void DisplayHandler::updateChannelInfo(uint8_t channel) {
    String info = "Channel: " + String(channel);
    updateStatus(info);
}

void DisplayHandler::updateScanStatus(bool isScanning) {
    if (isScanning) {
        updateStatus("SCANNING...");
    } else {
        updateStatus("IDLE");
    }
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
    // Note: Actual brightness control depends on hardware setup
    // Some CYD boards have backlight on fixed 3.3V
}

void DisplayHandler::sleep() {
    displayActive = false;
    gfx->writecommand(0x10); // Enter sleep mode
}

void DisplayHandler::wake() {
    displayActive = true;
    gfx->writecommand(0x11); // Exit sleep mode
    needsRedraw = true;
}

void DisplayHandler::setRotation(uint8_t rotation) {
    gfx->setRotation(rotation);
    touchReadScreensetRotation(rotation);
    clear();
}

void DisplayHandler::enableTouch(bool enable) {
    // Touch is always enabled in this implementation
}

void DisplayHandler::setUpdateInterval(uint32_t interval) {
    // Implement if needed
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
    // Make sure TFT releases MISO
    digitalWrite(TFT_CS, HIGH);

    SPI.beginTransaction(SPISettings(500000, MSBFIRST, SPI_MODE0));
    digitalWrite(TOUCH_CS, LOW);

    auto tr = [](uint8_t b) { return SPI.transfer(b); };

    uint16_t xs[5], ys[5], zs[5];
    for (int i = 0; i < 5; i++) {
        // Pressure Z1, Z2
        tr(0xB1);
        uint8_t h1 = tr(0x00), l1 = tr(0x00);
        tr(0xC1);
        uint8_t h2 = tr(0x00), l2 = tr(0x00);
        uint16_t z1 = ((uint16_t)h1 << 5) | (l1 >> 3);
        uint16_t z2 = ((uint16_t)h2 << 5) | (l2 >> 3);
        uint16_t zz = (z1 && z2) ? (z2 + (4095 - z1)) : 0;

        // X then Y
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

    if (z < 40) return false; // pressure gate
    if (rx == 0 || rx == 4095 || ry == 0 || ry == 4095) return false; // reject rails
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
    gfx->print("Touch each cross and hold still...");

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

        // Wait for touch down
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
    gfx->print("Calibration saved");
    delay(700);
}

#endif // CYD_DISPLAY