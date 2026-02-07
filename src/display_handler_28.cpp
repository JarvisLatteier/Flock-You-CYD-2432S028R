/**
 * @file display_handler_28.cpp
 * @brief Touchscreen UI implementation for ESP32-2432S028R
 *
 * Implements the DisplayHandler class for the 2.8" CYD board with ILI9341
 * display and XPT2046 touch controller.
 *
 * @see display_handler_28.h for class definition and hardware notes
 */

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
void onBrightnessMax() { display.setBrightness(255); }
void onSoundVolumeMax() { display.setSoundVolume(255); }
void onRgbBrightnessMax() { display.setRgbBrightness(255); }
void onCalibratePress() { display.setPage(DisplayHandler::PAGE_CALIBRATE); }
void onCalibrateSave() {
    if (display.saveCalibration()) {
        display.setPage(DisplayHandler::PAGE_MAIN);
    }
}
void onLedAlertToggle() { display.toggleLedAlerts(); }
void onSoundToggle() { display.toggleSound(); }
void onSoundVolumeUp() { display.increaseSoundVolume(); }
void onSoundVolumeDown() { display.decreaseSoundVolume(); }

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
    lastSdCheck = 0;
    autoBrightness = false;
    rgbBrightness = 128;  // 50% default
    lastLdrRead = 0;
    ledAlertsEnabled = true;  // LED alerts on by default
    soundEnabled = true;      // Sound on by default
    soundVolume = 128;        // 50% default
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

    // Load saved settings from SD card
    if (sdCardPresent) {
        loadSettings();
    }

    // Initialize LDR for auto brightness
    pinMode(LDR_PIN, INPUT);

    // Initialize speaker
    setupSpeaker();

    // Play boot tone
    playBootTone();

    // === STREAMLINED BOOT SCREEN ===
    tft.fillScreen(BG_COLOR);

    // Draw header-style splash area (gray bar at top like the main UI)
    tft.fillRect(0, 0, 320, 70, HEADER_COLOR);
    tft.drawFastHLine(0, 69, 320, TEXT_COLOR);

    // "FLOCK YOU" title with glitch-in effect
    tft.setTextSize(4);
    const char* title = "FLOCK YOU";
    int titleX = 16;  // Centered for size 4: (320 - 9*24) / 2 = 28, adjusted
    int titleY = 18;

    // Quick glitch effect
    for (int g = 0; g < 3; g++) {
        tft.setTextColor(g % 2 ? LOGO_COLOR : ALERT_COLOR);
        tft.setCursor(titleX + (g % 2 ? 2 : -2), titleY);
        tft.print(title);
        delay(60);
        tft.fillRect(10, 10, 300, 50, HEADER_COLOR);
    }

    // Final title
    tft.setTextColor(LOGO_COLOR);
    tft.setCursor(titleX, titleY);
    tft.print(title);

    delay(150);

    // Tagline (matching new header style)
    tft.setTextSize(1);
    tft.setTextColor(LOGO_COLOR);
    tft.setCursor(97, 55);
    tft.print("Surveillance Detector");

    delay(300);

    // Status messages in main content area
    const char* messages[] = {
        "Initializing WiFi...",
        "Starting BLE scanner...",
        "Loading detection patterns...",
        "System ready"
    };

    tft.setTextSize(1);
    int msgY = 85;
    for (int i = 0; i < 4; i++) {
        // Draw progress indicator
        tft.setTextColor(LOGO_COLOR);
        tft.setCursor(20, msgY + i * 22);
        tft.print(">");

        // Print message
        tft.setTextColor(TEXT_COLOR);
        tft.setCursor(35, msgY + i * 22);
        tft.print(messages[i]);

        delay(200);

        // Show checkmark
        tft.setTextColor(SUCCESS_COLOR);
        tft.setCursor(280, msgY + i * 22);
        tft.print("[OK]");

        delay(100);
    }

    // SD card status
    tft.setTextColor(LOGO_COLOR);
    tft.setCursor(20, msgY + 4 * 22);
    tft.print(">");
    tft.setTextColor(TEXT_COLOR);
    tft.setCursor(35, msgY + 4 * 22);
    tft.print("SD Card: ");
    tft.setTextColor(sdCardPresent ? SUCCESS_COLOR : ALERT_COLOR);
    tft.print(sdCardPresent ? "OK" : "Not found");

    delay(400);

    // Final "SCANNING" splash
    tft.fillRect(0, 70, 320, 170, BG_COLOR);

    // Draw a simple scanning animation
    tft.setTextSize(3);
    tft.setTextColor(SUCCESS_COLOR);
    tft.setCursor(70, 110);
    tft.print("SCANNING");

    // Animated dots
    for (int d = 0; d < 3; d++) {
        tft.setTextColor(SUCCESS_COLOR);
        tft.setCursor(232 + d * 18, 110);
        tft.print(".");
        delay(200);
    }

    tft.setTextSize(1);
    tft.setTextColor(TEXT_DIM);
    tft.setCursor(75, 150);
    tft.print("Looking for surveillance devices...");

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

    // Check SD card status periodically (every 5 seconds)
    checkSDCard();

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

        // Note: Don't clear content area - each draw function fills its own background
        // This prevents screen flashing on updates

        // CONFIG page has its own custom layout (no header)
        // Other pages use the standard header/status bar
        if (currentPage != PAGE_SETTINGS) {
            drawHeader();
            drawStatusBar();
            drawFooter();
            // Draw LED status row for HOME page
            if (currentPage == PAGE_MAIN) {
                drawLedStatusRow();
            }
        }

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
            default:
                break;
        }

        needsRedraw = false;
        lastUpdate = now;
    }
}

void DisplayHandler::clear() {
    tft.fillScreen(BG_COLOR);
}

void DisplayHandler::drawHeader() {
    // Draw header background (gray)
    tft.fillRect(0, 0, tft.width(), HEADER_HEIGHT, HEADER_COLOR);
    // White border at bottom
    tft.drawFastHLine(0, HEADER_HEIGHT - 1, tft.width(), TEXT_COLOR);

    // Left: Channel/Mode indicator (only on main page)
    if (currentPage == PAGE_MAIN) {
        tft.setTextSize(1);
        if (bleScanning) {
            tft.setTextColor(BLE_COLOR);
            tft.setCursor(5, 10);
            tft.print("BLE");
            tft.setTextSize(2);
            tft.setCursor(5, 22);
            tft.print("SCAN");
        } else {
            tft.setTextColor(WIFI_COLOR);
            tft.setCursor(5, 8);
            tft.print("CH");
            tft.setTextSize(2);
            tft.setCursor(5, 20);
            if (currentChannel < 10) tft.print(" ");
            tft.print(currentChannel);
        }
    }

    // "FLOCK YOU" title - large orange text, centered
    tft.setTextColor(LOGO_COLOR);
    tft.setTextSize(3);
    // "FLOCK YOU" is 9 chars * 18px = 162px wide at size 3
    tft.setCursor((320 - 173) / 2, 8);
    tft.print("FLOCK<*>YOU");

    // Tagline below in smaller orange text
    tft.setTextSize(1);
    tft.setTextColor(LOGO_COLOR);
    // "Surveillance Detector" is 21 chars * 6px = 126px wide at size 1
    tft.setCursor((320 - 160) / 2, 40);
    tft.print("Scanning ALPRs <*> deflock.org");
}

