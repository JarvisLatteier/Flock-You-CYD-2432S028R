#ifdef CYD_DISPLAY

#include "display_handler_28.h"
#include <SPI.h>

// Global instance
DisplayHandler display;

// Touch callback implementations
void onMainButtonPress() { display.setPage(DisplayHandler::PAGE_MAIN); }
void onListButtonPress() { display.setPage(DisplayHandler::PAGE_LIST); }
void onStatsButtonPress() { display.setPage(DisplayHandler::PAGE_STATS); }
void onSettingsButtonPress() { display.setPage(DisplayHandler::PAGE_SETTINGS); }
void onClearButtonPress() { display.clearDetections(); }
void onBrightnessUp() { display.increaseBrightness(); }
void onBrightnessDown() { display.decreaseBrightness(); }
void onAutoBrightnessToggle() { display.toggleAutoBrightness(); }
void onRgbBrightnessUp() { display.increaseRgbBrightness(); }
void onRgbBrightnessDown() { display.decreaseRgbBrightness(); }
void onCalibratePress() { display.setPage(DisplayHandler::PAGE_CALIBRATE); }
void onCalibrateSave() {
    if (display.saveCalibration()) {
        display.setPage(DisplayHandler::PAGE_MAIN);
    }
}
void onLedAlertToggle() { display.toggleLedAlerts(); }

DisplayHandler::DisplayHandler() : tft(), touchSPI(HSPI) {
    needsRedraw = true;
    lastUpdate = 0;
    currentPage = PAGE_MAIN;
    brightness = 255;  // Start at 100%
    totalDetections = 0;
    flockDetections = 0;
    bleDetections = 0;
    lastTouchTime = 0;
    touchDebounce = false;
    currentChannel = 1;
    bleScanning = false;
    sdCardPresent = false;
    detectionsLogged = 0;
    autoBrightness = false;
    rgbBrightness = 128;  // 50% default
    lastLdrRead = 0;
    ledAlertsEnabled = true;  // LED alerts on by default
    // Touch calibration defaults
    touchRawYMin = TOUCH_RAW_Y_MIN_DEFAULT;
    touchRawYMax = TOUCH_RAW_Y_MAX_DEFAULT;
    touchRawXMin = TOUCH_RAW_X_MIN_DEFAULT;
    touchRawXMax = TOUCH_RAW_X_MAX_DEFAULT;
    calStep = 0;
}

bool DisplayHandler::begin() {
    // Setup backlight with PWM for brightness control
    setupBacklightPWM();

    // Initialize display (TFT_eSPI handles BGR via TFT_RGB_ORDER flag)
    tft.init();
    tft.setRotation(1);  // Landscape (320x240)
    tft.fillScreen(BG_COLOR);

    // Initialize touch on separate HSPI bus
    pinMode(TOUCH_CS, OUTPUT);
    digitalWrite(TOUCH_CS, HIGH);
    pinMode(TOUCH_IRQ, INPUT);  // GPIO36 is input-only, no pullup
    touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);

    // Initialize SD Card
    sdCardPresent = initSDCard();

    // Initialize LDR for auto brightness
    pinMode(LDR_PIN, INPUT);

    // Show splash screen
    tft.fillScreen(BG_DARK);

    // Main title
    tft.setTextColor(TEXT_COLOR);
    tft.setTextSize(3);
    tft.setCursor(65, 60);
    tft.print("FLOCK YOU");

    // Subtitle
    tft.setTextSize(2);
    tft.setTextColor(ACCENT_COLOR);
    tft.setCursor(45, 110);
    tft.print("Surveillance Detector");

    // Version
    tft.setTextSize(1);
    tft.setTextColor(TEXT_DIM);
    tft.setCursor(100, 160);
    tft.print("v2.0 - CYD Edition");

    // Animated loading dots
    tft.setTextColor(SUCCESS_COLOR);
    for (int i = 0; i < 3; i++) {
        tft.setCursor(130 + i * 20, 200);
        tft.print(".");
        delay(400);
    }

    delay(500);

    // Try to load calibration from SD card
    if (sdCardPresent && loadCalibration()) {
        // Calibration loaded, go to main page
        Serial.println("Touch calibration loaded from SD card");
        currentPage = PAGE_MAIN;
        clear();
    } else {
        // No calibration file, start in calibration mode
        Serial.println("No calibration file, starting calibration");
        currentPage = PAGE_CALIBRATE;
        startCalibration();
    }
    return true;
}

void DisplayHandler::update() {
    uint32_t now = millis();

    // Update auto brightness if enabled
    updateAutoBrightness();

    // Calibration mode has special touch handling - no UI interference
    if (currentPage == PAGE_CALIBRATE) {
        if (digitalRead(TOUCH_IRQ) == LOW && !touchDebounce) {
            handleCalibrationTouch();
            touchDebounce = true;
            lastTouchTime = now;
        }
        if (touchDebounce && (now - lastTouchTime > 300)) {
            touchDebounce = false;
        }
        return;  // Skip normal UI updates in calibration mode
    }

    // Check for touch input using IRQ (GPIO36 goes LOW when touched)
    if (digitalRead(TOUCH_IRQ) == LOW && !touchDebounce) {
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
        // Clear touch zones at start of redraw cycle
        clearTouchZones();

        // Clear content area before redrawing
        clearContentArea();

        // Draw header and footer first (footer adds nav touch zones)
        drawHeader();
        drawFooter();

        // Then draw page content (may add additional touch zones)
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

        needsRedraw = false;
        lastUpdate = now;
    }
}

void DisplayHandler::clear() {
    tft.fillScreen(BG_COLOR);
}

void DisplayHandler::clearContentArea() {
    // Clear only the content area (between header and footer)
    tft.fillRect(0, HEADER_HEIGHT, tft.width(), tft.height() - HEADER_HEIGHT - FOOTER_HEIGHT, BG_COLOR);
}