void DisplayHandler::drawStatusBar() {
    uint16_t y = HEADER_HEIGHT;
    uint16_t barHeight = STATUS_BAR_HEIGHT;

    // Determine status bar content based on page and state
    uint16_t bgColor;
    const char* statusText = "";

    if (currentPage == PAGE_MAIN) {
        // Home page: show scanning or threat status
        if (!detections.empty()) {
            Detection& latest = detections.back();
            bool isThreat = (latest.type.indexOf("flock") >= 0 || latest.type.indexOf("Flock") >= 0 ||
                            latest.type.indexOf("Penguin") >= 0);
            if (isThreat) {
                bgColor = ALERT_WARN;  // Orange for threat
                // Draw bar
                tft.fillRect(0, y, 320, barHeight, bgColor);
                tft.setTextColor(TFT_BLACK);
                tft.setTextSize(1);
                tft.setCursor(90, y + 6);
                tft.printf("THREAT FOUND %ddBm", latest.rssi);
                return;
            }
        }
        // Default: scanning
        bgColor = SUCCESS_COLOR;
        statusText = "SCANNING";
    } else if (currentPage == PAGE_LIST) {
        bgColor = SUCCESS_COLOR;
        statusText = "Detection List";
    } else if (currentPage == PAGE_STATS) {
        bgColor = SUCCESS_COLOR;
        statusText = "Statistics";
    } else if (currentPage == PAGE_SETTINGS) {
        bgColor = SUCCESS_COLOR;
        statusText = "Config";
    } else {
        return;  // No status bar for calibration page
    }

    // Draw status bar
    tft.fillRect(0, y, 320, barHeight, bgColor);
    tft.setTextColor(TFT_BLACK);
    tft.setTextSize(1);
    // Center the text
    int textWidth = strlen(statusText) * 6;
    tft.setCursor((320 - textWidth) / 2, y + 6);
    tft.print(statusText);
}

void DisplayHandler::drawLedStatusRow() {
    // Draw LED status indicators and system info row above nav buttons
    uint16_t y = 240 - FOOTER_HEIGHT - LED_STATUS_HEIGHT;

    tft.fillRect(0, y, 320, LED_STATUS_HEIGHT, BG_COLOR);

    tft.setTextSize(1);

    // LED indicators: Scan, Detect, Alert
    // Green dot for Scan
    tft.fillCircle(10, y + 11, 4, SUCCESS_COLOR);
    tft.setTextColor(TEXT_DIM);
    tft.setCursor(18, y + 7);
    tft.print("Scan");

    // Red dot for Detect
    tft.fillCircle(60, y + 11, 4, ALERT_COLOR);
    tft.setCursor(68, y + 7);
    tft.print("Detect");

    // Orange dot for Alert
    tft.fillCircle(120, y + 11, 4, ALERT_WARN);
    tft.setCursor(128, y + 7);
    tft.print("Alert");

    // Divider
    tft.drawFastVLine(170, y + 4, 14, TEXT_DIM);

    // SD card status
    tft.setCursor(178, y + 7);
    tft.print("SD");
    if (sdCardPresent) {
        tft.fillCircle(198, y + 11, 4, SUCCESS_COLOR);
    } else {
        tft.fillCircle(198, y + 11, 4, ALERT_COLOR);
    }

    // OUI database status
    tft.setCursor(210, y + 7);
    tft.print("OUI");
    if (sdCardPresent && SD.exists(OUI_FILE)) {
        tft.fillCircle(236, y + 11, 4, SUCCESS_COLOR);
    } else {
        tft.fillCircle(236, y + 11, 4, TEXT_DIM);
    }

    // Uptime
    tft.setTextColor(TEXT_DIM);
    tft.setCursor(250, y + 7);
    tft.print("UP ");
    uint32_t uptime = millis() / 60000;  // minutes
    if (uptime < 60) {
        tft.printf("%dm", uptime);
    } else {
        tft.printf("%dh", uptime / 60);
    }
}

void DisplayHandler::drawFooter() {
    uint16_t y = tft.height() - FOOTER_HEIGHT;

    // Draw footer background (black)
    tft.fillRect(0, y, tft.width(), FOOTER_HEIGHT, FOOTER_COLOR);

    // Draw navigation buttons (4 buttons)
    uint16_t buttonWidth = 78;
    uint16_t padding = 2;
    uint16_t startX = 2;

    // Button labels: HOME, LIST, STATS, CONFIG
    const char* labels[] = {"HOME", "LIST", "STATS", "CONFIG"};
    void (*callbacks[])() = {onMainButtonPress, onListButtonPress, onStatsButtonPress,
                             onSettingsButtonPress};

    for (int i = 0; i < 4; i++) {
        uint16_t x = startX + i * (buttonWidth + padding);
        uint16_t btnY = y + 3;
        uint16_t btnH = FOOTER_HEIGHT - 6;

        // Determine button style
        bool isActive = (i == currentPage);
        uint16_t bgColor = isActive ? BUTTON_ACTIVE : BG_COLOR;
        uint16_t borderColor = BUTTON_BORDER;
        uint16_t textColor = TEXT_COLOR;

        // Draw button background
        tft.fillRect(x, btnY, buttonWidth, btnH, bgColor);
        // Draw white border
        tft.drawRect(x, btnY, buttonWidth, btnH, borderColor);

        // Draw label centered
        tft.setTextColor(textColor);
        tft.setTextSize(1);
        int16_t textX = x + (buttonWidth - strlen(labels[i]) * 6) / 2;
        int16_t textY = btnY + (btnH - 8) / 2;
        tft.setCursor(textX, textY);
        tft.print(labels[i]);

        // Touch zone
        addTouchZone(x, btnY, x + buttonWidth, tft.height(), callbacks[i], labels[i]);
    }
}