void DisplayHandler::drawHeader() {
    // Draw header background
    tft.fillRect(0, 0, tft.width(), HEADER_HEIGHT, HEADER_COLOR);

    // Left: Channel/Mode indicator
    tft.setTextSize(1);
    if (bleScanning) {
        // BLE mode indicator
        tft.setTextColor(BLE_COLOR);
        tft.setCursor(8, 8);
        tft.print("BLE");
        // Draw Bluetooth icon (simple)
        tft.fillCircle(20, 24, 4, BLE_COLOR);
    } else {
        // WiFi channel indicator
        tft.setTextColor(WIFI_COLOR);
        tft.setCursor(8, 6);
        tft.print("CH");
        tft.setTextSize(2);
        tft.setCursor(8, 18);
        if (currentChannel < 10) tft.print(" ");
        tft.print(currentChannel);
    }

    // Center: Title
    tft.setTextColor(TEXT_COLOR);
    tft.setTextSize(2);
    tft.setCursor(100, 10);
    tft.print("FLOCK YOU");

    // Right: Detection count with color coding
    tft.setTextSize(1);
    tft.setTextColor(TEXT_DIM);
    tft.setCursor(265, 6);
    tft.print("DET");

    // Detection number - color based on count
    tft.setTextSize(2);
    if (flockDetections > 0) {
        tft.setTextColor(ALERT_COLOR);  // Red if Flock detected
    } else if (totalDetections > 0) {
        tft.setTextColor(SUCCESS_COLOR);  // Green if any detections
    } else {
        tft.setTextColor(TEXT_DIM);  // Dim if no detections
    }
    tft.setCursor(265, 18);
    if (totalDetections < 100) tft.print(" ");
    if (totalDetections < 10) tft.print(" ");
    tft.print(totalDetections);
}

void DisplayHandler::drawFooter() {
    uint16_t y = tft.height() - FOOTER_HEIGHT;

    // Draw footer background
    tft.fillRect(0, y, tft.width(), FOOTER_HEIGHT, FOOTER_COLOR);

    // Draw navigation buttons (5 buttons)
    uint16_t buttonWidth = 62;
    uint16_t padding = 2;
    uint16_t startX = 3;

    // Button labels: HOME, LIST, STAT, CONF, CAL
    const char* labels[] = {"HOME", "LIST", "STAT", "CONF", "CAL"};
    void (*callbacks[])() = {onMainButtonPress, onListButtonPress, onStatsButtonPress,
                             onSettingsButtonPress, onCalibratePress};

    for (int i = 0; i < 5; i++) {
        uint16_t x = startX + i * (buttonWidth + padding);
        uint16_t btnY = y + 3;
        uint16_t btnH = FOOTER_HEIGHT - 6;

        // Determine button color
        uint16_t bgColor, textColor;
        if (i == currentPage && i < 4) {  // CAL (4) doesn't have a page
            bgColor = BUTTON_ACTIVE;
            textColor = ACCENT_COLOR;
        } else if (i == 4) {  // CAL - yellow
            bgColor = 0x4200;  // Dark yellow
            textColor = TFT_YELLOW;
        } else {
            bgColor = FOOTER_COLOR;
            textColor = TEXT_DIM;
        }

        // Draw button
        tft.fillRect(x, btnY, buttonWidth, btnH, bgColor);
        if (i == currentPage && i < 4) {
            tft.drawRect(x, btnY, buttonWidth, btnH, ACCENT_COLOR);
        }

        // Draw label centered
        tft.setTextColor(textColor);
        tft.setTextSize(1);
        int16_t textX = x + (buttonWidth - strlen(labels[i]) * 6) / 2;
        int16_t textY = btnY + (btnH - 8) / 2;
        tft.setCursor(textX, textY);
        tft.print(labels[i]);

        // Touch zone: shift down 3px, extend to screen bottom
        addTouchZone(x, btnY + 3, x + buttonWidth, tft.height(), callbacks[i], labels[i]);
    }
}

void DisplayHandler::drawMainPage() {
    uint16_t yPos = HEADER_HEIGHT + 4;

    // === Compact Stats Row ===
    tft.setTextSize(1);

    // Total
    tft.setTextColor(TEXT_DIM);
    tft.setCursor(10, yPos);
    tft.print("ALL:");
    tft.setTextColor(TEXT_COLOR);
    tft.printf("%d", totalDetections);

    // Flock
    tft.setTextColor((flockDetections > 0) ? ALERT_COLOR : TEXT_DIM);
    tft.setCursor(80, yPos);
    tft.print("FLOCK:");
    tft.printf("%d", flockDetections);

    // BLE
    tft.setTextColor(BLE_COLOR);
    tft.setCursor(170, yPos);
    tft.print("BLE:");
    tft.setTextColor(TEXT_COLOR);
    tft.printf("%d", bleDetections);

    yPos += 14;

    // === Latest Detection Panel ===
    uint16_t panelHeight = 70;
    if (!detections.empty()) {
        Detection& latest = detections.back();
        bool isThreat = (latest.type.indexOf("flock") >= 0 || latest.type.indexOf("Flock") >= 0 ||
                        latest.type.indexOf("Penguin") >= 0);

        tft.fillRect(5, yPos, 310, panelHeight, BG_DARK);
        tft.drawRect(5, yPos, 310, panelHeight, isThreat ? ALERT_COLOR : ACCENT_COLOR);

        tft.setTextColor(isThreat ? ALERT_COLOR : ACCENT_COLOR);
        tft.setCursor(10, yPos + 4);
        tft.print(isThreat ? "! THREAT" : "LATEST");

        // SSID
        tft.setTextColor(TEXT_COLOR);
        tft.setCursor(70, yPos + 4);
        tft.print(latest.ssid.substring(0, 24));

        // MAC
        tft.setTextColor(TEXT_DIM);
        tft.setCursor(10, yPos + 18);
        tft.print(latest.mac);

        // RSSI
        tft.setCursor(10, yPos + 32);
        tft.setTextColor(TEXT_COLOR);
        tft.printf("%ddBm", latest.rssi);
        drawSignalStrength(80, yPos + 30, latest.rssi);

        // Type
        tft.setTextColor(latest.type == "BLE" ? BLE_COLOR : WIFI_COLOR);
        tft.setCursor(260, yPos + 32);
        tft.print(latest.type == "BLE" ? "BLE" : "WiFi");
    } else {
        tft.fillRect(5, yPos, 310, panelHeight, BG_DARK);
        tft.drawRect(5, yPos, 310, panelHeight, TEXT_DIM);
        tft.setTextColor(TEXT_DIM);
        tft.setCursor(100, yPos + 20);
        tft.print("No detections yet");
    }

    yPos += panelHeight + 8;

    // === LED Key ===
    tft.fillRect(5, yPos, 310, 45, BG_DARK);
    tft.drawRect(5, yPos, 310, 45, TEXT_DIM);

    // Green = Scanning
    tft.fillCircle(20, yPos + 12, 5, SUCCESS_COLOR);
    tft.setTextColor(TEXT_COLOR);
    tft.setCursor(30, yPos + 9);
    tft.print("Scanning");

    // Red flash = Detection
    tft.fillCircle(110, yPos + 12, 5, ALERT_COLOR);
    tft.setCursor(120, yPos + 9);
    tft.print("Detection");

    // Orange = Alert
    tft.fillCircle(210, yPos + 12, 5, ALERT_WARN);
    tft.setCursor(220, yPos + 9);
    tft.print("Alert");

    // Details
    tft.setTextColor(TEXT_DIM);
    tft.setCursor(10, yPos + 27);
    tft.print("Flash speed increases with signal strength");
}

void DisplayHandler::drawListPage() {
    uint16_t yPos = HEADER_HEIGHT + 5;
    uint16_t listHeight = tft.height() - HEADER_HEIGHT - FOOTER_HEIGHT - 10;
    uint16_t maxItems = listHeight / LIST_ITEM_HEIGHT;

    tft.setTextSize(1);

    if (detections.empty()) {
        tft.fillRect(5, 80, 310, 50, BG_DARK);
        tft.drawRect(5, 80, 310, 50, TEXT_DIM);
        tft.setTextColor(TEXT_DIM);
        tft.setCursor(85, 100);
        tft.print("No detections yet...");
        return;
    }

    // Calculate starting index for pagination (show newest first)
    uint16_t startIdx = 0;
    if (detections.size() > maxItems) {
        startIdx = detections.size() - maxItems;
    }

    // Draw detection list
    for (size_t i = startIdx; i < detections.size() && i < startIdx + maxItems; i++) {
        Detection& det = detections[i];
        bool isThreat = (det.type.indexOf("flock") >= 0 || det.type.indexOf("Flock") >= 0 ||
                        det.type.indexOf("Penguin") >= 0);
        bool isBLE = (det.type == "BLE");

        // Row background with left color indicator
        uint16_t rowColor = ((i - startIdx) % 2 == 0) ? BG_DARK : BG_COLOR;
        tft.fillRect(2, yPos, tft.width() - 4, LIST_ITEM_HEIGHT - 1, rowColor);

        // Left color bar indicator
        uint16_t indicatorColor = isThreat ? ALERT_COLOR : (isBLE ? BLE_COLOR : WIFI_COLOR);
        tft.fillRect(2, yPos, 3, LIST_ITEM_HEIGHT - 1, indicatorColor);

        // SSID/Name
        tft.setTextColor(isThreat ? ALERT_COLOR : TEXT_COLOR);
        tft.setCursor(8, yPos + 3);
        String ssidTrunc = det.ssid.substring(0, 22);
        tft.print(ssidTrunc);

        // MAC and RSSI on second line
        tft.setTextColor(TEXT_DIM);
        tft.setCursor(8, yPos + 13);
        tft.print(det.mac.substring(0, 17));

        // RSSI value
        tft.setTextColor(TEXT_COLOR);
        tft.setCursor(260, yPos + 8);
        tft.print(det.rssi);

        // Mini signal bars
        drawSignalStrength(290, yPos + 6, det.rssi);

        yPos += LIST_ITEM_HEIGHT;
    }

    // Show count at bottom
    tft.setTextColor(TEXT_DIM);
    tft.setCursor(5, tft.height() - FOOTER_HEIGHT - 12);
    tft.print("Showing ");
    tft.print(min((size_t)maxItems, detections.size()));
    tft.print(" of ");
    tft.print(detections.size());
}