void DisplayHandler::drawMainPage() {
    // Content area: from below status bar to above LED status row
    uint16_t yStart = HEADER_HEIGHT + STATUS_BAR_HEIGHT;
    uint16_t yEnd = 240 - FOOTER_HEIGHT - LED_STATUS_HEIGHT;
    uint16_t contentHeight = yEnd - yStart;

    // Fill background
    tft.fillRect(0, yStart, 320, contentHeight, BG_COLOR);

    // Check for threat detection
    bool hasThreat = false;
    Detection* latestDetection = nullptr;
    if (!detections.empty()) {
        latestDetection = &detections.back();
        hasThreat = (latestDetection->type.indexOf("flock") >= 0 ||
                     latestDetection->type.indexOf("Flock") >= 0 ||
                     latestDetection->type.indexOf("Penguin") >= 0);
    }

    if (hasThreat && latestDetection != nullptr) {
        // === STATE 3: THREAT FOUND ===
        tft.fillRect(10, yStart + 5, 300, contentHeight - 10, PANEL_COLOR);
        tft.drawRect(10, yStart + 5, 300, contentHeight - 10, ALERT_COLOR);

        // Large vendor name in red, centered
        tft.setTextColor(ALERT_COLOR);
        tft.setTextSize(3);
        String vendorDisplay = latestDetection->vendor;
        if (vendorDisplay.length() > 12) vendorDisplay = vendorDisplay.substring(0, 12);
        int textWidth = vendorDisplay.length() * 18;
        tft.setCursor((320 - textWidth) / 2, yStart + 15);
        tft.print(vendorDisplay);

        // SSID below
        tft.setTextSize(2);
        tft.setTextColor(TEXT_COLOR);
        String ssidDisplay = latestDetection->ssid;
        if (ssidDisplay.length() > 18) ssidDisplay = ssidDisplay.substring(0, 18);
        textWidth = ssidDisplay.length() * 12;
        tft.setCursor((320 - textWidth) / 2, yStart + 42);
        tft.print(ssidDisplay);

        // MAC and signal info
        tft.setTextSize(1);
        tft.setTextColor(TEXT_DIM);
        tft.setCursor(20, yStart + 65);
        tft.print("MAC: ");
        tft.setTextColor(TEXT_COLOR);
        tft.print(latestDetection->mac);

        // Signal strength
        tft.setTextColor(TEXT_DIM);
        tft.setCursor(20, yStart + 80);
        tft.print("Signal: ");
        tft.setTextColor(TEXT_COLOR);
        tft.printf("%ddBm ", latestDetection->rssi);
        drawSignalStrength(100, yStart + 78, latestDetection->rssi);

    } else if (!detections.empty() && latestDetection != nullptr) {
        // === STATE 2: SCANNING with recent detection ===
        tft.fillRect(10, yStart + 5, 300, contentHeight - 10, PANEL_COLOR);
        tft.drawRect(10, yStart + 5, 300, contentHeight - 10, SUCCESS_COLOR);

        // Latest detection header
        tft.setTextColor(SUCCESS_COLOR);
        tft.setTextSize(1);
        tft.setCursor(20, yStart + 12);
        tft.print("LATEST DETECTION:");

        // SSID large
        tft.setTextSize(2);
        tft.setTextColor(TEXT_COLOR);
        tft.setCursor(20, yStart + 28);
        tft.print(latestDetection->ssid.substring(0, 18));

        // Vendor
        tft.setTextSize(1);
        tft.setTextColor(TEXT_DIM);
        tft.setCursor(20, yStart + 50);
        tft.print("Vendor: ");
        tft.setTextColor(latestDetection->vendor == "Flock Safety" ? ALERT_COLOR : ACCENT_COLOR);
        tft.print(latestDetection->vendor.substring(0, 18));

        // MAC
        tft.setTextColor(TEXT_DIM);
        tft.setCursor(20, yStart + 65);
        tft.print("MAC: ");
        tft.setTextColor(TEXT_COLOR);
        tft.print(latestDetection->mac);

        // Signal + stats
        tft.setTextColor(TEXT_DIM);
        tft.setCursor(20, yStart + 80);
        tft.printf("Signal: %ddBm", latestDetection->rssi);
        drawSignalStrength(120, yStart + 78, latestDetection->rssi);

        tft.setCursor(180, yStart + 80);
        tft.printf("Total: %d", totalDetections);

    } else {
        // === STATE 1: NO DETECTIONS ===
        tft.fillRect(10, yStart + 10, 300, contentHeight - 20, PANEL_COLOR);
        tft.drawRect(10, yStart + 10, 300, contentHeight - 20, TEXT_DIM);

        // Centered message
        tft.setTextColor(TEXT_DIM);
        tft.setTextSize(2);
        tft.setCursor(40, yStart + 35);
        tft.print("No Detections...");

        tft.setTextSize(1);
        tft.setCursor(65, yStart + 60);
        tft.print("Scanning for devices.");
    }
}

void DisplayHandler::drawListPage() {
    // Start below status bar - no LED row on this page, so more room
    uint16_t yPos = HEADER_HEIGHT + STATUS_BAR_HEIGHT + 2;
    // Calculate available list height (extends to just above footer)
    uint16_t listBottom = 240 - FOOTER_HEIGHT - 14;  // Leave room for count
    uint16_t listHeight = listBottom - yPos;
    uint16_t maxItems = listHeight / LIST_ITEM_HEIGHT;

    tft.setTextSize(1);

    if (detections.empty()) {
        uint16_t emptyY = HEADER_HEIGHT + STATUS_BAR_HEIGHT + 40;
        tft.fillRect(5, emptyY, 310, 50, PANEL_COLOR);
        tft.drawRect(5, emptyY, 310, 50, TEXT_DIM);
        tft.setTextColor(TEXT_DIM);
        tft.setCursor(85, emptyY + 20);
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
        uint16_t rowColor = ((i - startIdx) % 2 == 0) ? PANEL_COLOR : BG_COLOR;
        tft.fillRect(2, yPos, tft.width() - 4, LIST_ITEM_HEIGHT - 1, rowColor);

        // Left color bar indicator
        uint16_t indicatorColor = isThreat ? ALERT_COLOR : (isBLE ? BLE_COLOR : WIFI_COLOR);
        tft.fillRect(2, yPos, 3, LIST_ITEM_HEIGHT - 1, indicatorColor);

        // SSID/Name
        tft.setTextColor(isThreat ? ALERT_COLOR : TEXT_COLOR);
        tft.setCursor(8, yPos + 3);
        String ssidTrunc = det.ssid.substring(0, 22);
        tft.print(ssidTrunc);

        // Vendor or MAC on second line
        if (det.vendor != "Unknown") {
            tft.setTextColor(det.vendor == "Flock Safety" ? ALERT_COLOR : ACCENT_COLOR);
            tft.setCursor(8, yPos + 13);
            tft.print(det.vendor.substring(0, 20));
        } else {
            tft.setTextColor(TEXT_DIM);
            tft.setCursor(8, yPos + 13);
            tft.print(det.mac.substring(0, 17));
        }

        // RSSI value
        tft.setTextColor(TEXT_COLOR);
        tft.setCursor(260, yPos + 8);
        tft.print(det.rssi);

        // Mini signal bars
        drawSignalStrength(290, yPos + 6, det.rssi);

        yPos += LIST_ITEM_HEIGHT;
    }

    // Show count at bottom (just above footer)
    tft.setTextColor(TEXT_DIM);
    tft.setCursor(5, 240 - FOOTER_HEIGHT - 12);
    tft.print("Showing ");
    tft.print(min((size_t)maxItems, detections.size()));
    tft.print(" of ");
    tft.print(detections.size());
}