void DisplayHandler::drawStatsPage() {
    uint16_t yPos = HEADER_HEIGHT + 10;

    tft.setTextSize(2);
    tft.setTextColor(TEXT_COLOR, BG_COLOR);
    tft.setCursor(100, yPos);
    tft.print("STATS");
    yPos += 30;

    tft.setTextSize(1);

    // Total detections
    tft.setTextColor(SUCCESS_COLOR, BG_COLOR);
    tft.setCursor(10, yPos);
    tft.print("Total: ");
    tft.print(totalDetections);
    yPos += 18;

    // Flock detections
    tft.setTextColor(ALERT_COLOR, BG_COLOR);
    tft.setCursor(10, yPos);
    tft.print("Flock: ");
    tft.print(flockDetections);
    if (totalDetections > 0) {
        tft.print(" (");
        tft.print((int)(flockDetections * 100 / totalDetections));
        tft.print("%)");
    }
    yPos += 18;

    // BLE detections
    tft.setTextColor(INFO_COLOR, BG_COLOR);
    tft.setCursor(10, yPos);
    tft.print("BLE: ");
    tft.print(bleDetections);
    if (totalDetections > 0) {
        tft.print(" (");
        tft.print((int)(bleDetections * 100 / totalDetections));
        tft.print("%)");
    }
    yPos += 18;

    // WiFi detections
    uint32_t wifiDetections = totalDetections - bleDetections;
    tft.setTextColor(WARNING_COLOR, BG_COLOR);
    tft.setCursor(10, yPos);
    tft.print("WiFi: ");
    tft.print(wifiDetections);
    yPos += 25;

    // Draw progress bars
    if (totalDetections > 0) {
        tft.setTextColor(TEXT_COLOR, BG_COLOR);
        tft.setCursor(10, yPos);
        tft.print("Distribution:");
        yPos += 15;

        // Flock progress bar
        float flockProgress = (float)flockDetections / totalDetections;
        drawProgressBar(10, yPos, 300, 15, flockProgress, ALERT_COLOR);
        yPos += 20;

        // BLE progress bar
        float bleProgress = (float)bleDetections / totalDetections;
        drawProgressBar(10, yPos, 300, 15, bleProgress, INFO_COLOR);
        yPos += 25;
    } else {
        yPos = HEADER_HEIGHT + 120;
    }

    // Clear button
    uint16_t clrX = 110, clrW = 100, clrH = 28;
    tft.fillRect(clrX, yPos, clrW, clrH, 0x4000);  // Dark red
    tft.drawRect(clrX, yPos, clrW, clrH, ALERT_COLOR);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(clrX + 14, yPos + 6);
    tft.print("CLEAR");
    addTouchZone(clrX, yPos, clrX + clrW, yPos + clrH, onClearButtonPress, "CLR");
}

void DisplayHandler::drawSettingsPage() {
    uint16_t yPos = HEADER_HEIGHT + 5;
    uint16_t btnW = 40;
    uint16_t btnH = 24;

    // Title
    tft.setTextSize(2);
    tft.setTextColor(ACCENT_COLOR);
    tft.setCursor(100, yPos);
    tft.print("SETTINGS");
    yPos += 28;

    tft.setTextSize(1);

    // === Display Brightness Control ===
    tft.setTextColor(TEXT_COLOR);
    tft.setCursor(10, yPos + 6);
    tft.print("Display:");

    // Current percentage
    int pct = (brightness * 100) / 255;
    tft.setCursor(70, yPos + 6);
    tft.setTextColor(ACCENT_COLOR);
    tft.printf("%3d%%", pct);

    // Minus button
    uint16_t minusBtnX = 110;
    tft.fillRect(minusBtnX, yPos, btnW, btnH, BG_DARK);
    tft.drawRect(minusBtnX, yPos, btnW, btnH, ACCENT_COLOR);
    tft.setTextColor(ACCENT_COLOR);
    tft.setTextSize(2);
    tft.setCursor(minusBtnX + 14, yPos + 4);
    tft.print("-");
    addTouchZone(minusBtnX, yPos, minusBtnX + btnW, yPos + btnH, onBrightnessDown, "BR-");

    // Plus button
    uint16_t plusBtnX = 155;
    tft.fillRect(plusBtnX, yPos, btnW, btnH, BG_DARK);
    tft.drawRect(plusBtnX, yPos, btnW, btnH, ACCENT_COLOR);
    tft.setTextColor(ACCENT_COLOR);
    tft.setCursor(plusBtnX + 14, yPos + 4);
    tft.print("+");
    addTouchZone(plusBtnX, yPos, plusBtnX + btnW, yPos + btnH, onBrightnessUp, "BR+");

    // Auto brightness toggle button
    uint16_t autoBtnX = 210;
    uint16_t autoBtnW = 95;
    uint16_t autoColor = autoBrightness ? SUCCESS_COLOR : TEXT_DIM;
    tft.fillRect(autoBtnX, yPos, autoBtnW, btnH, autoBrightness ? 0x0320 : BG_DARK);
    tft.drawRect(autoBtnX, yPos, autoBtnW, btnH, autoColor);
    tft.setTextColor(autoColor);
    tft.setTextSize(1);
    tft.setCursor(autoBtnX + 8, yPos + 8);
    tft.print(autoBrightness ? "AUTO: ON" : "AUTO: OFF");
    addTouchZone(autoBtnX, yPos, autoBtnX + autoBtnW, yPos + btnH, onAutoBrightnessToggle, "AUTO");

    yPos += 32;

    // === RGB LED Brightness Control ===
    tft.setTextSize(1);
    tft.setTextColor(TEXT_COLOR);
    tft.setCursor(10, yPos + 6);
    tft.print("RGB LED:");

    // Current percentage
    int rgbPct = (rgbBrightness * 100) / 255;
    tft.setCursor(70, yPos + 6);
    tft.setTextColor(ALERT_COLOR);
    tft.printf("%3d%%", rgbPct);

    // Minus button
    tft.fillRect(minusBtnX, yPos, btnW, btnH, BG_DARK);
    tft.drawRect(minusBtnX, yPos, btnW, btnH, ALERT_COLOR);
    tft.setTextColor(ALERT_COLOR);
    tft.setTextSize(2);
    tft.setCursor(minusBtnX + 14, yPos + 4);
    tft.print("-");
    addTouchZone(minusBtnX, yPos, minusBtnX + btnW, yPos + btnH, onRgbBrightnessDown, "RGB-");

    // Plus button
    tft.fillRect(plusBtnX, yPos, btnW, btnH, BG_DARK);
    tft.drawRect(plusBtnX, yPos, btnW, btnH, ALERT_COLOR);
    tft.setTextColor(ALERT_COLOR);
    tft.setCursor(plusBtnX + 14, yPos + 4);
    tft.print("+");
    addTouchZone(plusBtnX, yPos, plusBtnX + btnW, yPos + btnH, onRgbBrightnessUp, "RGB+");

    // LED Alert toggle button (replaces RGB preview bar)
    uint16_t ledBtnX = 210, ledBtnW = 95;
    uint16_t ledColor = ledAlertsEnabled ? SUCCESS_COLOR : ALERT_COLOR;
    tft.fillRect(ledBtnX, yPos, ledBtnW, btnH, ledAlertsEnabled ? 0x0320 : 0x4000);
    tft.drawRect(ledBtnX, yPos, ledBtnW, btnH, ledColor);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);
    tft.setCursor(ledBtnX + 8, yPos + 8);
    tft.print(ledAlertsEnabled ? "LED: ON" : "LED: OFF");
    addTouchZone(ledBtnX, yPos, ledBtnX + ledBtnW, yPos + btnH, onLedAlertToggle, "LED");

    yPos += 35;

    // === SD Card Status ===
    tft.fillRect(5, yPos, 310, 40, BG_DARK);
    tft.drawRect(5, yPos, 310, 40, sdCardPresent ? SUCCESS_COLOR : TEXT_DIM);

    tft.setTextColor(sdCardPresent ? SUCCESS_COLOR : ALERT_COLOR);
    tft.setTextSize(1);
    tft.setCursor(10, yPos + 5);
    tft.print("SD Card: ");
    tft.print(sdCardPresent ? "READY" : "NOT FOUND");

    if (sdCardPresent) {
        tft.setTextColor(TEXT_DIM);
        tft.setCursor(10, yPos + 18);
        tft.print("Logged: ");
        tft.setTextColor(TEXT_COLOR);
        tft.print(detectionsLogged);
        tft.setTextColor(TEXT_DIM);
        tft.print(" | ");
        tft.setTextColor(TEXT_COLOR);
        tft.print(logFileName);
    }
    yPos += 48;

    // Hardware Info
    tft.setTextColor(TEXT_DIM);
    tft.setTextSize(1);
    tft.setCursor(10, yPos);
    tft.print("ILI9341 320x240 | XPT2046 Touch");
}

void DisplayHandler::drawAboutPage() {
    uint16_t yPos = HEADER_HEIGHT + 15;

    tft.setTextSize(2);
    tft.setTextColor(TEXT_COLOR, BG_COLOR);
    tft.setCursor(60, yPos);
    tft.print("FLOCK YOU");
    yPos += 25;

    tft.setTextSize(1);
    tft.setTextColor(INFO_COLOR, BG_COLOR);
    tft.setCursor(70, yPos);
    tft.print("CYD 2.8\" Edition v1.0");
    yPos += 20;

    tft.setTextColor(TEXT_COLOR, BG_COLOR);
    tft.setCursor(40, yPos);
    tft.print("Surveillance Detection System");
    yPos += 15;

    tft.setCursor(60, yPos);
    tft.print("ESP32-2432S028R");
    yPos += 25;

    tft.setTextColor(WARNING_COLOR, BG_COLOR);
    tft.setCursor(100, yPos);
    tft.print("Hardware:");
    yPos += 15;

    tft.setTextColor(TEXT_COLOR, BG_COLOR);
    tft.setCursor(80, yPos);
    tft.print("ESP32-WROOM-32");
    yPos += 12;
    tft.setCursor(70, yPos);
    tft.print("2.8\" ILI9341 320x240");
    yPos += 12;
    tft.setCursor(70, yPos);
    tft.print("XPT2046 Touch");
}

void DisplayHandler::startCalibration() {
    calStep = 0;
    for (int i = 0; i < 4; i++) {
        calRawX[i] = 0;
        calRawY[i] = 0;
    }
    drawCalibrationPage();
}

void DisplayHandler::drawCalibrationPage() {
    tft.fillScreen(TFT_BLACK);

    uint16_t targetSize = 20;
    uint16_t margin = 25;
    uint16_t trX = 319 - margin;
    uint16_t blY = 239 - margin;

    // Target positions: TL, TR, BL, BR
    uint16_t targetX[] = {margin, trX, margin, trX};
    uint16_t targetY[] = {margin, margin, blY, blY};
    const char* targetLabels[] = {"TOP-LEFT", "TOP-RIGHT", "BOTTOM-LEFT", "BOTTOM-RIGHT"};

    // Draw all targets dimmed
    for (int i = 0; i < 4; i++) {
        uint16_t color = (i < calStep) ? TFT_DARKGREEN : 0x4208;  // Done=green, waiting=dim
        if (i == calStep) color = TFT_YELLOW;  // Current target = yellow

        tft.drawLine(targetX[i] - targetSize, targetY[i], targetX[i] + targetSize, targetY[i], color);
        tft.drawLine(targetX[i], targetY[i] - targetSize, targetX[i], targetY[i] + targetSize, color);

        if (i == calStep) {
            tft.fillCircle(targetX[i], targetY[i], 5, TFT_YELLOW);
        }
    }

    // Title
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);
    tft.setCursor(85, 80);
    tft.print("TOUCH CALIBRATION");

    // Instructions based on step
    tft.setTextSize(2);
    if (calStep < 4) {
        tft.setTextColor(TFT_YELLOW);
        tft.setCursor(60, 100);
        tft.printf("Tap %s", targetLabels[calStep]);

        // Progress indicator
        tft.setTextSize(1);
        tft.setTextColor(TFT_WHITE);
        tft.setCursor(130, 125);
        tft.printf("Step %d/4", calStep + 1);
    } else {
        tft.setTextColor(TFT_GREEN);
        tft.setCursor(45, 100);
        tft.print("Calibration OK!");

        tft.setTextSize(1);
        tft.setTextColor(TFT_WHITE);
        tft.setCursor(100, 125);
        tft.print("Tap SAVE to apply");
    }

    // Button row at bottom (above the corner targets)
    uint16_t btnW = 90, btnH = 28, btnY = 150;

    // CANCEL button (always visible, left side)
    uint16_t cancelX = 40;
    tft.fillRect(cancelX, btnY, btnW, btnH, 0x4000);  // Dark red
    tft.drawRect(cancelX, btnY, btnW, btnH, TFT_RED);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(cancelX + 8, btnY + 6);
    tft.print("CANCEL");

    // SAVE button (only when done, right side)
    uint16_t saveX = 190;
    if (calStep >= 4) {
        tft.fillRect(saveX, btnY, btnW, btnH, 0x0320);  // Dark green
        tft.drawRect(saveX, btnY, btnW, btnH, TFT_GREEN);
        tft.setTextColor(TFT_WHITE);
        tft.setCursor(saveX + 18, btnY + 6);
        tft.print("SAVE");
    } else {
        // Dimmed placeholder
        tft.fillRect(saveX, btnY, btnW, btnH, 0x2104);  // Very dark gray
        tft.drawRect(saveX, btnY, btnW, btnH, 0x4208);
        tft.setTextColor(0x4208);
        tft.setCursor(saveX + 18, btnY + 6);
        tft.print("SAVE");
    }

    clearTouchZones();
}