void DisplayHandler::drawStatsPage() {
    // Fill background for this page area
    uint16_t contentTop = HEADER_HEIGHT + STATUS_BAR_HEIGHT;
    uint16_t contentBottom = 240 - FOOTER_HEIGHT;
    tft.fillRect(0, contentTop, 320, contentBottom - contentTop, BG_COLOR);

    // Start below status bar - no LED row on this page
    uint16_t yPos = contentTop + 5;

    // Stats panel
    tft.fillRect(5, yPos, 310, 85, PANEL_COLOR);
    tft.drawRect(5, yPos, 310, 85, TEXT_DIM);

    // Total detections - larger text
    tft.setTextSize(2);
    tft.setTextColor(TEXT_DIM);
    tft.setCursor(10, yPos + 8);
    tft.print("Total:");
    tft.setTextColor(TEXT_COLOR);
    tft.setCursor(90, yPos + 8);
    tft.print(totalDetections);

    // Flock detections
    tft.setTextColor(ALERT_COLOR);
    tft.setCursor(10, yPos + 32);
    tft.print("Flock:");
    tft.setCursor(90, yPos + 32);
    tft.print(flockDetections);
    if (totalDetections > 0) {
        tft.setTextSize(1);
        tft.setCursor(140, yPos + 38);
        tft.printf("(%d%%)", (int)(flockDetections * 100 / totalDetections));
        tft.setTextSize(2);
    }

    // BLE detections
    tft.setTextColor(BLE_COLOR);
    tft.setCursor(170, yPos + 8);
    tft.print("BLE:");
    tft.setTextColor(TEXT_COLOR);
    tft.setCursor(230, yPos + 8);
    tft.print(bleDetections);

    // WiFi detections
    uint32_t wifiDetections = totalDetections - bleDetections;
    tft.setTextColor(WIFI_COLOR);
    tft.setCursor(170, yPos + 32);
    tft.print("WiFi:");
    tft.setTextColor(TEXT_COLOR);
    tft.setCursor(230, yPos + 32);
    tft.print(wifiDetections);

    // Draw progress bars
    tft.setTextSize(1);
    tft.setTextColor(TEXT_DIM);
    tft.setCursor(10, yPos + 55);
    tft.print("Distribution:");

    if (totalDetections > 0) {
        // Flock progress bar
        float flockProgress = (float)flockDetections / totalDetections;
        drawProgressBar(10, yPos + 68, 145, 12, flockProgress, ALERT_COLOR);

        // BLE progress bar
        float bleProgress = (float)bleDetections / totalDetections;
        drawProgressBar(160, yPos + 68, 145, 12, bleProgress, BLE_COLOR);
    } else {
        // Empty bars
        tft.drawRect(10, yPos + 68, 145, 12, TEXT_DIM);
        tft.drawRect(160, yPos + 68, 145, 12, TEXT_DIM);
    }

    // Clear button - positioned at bottom with proper spacing
    uint16_t clrW = 100, clrH = 28;
    uint16_t clrX = (320 - clrW) / 2;  // Center horizontally
    uint16_t clrY = contentBottom - clrH - 5;  // 5px above footer
    tft.fillRect(clrX, clrY, clrW, clrH, 0x4000);  // Dark red
    tft.drawRect(clrX, clrY, clrW, clrH, ALERT_COLOR);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    // "CLEAR" is 5 chars * 12px = 60px, center in 100px button: (100-60)/2 = 20
    tft.setCursor(clrX + 20, clrY + 6);
    tft.print("CLEAR");
    addTouchZone(clrX, clrY, clrX + clrW, clrY + clrH, onClearButtonPress, "CLR");
}

void DisplayHandler::drawSettingsPage() {
    // CONFIG page: no header, no LED key row
    // Layout: Title bar, SD Card panel, control rows, CALIBRATE button, footer
    uint16_t contentBottom = 240 - FOOTER_HEIGHT;  // 207

    // Title bar
    tft.fillRect(0, 0, 320, 22, HEADER_COLOR);
    tft.drawFastHLine(0, 21, 320, TEXT_COLOR);
    tft.setTextColor(LOGO_COLOR);
    tft.setTextSize(2);
    tft.setCursor(108, 3);
    tft.print("CONFIG");

    // Content area
    tft.fillRect(0, 22, 320, contentBottom - 22, BG_COLOR);

    uint16_t yPos = 26;

    // === SD Card Info Panel (at top) ===
    uint16_t sdPanelH = 32;
    tft.fillRect(5, yPos, 310, sdPanelH, PANEL_COLOR);
    tft.drawRect(5, yPos, 310, sdPanelH, sdCardPresent ? SUCCESS_COLOR : TEXT_DIM);

    tft.setTextColor(TEXT_COLOR);
    tft.setTextSize(1);
    tft.setCursor(10, yPos + 4);
    tft.print("SD Card & Files:");

    if (sdCardPresent) {
        tft.setTextColor(SUCCESS_COLOR);
        tft.setCursor(10, yPos + 18);
        tft.print("SD: OK");

        tft.setCursor(60, yPos + 18);
        if (SD.exists(TOUCH_CAL_FILE)) {
            tft.setTextColor(SUCCESS_COLOR);
            tft.print("cal: OK");
        } else {
            tft.setTextColor(ALERT_COLOR);
            tft.print("cal: MISS");
        }

        tft.setCursor(120, yPos + 18);
        if (SD.exists(OUI_FILE)) {
            tft.setTextColor(SUCCESS_COLOR);
            tft.print("oui: OK");
        } else {
            tft.setTextColor(TEXT_DIM);
            tft.print("oui: -");
        }

        tft.setTextColor(TEXT_DIM);
        tft.setCursor(180, yPos + 18);
        tft.printf("Log: %d", detectionsLogged);
    } else {
        tft.setTextColor(ALERT_COLOR);
        tft.setCursor(10, yPos + 18);
        tft.print("SD Card not present - Insert for logging");
    }

    yPos += sdPanelH + 8;

    // Control row dimensions
    uint16_t rowH = 28;
    uint16_t toggleW = 44;
    uint16_t btnW = 32;
    uint16_t btnH = 24;
    uint16_t maxW = 40;

    // === Row 1: Display Brightness ===
    uint16_t autoColor = autoBrightness ? SUCCESS_COLOR : TEXT_DIM;
    tft.fillRect(5, yPos, toggleW, btnH, autoBrightness ? 0x0320 : PANEL_COLOR);
    tft.drawRect(5, yPos, toggleW, btnH, autoColor);
    tft.setTextColor(TEXT_COLOR);
    tft.setTextSize(1);
    tft.setCursor(autoBrightness ? 9 : 12, yPos + 8);
    tft.print(autoBrightness ? "AUTO" : "MAN");
    addTouchZone(5, yPos, 5 + toggleW, yPos + btnH, onAutoBrightnessToggle, "AUTO");

    tft.setTextColor(TEXT_COLOR);
    tft.setCursor(55, yPos + 8);
    tft.print("Display:");

    int pct = (brightness * 100) / 255;
    tft.setTextColor(SLIDER_COLOR);
    tft.setTextSize(2);
    tft.setCursor(115, yPos + 4);
    tft.printf("%3d%%", pct);

    uint16_t minusX = 175;
    tft.fillRect(minusX, yPos, btnW, btnH, PANEL_COLOR);
    tft.drawRect(minusX, yPos, btnW, btnH, SLIDER_COLOR);
    tft.setTextColor(SLIDER_COLOR);
    tft.setCursor(minusX + 11, yPos + 4);
    tft.print("-");
    addTouchZone(minusX, yPos, minusX + btnW, yPos + btnH, onBrightnessDown, "BR-");

    uint16_t plusX = minusX + btnW + 4;
    tft.fillRect(plusX, yPos, btnW, btnH, PANEL_COLOR);
    tft.drawRect(plusX, yPos, btnW, btnH, SLIDER_COLOR);
    tft.setTextColor(SLIDER_COLOR);
    tft.setCursor(plusX + 11, yPos + 4);
    tft.print("+");
    addTouchZone(plusX, yPos, plusX + btnW, yPos + btnH, onBrightnessUp, "BR+");

    uint16_t maxX = plusX + btnW + 4;
    tft.fillRect(maxX, yPos, maxW, btnH, PANEL_COLOR);
    tft.drawRect(maxX, yPos, maxW, btnH, SLIDER_COLOR);
    tft.setTextColor(SLIDER_COLOR);
    tft.setTextSize(1);
    tft.setCursor(maxX + 8, yPos + 8);
    tft.print("MAX");
    addTouchZone(maxX, yPos, maxX + maxW, yPos + btnH, onBrightnessMax, "BRMAX");

    yPos += rowH;

    // === Row 2: Sound Volume ===
    uint16_t sndColor = soundEnabled ? SUCCESS_COLOR : ALERT_COLOR;
    tft.fillRect(5, yPos, toggleW, btnH, soundEnabled ? 0x0320 : 0x4000);
    tft.drawRect(5, yPos, toggleW, btnH, sndColor);
    tft.setTextColor(TEXT_COLOR);
    tft.setTextSize(1);
    tft.setCursor(soundEnabled ? 17 : 14, yPos + 8);
    tft.print(soundEnabled ? "ON" : "OFF");
    addTouchZone(5, yPos, 5 + toggleW, yPos + btnH, onSoundToggle, "SND");

    tft.setTextColor(TEXT_COLOR);
    tft.setCursor(55, yPos + 8);
    tft.print("Sound:");

    int sndPct = (soundVolume * 100) / 255;
    tft.setTextColor(ACCENT_COLOR);
    tft.setTextSize(2);
    tft.setCursor(115, yPos + 4);
    tft.printf("%3d%%", sndPct);

    tft.fillRect(minusX, yPos, btnW, btnH, PANEL_COLOR);
    tft.drawRect(minusX, yPos, btnW, btnH, ACCENT_COLOR);
    tft.setTextColor(ACCENT_COLOR);
    tft.setCursor(minusX + 11, yPos + 4);
    tft.print("-");
    addTouchZone(minusX, yPos, minusX + btnW, yPos + btnH, onSoundVolumeDown, "SND-");

    tft.fillRect(plusX, yPos, btnW, btnH, PANEL_COLOR);
    tft.drawRect(plusX, yPos, btnW, btnH, ACCENT_COLOR);
    tft.setTextColor(ACCENT_COLOR);
    tft.setCursor(plusX + 11, yPos + 4);
    tft.print("+");
    addTouchZone(plusX, yPos, plusX + btnW, yPos + btnH, onSoundVolumeUp, "SND+");

    tft.fillRect(maxX, yPos, maxW, btnH, PANEL_COLOR);
    tft.drawRect(maxX, yPos, maxW, btnH, ACCENT_COLOR);
    tft.setTextColor(ACCENT_COLOR);
    tft.setTextSize(1);
    tft.setCursor(maxX + 8, yPos + 8);
    tft.print("MAX");
    addTouchZone(maxX, yPos, maxX + maxW, yPos + btnH, onSoundVolumeMax, "SNDMAX");

    yPos += rowH;

    // === Row 3: LED Brightness ===
    uint16_t ledColor = ledAlertsEnabled ? SUCCESS_COLOR : ALERT_COLOR;
    tft.fillRect(5, yPos, toggleW, btnH, ledAlertsEnabled ? 0x0320 : 0x4000);
    tft.drawRect(5, yPos, toggleW, btnH, ledColor);
    tft.setTextColor(TEXT_COLOR);
    tft.setTextSize(1);
    tft.setCursor(ledAlertsEnabled ? 17 : 14, yPos + 8);
    tft.print(ledAlertsEnabled ? "ON" : "OFF");
    addTouchZone(5, yPos, 5 + toggleW, yPos + btnH, onLedAlertToggle, "LED");

    tft.setTextColor(TEXT_COLOR);
    tft.setCursor(55, yPos + 8);
    tft.print("LED:");

    int rgbPct = (rgbBrightness * 100) / 255;
    tft.setTextColor(ALERT_WARN);
    tft.setTextSize(2);
    tft.setCursor(115, yPos + 4);
    tft.printf("%3d%%", rgbPct);

    tft.fillRect(minusX, yPos, btnW, btnH, PANEL_COLOR);
    tft.drawRect(minusX, yPos, btnW, btnH, ALERT_WARN);
    tft.setTextColor(ALERT_WARN);
    tft.setCursor(minusX + 11, yPos + 4);
    tft.print("-");
    addTouchZone(minusX, yPos, minusX + btnW, yPos + btnH, onRgbBrightnessDown, "RGB-");

    tft.fillRect(plusX, yPos, btnW, btnH, PANEL_COLOR);
    tft.drawRect(plusX, yPos, btnW, btnH, ALERT_WARN);
    tft.setTextColor(ALERT_WARN);
    tft.setCursor(plusX + 11, yPos + 4);
    tft.print("+");
    addTouchZone(plusX, yPos, plusX + btnW, yPos + btnH, onRgbBrightnessUp, "RGB+");

    tft.fillRect(maxX, yPos, maxW, btnH, PANEL_COLOR);
    tft.drawRect(maxX, yPos, maxW, btnH, ALERT_WARN);
    tft.setTextColor(ALERT_WARN);
    tft.setTextSize(1);
    tft.setCursor(maxX + 8, yPos + 8);
    tft.print("MAX");
    addTouchZone(maxX, yPos, maxX + maxW, yPos + btnH, onRgbBrightnessMax, "RGBMAX");

    yPos += rowH + 8;

    // === Large CALIBRATE button ===
    uint16_t calBtnW = 180, calBtnH = 28;
    uint16_t calBtnX = (320 - calBtnW) / 2;
    tft.fillRect(calBtnX, yPos, calBtnW, calBtnH, 0x0320);
    tft.drawRect(calBtnX, yPos, calBtnW, calBtnH, LOGO_COLOR);
    tft.setTextColor(LOGO_COLOR);
    tft.setTextSize(2);
    tft.setCursor(calBtnX + 28, yPos + 6);
    tft.print("CALIBRATE");
    addTouchZone(calBtnX, yPos, calBtnX + calBtnW, yPos + calBtnH, onCalibratePress, "CAL");

    // Draw footer only
    drawFooter();
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
    // FULL SCREEN calibration - no header/footer, targets at actual corners
    tft.fillScreen(BG_COLOR);

    // Target positions at actual screen corners
    uint16_t margin = 20;
    uint16_t targetSize = 15;
    uint16_t targetX[] = {margin, (uint16_t)(319 - margin), margin, (uint16_t)(319 - margin)};
    uint16_t targetY[] = {margin, margin, (uint16_t)(239 - margin), (uint16_t)(239 - margin)};
    const char* shortLabels[] = {"TL", "TR", "BL", "BR"};

    // Draw all 4 targets
    for (int i = 0; i < 4; i++) {
        uint16_t color;
        if (i < calStep) {
            color = SUCCESS_COLOR;  // Completed = green
        } else if (i == calStep) {
            color = LOGO_COLOR;  // Current = orange/yellow
        } else {
            color = TEXT_DIM;  // Pending = dim
        }

        // Draw crosshair
        tft.drawLine(targetX[i] - targetSize, targetY[i], targetX[i] + targetSize, targetY[i], color);
        tft.drawLine(targetX[i], targetY[i] - targetSize, targetX[i], targetY[i] + targetSize, color);

        // Fill center dot for current target
        if (i == calStep && calStep < 4) {
            tft.fillCircle(targetX[i], targetY[i], 5, LOGO_COLOR);
        } else if (i < calStep) {
            tft.fillCircle(targetX[i], targetY[i], 4, SUCCESS_COLOR);
        }

        // Label near each target
        tft.setTextSize(1);
        tft.setTextColor(color);
        // Position labels outside the crosshairs
        int labelX, labelY;
        if (i == 0) { labelX = targetX[i] + targetSize + 3; labelY = targetY[i] - 3; }      // TL: right
        else if (i == 1) { labelX = targetX[i] - targetSize - 12; labelY = targetY[i] - 3; } // TR: left
        else if (i == 2) { labelX = targetX[i] + targetSize + 3; labelY = targetY[i] - 3; }  // BL: right
        else { labelX = targetX[i] - targetSize - 12; labelY = targetY[i] - 3; }             // BR: left
        tft.setCursor(labelX, labelY);
        tft.print(shortLabels[i]);
    }

    // Center panel with instructions
    uint16_t panelX = 60, panelY = 75;
    uint16_t panelW = 200, panelH = 55;
    tft.fillRect(panelX, panelY, panelW, panelH, PANEL_COLOR);
    tft.drawRect(panelX, panelY, panelW, panelH, TEXT_COLOR);

    tft.setTextColor(TEXT_COLOR);
    tft.setTextSize(1);
    tft.setCursor(panelX + 35, panelY + 6);
    tft.print("TOUCH CALIBRATION");

    if (calStep < 4) {
        // Show current step instruction
        tft.setTextColor(LOGO_COLOR);
        tft.setTextSize(2);
        const char* targetLabels[] = {"TOP-LEFT", "TOP-RIGHT", "BOT-LEFT", "BOT-RIGHT"};
        int labelLen = strlen(targetLabels[calStep]);
        int textX = panelX + (panelW - labelLen * 12) / 2;
        tft.setCursor(textX, panelY + 20);
        tft.print(targetLabels[calStep]);

        // Progress
        tft.setTextColor(TEXT_DIM);
        tft.setTextSize(1);
        tft.setCursor(panelX + 65, panelY + 42);
        tft.printf("Step %d of 4", calStep + 1);
    } else {
        // Calibration complete
        tft.setTextColor(SUCCESS_COLOR);
        tft.setTextSize(2);
        tft.setCursor(panelX + 30, panelY + 20);
        tft.print("COMPLETE!");

        tft.setTextColor(TEXT_DIM);
        tft.setTextSize(1);
        tft.setCursor(panelX + 40, panelY + 42);
        tft.print("Tap SAVE to apply");
    }

    // Button row
    uint16_t btnW = 90, btnH = 28, btnY = 145;

    // CANCEL button (left)
    uint16_t cancelX = 55;
    tft.fillRect(cancelX, btnY, btnW, btnH, 0x4000);
    tft.drawRect(cancelX, btnY, btnW, btnH, ALERT_COLOR);
    tft.setTextColor(TEXT_COLOR);
    tft.setTextSize(1);
    tft.setCursor(cancelX + 24, btnY + 10);
    tft.print("CANCEL");
    addTouchZone(cancelX, btnY, cancelX + btnW, btnY + btnH, onSettingsButtonPress, "CANCEL");

    // SAVE button (right)
    uint16_t saveX = 175;
    if (calStep >= 4) {
        tft.fillRect(saveX, btnY, btnW, btnH, 0x0320);
        tft.drawRect(saveX, btnY, btnW, btnH, SUCCESS_COLOR);
        tft.setTextColor(TEXT_COLOR);
        tft.setCursor(saveX + 30, btnY + 10);
        tft.print("SAVE");
        addTouchZone(saveX, btnY, saveX + btnW, btnY + btnH, onCalibrateSave, "SAVE");
    } else {
        tft.fillRect(saveX, btnY, btnW, btnH, PANEL_COLOR);
        tft.drawRect(saveX, btnY, btnW, btnH, TEXT_DIM);
        tft.setTextColor(TEXT_DIM);
        tft.setCursor(saveX + 30, btnY + 10);
        tft.print("SAVE");
    }
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
                // Invalid calibration, show error in center panel area
                uint16_t panelX = 60, panelY = 105;
                tft.fillRect(panelX, panelY, 200, 50, ALERT_COLOR);
                tft.setTextColor(TEXT_COLOR);
                tft.setTextSize(1);
                tft.setCursor(panelX + 10, panelY + 10);
                tft.print("Calibration invalid!");
                tft.setCursor(panelX + 30, panelY + 28);
                tft.print("Restarting...");
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

// ============================================================================
// OUI LOOKUP
// ============================================================================

// Embedded OUI table for known surveillance and common vendors (fast lookup)
const char* DisplayHandler::lookupEmbeddedOUI(const char* prefix) {
    // Known Flock Safety / Surveillance OUIs
    if (strncasecmp(prefix, "58:8e:81", 8) == 0) return "Flock Safety";
    if (strncasecmp(prefix, "cc:cc:cc", 8) == 0) return "Flock Safety";
    if (strncasecmp(prefix, "ec:1b:bd", 8) == 0) return "Flock Safety";
    if (strncasecmp(prefix, "90:35:ea", 8) == 0) return "Flock Safety";
    if (strncasecmp(prefix, "04:0d:84", 8) == 0) return "Flock Safety";
    if (strncasecmp(prefix, "f0:82:c0", 8) == 0) return "Flock Safety";
    if (strncasecmp(prefix, "1c:34:f1", 8) == 0) return "Flock Safety";
    if (strncasecmp(prefix, "38:5b:44", 8) == 0) return "Flock Safety";
    if (strncasecmp(prefix, "94:34:69", 8) == 0) return "Flock Safety";
    if (strncasecmp(prefix, "b4:e3:f9", 8) == 0) return "Flock Safety";
    if (strncasecmp(prefix, "70:c9:4e", 8) == 0) return "Flock Safety";
    if (strncasecmp(prefix, "3c:91:80", 8) == 0) return "Flock Safety";
    if (strncasecmp(prefix, "d8:f3:bc", 8) == 0) return "Flock Safety";
    if (strncasecmp(prefix, "80:30:49", 8) == 0) return "Flock Safety";
    if (strncasecmp(prefix, "14:5a:fc", 8) == 0) return "Flock Safety";
    if (strncasecmp(prefix, "74:4c:a1", 8) == 0) return "Flock Safety";
    if (strncasecmp(prefix, "08:3a:88", 8) == 0) return "Flock Safety";
    if (strncasecmp(prefix, "9c:2f:9d", 8) == 0) return "Flock Safety";
    if (strncasecmp(prefix, "94:08:53", 8) == 0) return "Flock Safety";
    if (strncasecmp(prefix, "e4:aa:ea", 8) == 0) return "Flock Safety";

    // Common camera/IoT manufacturers
    if (strncasecmp(prefix, "ac:cf:85", 8) == 0) return "Hikvision";
    if (strncasecmp(prefix, "c0:56:e3", 8) == 0) return "Hikvision";
    if (strncasecmp(prefix, "44:19:b6", 8) == 0) return "Hikvision";
    if (strncasecmp(prefix, "54:c4:15", 8) == 0) return "Hikvision";
    if (strncasecmp(prefix, "e0:50:8b", 8) == 0) return "Dahua";
    if (strncasecmp(prefix, "3c:ef:8c", 8) == 0) return "Dahua";
    if (strncasecmp(prefix, "a0:bd:1d", 8) == 0) return "Dahua";
    if (strncasecmp(prefix, "9c:8e:cd", 8) == 0) return "Amcrest";
    if (strncasecmp(prefix, "fc:fc:48", 8) == 0) return "Apple";
    if (strncasecmp(prefix, "3c:06:30", 8) == 0) return "Apple";
    if (strncasecmp(prefix, "00:17:88", 8) == 0) return "Philips Hue";
    if (strncasecmp(prefix, "b8:27:eb", 8) == 0) return "Raspberry Pi";
    if (strncasecmp(prefix, "dc:a6:32", 8) == 0) return "Raspberry Pi";
    if (strncasecmp(prefix, "18:fe:34", 8) == 0) return "Espressif";
    if (strncasecmp(prefix, "24:0a:c4", 8) == 0) return "Espressif";
    if (strncasecmp(prefix, "30:ae:a4", 8) == 0) return "Espressif";
    if (strncasecmp(prefix, "84:cc:a8", 8) == 0) return "Espressif";
    if (strncasecmp(prefix, "b4:e6:2d", 8) == 0) return "Espressif";
    if (strncasecmp(prefix, "50:02:91", 8) == 0) return "Espressif";
    if (strncasecmp(prefix, "34:94:54", 8) == 0) return "Espressif";

    return nullptr;  // Not found in embedded table
}

// Binary search lookup in SD card OUI file
String DisplayHandler::lookupOUIFromSD(const String& prefix) {
    if (!sdCardPresent) return "";
    if (!SD.exists(OUI_FILE)) return "";

    File file = SD.open(OUI_FILE, FILE_READ);
    if (!file) return "";

    // Binary search through sorted file
    size_t fileSize = file.size();
    size_t low = 0, high = fileSize;
    String searchPrefix = prefix;
    searchPrefix.toLowerCase();

    char lineBuf[64];
    String result = "";

    while (low < high) {
        size_t mid = (low + high) / 2;

        // Seek to mid and find start of line
        file.seek(mid);
        if (mid > 0) {
            // Skip to next line
            while (file.available() && file.read() != '\n');
        }

        if (!file.available()) {
            high = mid;
            continue;
        }

        // Read line
        size_t lineStart = file.position();
        int len = file.readBytesUntil('\n', lineBuf, sizeof(lineBuf) - 1);
        if (len <= 0) {
            high = mid;
            continue;
        }
        lineBuf[len] = '\0';

        // Parse OUI prefix (first 8 chars)
        if (len < 8) {
            high = mid;
            continue;
        }

        String linePrefix = String(lineBuf).substring(0, 8);
        linePrefix.toLowerCase();

        int cmp = searchPrefix.compareTo(linePrefix);
        if (cmp == 0) {
            // Found! Extract vendor name after comma
            char* comma = strchr(lineBuf, ',');
            if (comma) {
                result = String(comma + 1);
                result.trim();
            }
            break;
        } else if (cmp < 0) {
            high = lineStart;
        } else {
            low = file.position();
        }
    }

    file.close();
    return result;
}

// Main OUI lookup: embedded first, then SD card fallback
String DisplayHandler::lookupOUI(const String& mac) {
    // Extract prefix (first 8 chars: "aa:bb:cc")
    if (mac.length() < 8) return "Unknown";
    String prefix = mac.substring(0, 8);

    // Try embedded lookup first (fast)
    const char* embedded = lookupEmbeddedOUI(prefix.c_str());
    if (embedded) return String(embedded);

    // Try SD card lookup (slower but comprehensive)
    String sdResult = lookupOUIFromSD(prefix);
    if (sdResult.length() > 0) return sdResult;

    return "Unknown";
}

void DisplayHandler::addDetection(String ssid, String mac, int8_t rssi, String type) {
    Detection det;
    det.ssid = ssid;
    det.mac = mac;
    det.vendor = lookupOUI(mac);
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
    currentPage = (DisplayPage)((currentPage + 1) % 4);  // 4 main pages (0-3)
    clear();
}

void DisplayHandler::previousPage() {
    currentPage = (DisplayPage)((currentPage + 3) % 4);  // 4 main pages (0-3)
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
    saveSettings();
    needsRedraw = true;
}

void DisplayHandler::increaseBrightness() {
    if (brightness <= 230) {
        brightness += 25;  // ~10% of 255
    } else {
        brightness = 255;
    }
    applyBrightness();
    saveSettings();
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
    saveSettings();
    needsRedraw = true;
}

void DisplayHandler::toggleAutoBrightness() {
    autoBrightness = !autoBrightness;
    if (autoBrightness) {
        updateAutoBrightness();  // Apply immediately
    }
    saveSettings();
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
    saveSettings();
    needsRedraw = true;
}

void DisplayHandler::increaseRgbBrightness() {
    if (rgbBrightness <= 230) {
        rgbBrightness += 25;
    } else {
        rgbBrightness = 255;
    }
    saveSettings();
    needsRedraw = true;
}

void DisplayHandler::decreaseRgbBrightness() {
    if (rgbBrightness >= 50) {
        rgbBrightness -= 25;
    } else {
        rgbBrightness = 25;  // Minimum 10%
    }
    saveSettings();
    needsRedraw = true;
}

void DisplayHandler::toggleLedAlerts() {
    ledAlertsEnabled = !ledAlertsEnabled;
    saveSettings();
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
            file.println("timestamp,ssid,mac,vendor,rssi,type");
            file.close();
            Serial.println("SD Card: Created log file");
        }
    }

    Serial.printf("SD Card: Initialized, type=%d, size=%lluMB\n", cardType, SD.cardSize() / (1024 * 1024));
    return true;
}