void DisplayHandler::processCalibrationTouch(uint16_t rawX, uint16_t rawY) {
    if (calStep < 4) {
        // Store raw values for this corner
        calRawX[calStep] = rawX;
        calRawY[calStep] = rawY;
        calStep++;

        if (calStep >= 4) {
            // All corners captured, validate
            if (!validateAndApplyCalibration()) {
                // Invalid calibration, show error and restart
                tft.fillRect(50, 150, 220, 30, TFT_RED);
                tft.setTextColor(TFT_WHITE);
                tft.setTextSize(1);
                tft.setCursor(60, 160);
                tft.print("Bad calibration! Restarting...");
                delay(2000);
                startCalibration();
                return;
            }
        }
        drawCalibrationPage();
    }
}

bool DisplayHandler::validateAndApplyCalibration() {
    // Known good ranges from testing (with some tolerance)
    const uint16_t RAW_Y_MIN_VALID = 200;   // Screen X maps from RAW_Y
    const uint16_t RAW_Y_MAX_VALID = 4000;
    const uint16_t RAW_X_MIN_VALID = 200;   // Screen Y maps from RAW_X
    const uint16_t RAW_X_MAX_VALID = 4000;
    const uint16_t MIN_RANGE = 2000;        // Minimum span required

    // TL=0, TR=1, BL=2, BR=3
    // RAW_Y: left corners (0,2) should be low, right corners (1,3) should be high
    // RAW_X: top corners (0,1) should be low, bottom corners (2,3) should be high

    uint16_t rawYMin = min(calRawY[0], calRawY[2]);  // Left side
    uint16_t rawYMax = max(calRawY[1], calRawY[3]);  // Right side
    uint16_t rawXMin = min(calRawX[0], calRawX[1]);  // Top side
    uint16_t rawXMax = max(calRawX[2], calRawX[3]);  // Bottom side

    Serial.printf("Calibration values: Y(%d-%d) X(%d-%d)\n", rawYMin, rawYMax, rawXMin, rawXMax);

    // Validate ranges
    if (rawYMin < RAW_Y_MIN_VALID || rawYMax > RAW_Y_MAX_VALID ||
        rawXMin < RAW_X_MIN_VALID || rawXMax > RAW_X_MAX_VALID) {
        Serial.println("Calibration failed: values out of valid range");
        return false;
    }

    if ((rawYMax - rawYMin) < MIN_RANGE || (rawXMax - rawXMin) < MIN_RANGE) {
        Serial.println("Calibration failed: range too small");
        return false;
    }

    // Apply calibration
    touchRawYMin = rawYMin;
    touchRawYMax = rawYMax;
    touchRawXMin = rawXMin;
    touchRawXMax = rawXMax;

    Serial.println("Calibration validated and applied");
    return true;
}

void DisplayHandler::handleCalibrationTouch() {
    uint16_t rawX, rawY;
    if (!readTouchRaw(rawX, rawY)) return;

    // Calculate approximate screen position for button detection
    int16_t screenX = map(rawY, touchRawYMin, touchRawYMax, 0, 319);
    int16_t screenY = map(rawX, touchRawXMin, touchRawXMax, 0, 239);
    screenX = constrain(screenX, 0, 319);
    screenY = constrain(screenY, 0, 239);

    // Button row is at Y=150, height=28 (so Y range 150-178)
    // CANCEL button: X=40, width=90 (so X range 40-130)
    // SAVE button: X=190, width=90 (so X range 190-280)

    if (screenY >= 145 && screenY <= 185) {
        // CANCEL button
        if (screenX >= 30 && screenX <= 140) {
            setPage(PAGE_MAIN);
            return;
        }

        // SAVE button (only active when calibration complete)
        if (calStep >= 4 && screenX >= 180 && screenX <= 290) {
            if (saveCalibration()) {
                setPage(PAGE_MAIN);
            }
            return;
        }
    }

    // If calibration still in progress, process corner touches
    if (calStep < 4) {
        processCalibrationTouch(rawX, rawY);
    }
}

void DisplayHandler::drawProgressBar(uint16_t x, uint16_t y, uint16_t w, uint16_t h, float progress, uint16_t color) {
    // Draw border
    tft.drawRect(x, y, w, h, TEXT_COLOR);

    // Draw fill
    uint16_t fillWidth = (uint16_t)(w * progress);
    if (fillWidth > 2) {
        tft.fillRect(x + 1, y + 1, fillWidth - 2, h - 2, color);
    }

    // Draw percentage text
    tft.setTextColor(TEXT_COLOR);
    tft.setTextSize(1);
    tft.setCursor(x + w / 2 - 10, y + 2);
    tft.print((int)(progress * 100));
    tft.print("%");
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
            tft.fillRect(x + (i * 6), barY, 4, barHeight, color);
        } else {
            tft.drawRect(x + (i * 6), barY, 4, barHeight, 0x4208);
        }
    }
}