void DisplayHandler::checkSDCard() {
    uint32_t now = millis();

    // Only check every 3 seconds
    if (now - lastSdCheck < 3000) return;
    lastSdCheck = now;

    bool wasPresent = sdCardPresent;

    if (sdCardPresent) {
        // Card was present - try to access it to verify still there
        // SD.exists() or opening a file is more reliable than cardType()
        File testFile = SD.open("/");
        if (!testFile) {
            // Card was removed or became inaccessible
            sdCardPresent = false;
            SD.end();  // Clean up SD library state
            Serial.println("SD Card: Removed");
        } else {
            testFile.close();
        }
    } else {
        // Card was not present, try to initialize
        if (SD.begin(SD_CS)) {
            File testFile = SD.open("/");
            if (testFile) {
                testFile.close();
                sdCardPresent = true;
                Serial.println("SD Card: Inserted");

                // Re-setup log file if needed
                if (!SD.exists(logFileName)) {
                    File file = SD.open(logFileName, FILE_WRITE);
                    if (file) {
                        file.println("timestamp,ssid,mac,vendor,rssi,type");
                        file.close();
                    }
                }
            }
        }
    }

    // Force redraw if state changed
    if (wasPresent != sdCardPresent) {
        needsRedraw = true;
        Serial.printf("SD Card state changed: %s\n", sdCardPresent ? "PRESENT" : "REMOVED");
    }
}

void DisplayHandler::logDetection(const String& ssid, const String& mac, int8_t rssi, const String& type) {
    if (!sdCardPresent) return;

    File file = SD.open(logFileName, FILE_APPEND);
    if (file) {
        // Lookup vendor for logging
        String vendor = lookupOUI(mac);

        // Format: timestamp,ssid,mac,vendor,rssi,type
        file.printf("%lu,\"%s\",%s,\"%s\",%d,%s\n",
            millis() / 1000,  // seconds since boot
            ssid.c_str(),
            mac.c_str(),
            vendor.c_str(),
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

bool DisplayHandler::loadSettings() {
    if (!sdCardPresent) return false;
    if (!SD.exists(SETTINGS_FILE)) return false;

    File file = SD.open(SETTINGS_FILE, FILE_READ);
    if (!file) return false;

    // Read settings (one per line): brightness, autoBrightness, soundEnabled, soundVolume, ledAlertsEnabled, rgbBrightness
    String line;
    int idx = 0;
    int values[6] = {255, 0, 1, 128, 1, 128};  // Defaults

    while (file.available() && idx < 6) {
        line = file.readStringUntil('\n');
        line.trim();
        if (line.length() > 0) {
            values[idx++] = line.toInt();
        }
    }
    file.close();

    if (idx >= 6) {
        brightness = constrain(values[0], 25, 255);
        autoBrightness = values[1] != 0;
        soundEnabled = values[2] != 0;
        soundVolume = constrain(values[3], 0, 255);
        ledAlertsEnabled = values[4] != 0;
        rgbBrightness = constrain(values[5], 0, 255);
        applyBrightness();
        Serial.printf("Settings loaded: bright=%d auto=%d snd=%d vol=%d led=%d rgb=%d\n",
            brightness, autoBrightness, soundEnabled, soundVolume, ledAlertsEnabled, rgbBrightness);
        return true;
    }
    return false;
}

bool DisplayHandler::saveSettings() {
    if (!sdCardPresent) {
        Serial.println("Cannot save settings: no SD card");
        return false;
    }

    File file = SD.open(SETTINGS_FILE, FILE_WRITE);
    if (!file) {
        Serial.println("Cannot save settings: file open failed");
        return false;
    }

    // Write settings (one per line)
    file.println(brightness);
    file.println(autoBrightness ? 1 : 0);
    file.println(soundEnabled ? 1 : 0);
    file.println(soundVolume);
    file.println(ledAlertsEnabled ? 1 : 0);
    file.println(rgbBrightness);
    file.close();

    Serial.printf("Settings saved: bright=%d auto=%d snd=%d vol=%d led=%d rgb=%d\n",
        brightness, autoBrightness, soundEnabled, soundVolume, ledAlertsEnabled, rgbBrightness);
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

// === Speaker Functions ===
void DisplayHandler::setupSpeaker() {
    ledcSetup(SPEAKER_CHANNEL, 2000, 8);  // Channel 5, 2kHz default, 8-bit
    ledcAttachPin(SPEAKER_PIN, SPEAKER_CHANNEL);
    ledcWrite(SPEAKER_CHANNEL, 0);  // Start silent
    Serial.println("Speaker initialized on GPIO 26");
}

void DisplayHandler::playTone(uint16_t frequency, uint16_t duration) {
    if (!soundEnabled) return;

    // Scale volume (0-255) to duty cycle (0-127 for audible range)
    uint8_t duty = (soundVolume * 64) / 255;

    ledcWriteTone(SPEAKER_CHANNEL, frequency);
    ledcWrite(SPEAKER_CHANNEL, duty);
    delay(duration);
    ledcWrite(SPEAKER_CHANNEL, 0);  // Stop tone
}

void DisplayHandler::playBootTone() {
    if (!soundEnabled) return;

    // Play boot tone at fixed 20% volume regardless of setting
    uint8_t savedVolume = soundVolume;
    soundVolume = 51;  // 20% of 255

    // Play a short ascending tone sequence
    playTone(880, 80);   // A5
    delay(30);
    playTone(1175, 80);  // D6
    delay(30);
    playTone(1760, 120); // A6

    soundVolume = savedVolume;  // Restore user's volume
}

void DisplayHandler::toggleSound() {
    soundEnabled = !soundEnabled;
    if (soundEnabled) {
        playTone(1000, 50);  // Quick beep to confirm
    }
    saveSettings();
    needsRedraw = true;
}

void DisplayHandler::setSoundVolume(uint8_t level) {
    soundVolume = level;
    saveSettings();
    needsRedraw = true;
}

void DisplayHandler::increaseSoundVolume() {
    if (soundVolume <= 230) {
        soundVolume += 25;
    } else {
        soundVolume = 255;
    }
    if (soundEnabled) {
        playTone(1000, 30);  // Quick feedback beep
    }
    saveSettings();
    needsRedraw = true;
}

void DisplayHandler::decreaseSoundVolume() {
    if (soundVolume >= 50) {
        soundVolume -= 25;
    } else {
        soundVolume = 25;  // Minimum 10%
    }
    if (soundEnabled) {
        playTone(800, 30);  // Quick feedback beep
    }
    saveSettings();
    needsRedraw = true;
}

#endif // CYD_DISPLAY