// Read raw touch values from XPT2046 on HSPI
bool DisplayHandler::readTouchRaw(uint16_t &rawX, uint16_t &rawY) {
    // Check IRQ first - LOW means touched
    if (digitalRead(TOUCH_IRQ) == HIGH) return false;

    touchSPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
    digitalWrite(TOUCH_CS, LOW);

    // Read X (command 0xD0) and Y (command 0x90)
    // Multiple samples for averaging
    uint32_t sumX = 0, sumY = 0;
    const int samples = 4;

    for (int i = 0; i < samples; i++) {
        touchSPI.transfer(0x90);  // Y command
        uint8_t y_msb = touchSPI.transfer(0x00);
        uint8_t y_lsb = touchSPI.transfer(0xD0);  // X command
        uint8_t x_msb = touchSPI.transfer(0x00);
        uint8_t x_lsb = touchSPI.transfer(0x00);

        sumY += ((y_msb << 8) | y_lsb) >> 3;
        sumX += ((x_msb << 8) | x_lsb) >> 3;
    }

    digitalWrite(TOUCH_CS, HIGH);
    touchSPI.endTransaction();

    rawX = sumX / samples;
    rawY = sumY / samples;

    // Validate readings (reject obviously bad values)
    if (rawX < 100 || rawX > 4000 || rawY < 100 || rawY > 4000) return false;

    return true;
}

// Map raw touch coordinates to screen coordinates
void DisplayHandler::mapTouchToScreen(uint16_t rawX, uint16_t rawY, int16_t &screenX, int16_t &screenY) {
    // Axes are swapped: RAW_Y -> Screen X, RAW_X -> Screen Y

    // Map RAW_Y to Screen X (0-319)
    screenX = map(rawY, touchRawYMin, touchRawYMax, 0, 319);

    // Map RAW_X to Screen Y (0-239)
    screenY = map(rawX, touchRawXMin, touchRawXMax, 0, 239);

    // Clamp to screen bounds
    screenX = constrain(screenX, 0, 319);
    screenY = constrain(screenY, 0, 239);
}

void DisplayHandler::handleTouch() {
    uint16_t rawX, rawY;
    if (!readTouchRaw(rawX, rawY)) return;

    int16_t screenX, screenY;
    mapTouchToScreen(rawX, rawY, screenX, screenY);

    // Check touch zones
    for (auto& zone : touchZones) {
        if (screenX >= zone.x1 && screenX <= zone.x2 &&
            screenY >= zone.y1 && screenY <= zone.y2) {
            if (zone.callback) {
                zone.callback();
            }
            break;
        }
    }
}

void DisplayHandler::addTouchZone(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, void (*callback)(), const char* label) {
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
    if (detections.size() > 50) {
        detections.erase(detections.begin());
    }

    totalDetections++;

    if (type.indexOf("flock") >= 0 || type.indexOf("Flock") >= 0 ||
        type.indexOf("Penguin") >= 0 || type.indexOf("Pigvision") >= 0) {
        flockDetections++;
    }

    if (type == "BLE") {
        bleDetections++;
    }

    // Log to SD card
    logDetection(ssid, mac, rssi, type);

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
    tft.fillRect(10, tft.height() / 2 - 25, tft.width() - 20, 50, color);
    tft.drawRect(10, tft.height() / 2 - 25, tft.width() - 20, 50, TEXT_COLOR);

    tft.setTextColor(TEXT_COLOR);
    tft.setTextSize(2);
    tft.setCursor(tft.width() / 2 - message.length() * 6, tft.height() / 2 - 8);
    tft.print(message);

    delay(2000);
    needsRedraw = true;
}

void DisplayHandler::showInfo(String message) {
    showAlert(message, INFO_COLOR);
}

void DisplayHandler::setPage(DisplayPage page) {
    currentPage = page;
    clear();
    // Calibration page needs special setup
    if (page == PAGE_CALIBRATE) {
        startCalibration();
    }
}

void DisplayHandler::nextPage() {
    currentPage = (DisplayPage)((currentPage + 1) % 5);
    clear();
}

void DisplayHandler::previousPage() {
    currentPage = (DisplayPage)((currentPage + 4) % 5);
    clear();
}

void DisplayHandler::setupBacklightPWM() {
    // Use LEDC for backlight PWM
    // Both backlight pins must be driven together
    // Using channels 3 and 4 (0-2 used by RGB LED in main.cpp)
    ledcSetup(3, 2000, 8);  // Channel 3, 2kHz, 8-bit
    ledcSetup(4, 2000, 8);  // Channel 4, 2kHz, 8-bit
    ledcAttachPin(TFT_BL, 3);  // GPIO 27 on channel 3
    ledcAttachPin(21, 4);       // GPIO 21 on channel 4
    applyBrightness();
    Serial.printf("Backlight PWM initialized, brightness=%d\n", brightness);
}

void DisplayHandler::applyBrightness() {
    ledcWrite(3, brightness);  // Channel 3 = GPIO 27
    ledcWrite(4, brightness);  // Channel 4 = GPIO 21
}

void DisplayHandler::setBrightness(uint8_t level) {
    brightness = level;
    applyBrightness();
    needsRedraw = true;
}

void DisplayHandler::increaseBrightness() {
    if (brightness <= 230) {
        brightness += 25;  // ~10% of 255
    } else {
        brightness = 255;
    }
    applyBrightness();
    needsRedraw = true;
}

void DisplayHandler::decreaseBrightness() {
    if (brightness >= 50) {
        brightness -= 25;  // ~10% of 255
    } else {
        brightness = 25;  // Minimum 10%
    }
    autoBrightness = false;  // Manual adjustment disables auto
    applyBrightness();
    needsRedraw = true;
}

void DisplayHandler::toggleAutoBrightness() {
    autoBrightness = !autoBrightness;
    if (autoBrightness) {
        updateAutoBrightness();  // Apply immediately
    }
    needsRedraw = true;
}

void DisplayHandler::updateAutoBrightness() {
    if (!autoBrightness) return;

    uint32_t now = millis();
    // Only read LDR every 500ms to avoid flicker
    if (now - lastLdrRead < 500) return;
    lastLdrRead = now;

    // Read LDR value (higher = brighter ambient light)
    int ldrValue = analogRead(LDR_PIN);

    // Map LDR reading to brightness (0-4095 ADC to 25-255 brightness)
    // Invert: low light = low brightness, high light = high brightness
    uint8_t newBrightness = map(ldrValue, 0, 4095, 25, 255);

    // Smooth the transition to avoid jarring changes
    if (abs(newBrightness - brightness) > 10) {
        if (newBrightness > brightness) {
            brightness = min(255, brightness + 5);
        } else {
            brightness = max(25, brightness - 5);
        }
        applyBrightness();
    }
}

// === RGB Brightness Control ===
void DisplayHandler::setRgbBrightness(uint8_t level) {
    rgbBrightness = level;
    needsRedraw = true;
}

void DisplayHandler::increaseRgbBrightness() {
    if (rgbBrightness <= 230) {
        rgbBrightness += 25;
    } else {
        rgbBrightness = 255;
    }
    needsRedraw = true;
}

void DisplayHandler::decreaseRgbBrightness() {
    if (rgbBrightness >= 50) {
        rgbBrightness -= 25;
    } else {
        rgbBrightness = 25;  // Minimum 10%
    }
    needsRedraw = true;
}

void DisplayHandler::toggleLedAlerts() {
    ledAlertsEnabled = !ledAlertsEnabled;
    needsRedraw = true;
}

// === SD Card Functions ===
bool DisplayHandler::initSDCard() {
    // SD card uses VSPI (same as display) with CS on GPIO 5
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);

    if (!SD.begin(SD_CS)) {
        Serial.println("SD Card: Mount failed or not present");
        return false;
    }

    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("SD Card: No card detected");
        return false;
    }

    // Create log filename with timestamp placeholder
    logFileName = "/flockyou_detections.csv";

    // Check if file exists, if not create with header
    if (!SD.exists(logFileName)) {
        File file = SD.open(logFileName, FILE_WRITE);
        if (file) {
            file.println("timestamp,ssid,mac,rssi,type");
            file.close();
            Serial.println("SD Card: Created log file");
        }
    }

    Serial.printf("SD Card: Initialized, type=%d, size=%lluMB\n", cardType, SD.cardSize() / (1024 * 1024));
    return true;
}

void DisplayHandler::logDetection(const String& ssid, const String& mac, int8_t rssi, const String& type) {
    if (!sdCardPresent) return;

    File file = SD.open(logFileName, FILE_APPEND);
    if (file) {
        // Format: timestamp,ssid,mac,rssi,type
        file.printf("%lu,\"%s\",%s,%d,%s\n",
            millis() / 1000,  // seconds since boot
            ssid.c_str(),
            mac.c_str(),
            rssi,
            type.c_str()
        );
        file.close();
        detectionsLogged++;
    }
}

bool DisplayHandler::loadCalibration() {
    if (!sdCardPresent) return false;
    if (!SD.exists(TOUCH_CAL_FILE)) return false;

    File file = SD.open(TOUCH_CAL_FILE, FILE_READ);
    if (!file) return false;

    // Read 4 calibration values (one per line)
    String line;
    int values[4] = {0};
    int idx = 0;

    while (file.available() && idx < 4) {
        line = file.readStringUntil('\n');
        line.trim();
        if (line.length() > 0) {
            values[idx++] = line.toInt();
        }
    }
    file.close();

    if (idx == 4) {
        touchRawYMin = values[0];
        touchRawYMax = values[1];
        touchRawXMin = values[2];
        touchRawXMax = values[3];
        Serial.printf("Calibration loaded: Y(%d-%d) X(%d-%d)\n",
            touchRawYMin, touchRawYMax, touchRawXMin, touchRawXMax);
        return true;
    }
    return false;
}

bool DisplayHandler::saveCalibration() {
    if (!sdCardPresent) {
        Serial.println("Cannot save calibration: no SD card");
        return false;
    }

    File file = SD.open(TOUCH_CAL_FILE, FILE_WRITE);
    if (!file) {
        Serial.println("Cannot save calibration: file open failed");
        return false;
    }

    // Write 4 calibration values (one per line)
    file.println(touchRawYMin);
    file.println(touchRawYMax);
    file.println(touchRawXMin);
    file.println(touchRawXMax);
    file.close();

    Serial.printf("Calibration saved: Y(%d-%d) X(%d-%d)\n",
        touchRawYMin, touchRawYMax, touchRawXMin, touchRawXMax);
    return true;
}

void DisplayHandler::updateChannelInfo(uint8_t channel) {
    currentChannel = channel;
    bleScanning = false;  // Channel updates mean WiFi scanning
    // Header will be redrawn on next update cycle
}

void DisplayHandler::updateScanMode(bool isBLE) {
    bleScanning = isBLE;
    needsRedraw = true;
}

void DisplayHandler::updateScanStatus(bool isScanning) {
    needsRedraw = true;
}

void DisplayHandler::showDebugSSID(String ssid, int8_t rssi, uint8_t channel) {
    currentChannel = channel;
    bleScanning = false;
    (void)ssid;
    (void)rssi;
}

void DisplayHandler::showDebugBLE(String name, String mac, int8_t rssi) {
    bleScanning = true;
    (void)name;
    (void)mac;
    (void)rssi;
}

#endif // CYD_DISPLAY
