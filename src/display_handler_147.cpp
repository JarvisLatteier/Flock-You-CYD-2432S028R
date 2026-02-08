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
    currentPage(PAGE_MAIN),
    buttonPressed(false),
    buttonPressTime(0),
    longPressHandled(false),
    adjustMode(false),
    settingsSelection(0),
    rgbBrightness(128),
    totalDetections(0),
    flockDetections(0),
    bleDetections(0),
    closestThreatRssi(-127),
    lastThreatTime(0),
    hadThreat(false),
    scrollOffset(0),
    lastScrollTime(0),
    scrollPaused(false),
    currentChannel(1),
    bleScanning(false),
    sdCardPresent(false),
    lastSdCheck(0),
    detectionsLogged(0),
    ledState(1),
    lastLedUpdate(0),
    lastDetectionTime(0),
    alertStartTime(0),
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

    // Initialize stats
    memset(channelCounts, 0, sizeof(channelCounts));

    // Initialize boot button
    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

    // Initialize TFT
    Serial.println("  TFT init...");
    tft.init();
    tft.setRotation(1);  // Landscape mode (320x172)
    tft.fillScreen(BG_COLOR);
    Serial.println("  TFT done");

    // Setup backlight
    Serial.println("  Backlight init...");
    setupBacklightPWM();
    Serial.println("  Backlight done");

    // Initialize FastLED for WS2812
    Serial.println("  FastLED init...");
    Serial.flush();
    delay(10);
    FastLED.addLeds<WS2812B, RGB_LED_PIN, RGB>(leds, NUM_LEDS);
    FastLED.setBrightness(rgbBrightness);
    leds[0] = CRGB(0, 128, 0);  // Start green (scanning)
    FastLED.show();
    Serial.println("  FastLED done");

    // Initialize SD Card (SDMMC)
    Serial.println("  SD init...");
    initSDCard();
    Serial.println("  SD done");

    // Load saved settings from SD
    if (sdCardPresent) {
        Serial.println("  Loading settings...");
        loadSettings();
    }

    // Show boot animation
    Serial.println("  Boot animation...");
    showBootAnimation();

    needsRedraw = true;
    Serial.println("Display ready!");
    return true;
}

void DisplayHandler::showBootAnimation() {
    tft.fillScreen(TFT_BLACK);

    // Header bar (matching main UI style)
    tft.fillRect(CONTENT_X, CONTENT_Y, CONTENT_WIDTH, 35, HEADER_COLOR);
    tft.drawFastHLine(CONTENT_X, CONTENT_Y + 34, CONTENT_WIDTH, TEXT_DIM);

    // "FLOCK YOU" title with glitch effect
    tft.setTextSize(2);
    const char* title = "FLOCK YOU";
    int titleX = CONTENT_X + (CONTENT_WIDTH - strlen(title) * 12) / 2;
    int titleY = CONTENT_Y + 5;

    // Quick glitch effect
    for (int g = 0; g < 3; g++) {
        tft.setTextColor(g % 2 ? ALERT_WARN : ALERT_COLOR);
        tft.setCursor(titleX + (g % 2 ? 2 : -2), titleY);
        tft.print(title);
        delay(50);
        tft.fillRect(CONTENT_X + 4, CONTENT_Y + 2, CONTENT_WIDTH - 8, 30, HEADER_COLOR);
    }

    // Final title
    tft.setTextColor(ALERT_WARN);  // Orange like CYD
    tft.setCursor(titleX, titleY);
    tft.print(title);

    // Tagline
    tft.setTextSize(1);
    tft.setTextColor(ALERT_WARN);
    tft.setCursor(CONTENT_X + (CONTENT_WIDTH - 120) / 2, CONTENT_Y + 24);
    tft.print("Surveillance Detector");

    delay(200);

    // Status messages in content area
    const char* messages[] = {
        "WiFi init...",
        "BLE scanner...",
        "Patterns...",
        "System ready"
    };

    tft.setTextSize(1);
    int msgY = CONTENT_Y + 45;
    int msgSpacing = 18;

    for (int i = 0; i < 4; i++) {
        // Progress indicator
        tft.setTextColor(ALERT_WARN);
        tft.setCursor(CONTENT_X + 10, msgY + i * msgSpacing);
        tft.print(">");

        // Message
        tft.setTextColor(TEXT_COLOR);
        tft.setCursor(CONTENT_X + 22, msgY + i * msgSpacing);
        tft.print(messages[i]);

        delay(120);

        // Checkmark
        tft.setTextColor(SUCCESS_COLOR);
        tft.setCursor(CONTENT_X + 130, msgY + i * msgSpacing);
        tft.print("[OK]");

        delay(80);
    }

    // SD card status
    tft.setTextColor(ALERT_WARN);
    tft.setCursor(CONTENT_X + 170, msgY);
    tft.print(">");
    tft.setTextColor(TEXT_COLOR);
    tft.setCursor(CONTENT_X + 182, msgY);
    tft.print("SD: ");
    tft.setTextColor(sdCardPresent ? SUCCESS_COLOR : ALERT_COLOR);
    tft.print(sdCardPresent ? "OK" : "--");

    // Settings status
    tft.setTextColor(ALERT_WARN);
    tft.setCursor(CONTENT_X + 170, msgY + msgSpacing);
    tft.print(">");
    tft.setTextColor(TEXT_COLOR);
    tft.setCursor(CONTENT_X + 182, msgY + msgSpacing);
    tft.print("Settings: ");
    tft.setTextColor(SUCCESS_COLOR);
    tft.print("OK");

    delay(300);
}

void DisplayHandler::update() {
    uint32_t now = millis();

    // Handle button input
    handleButton();

    // Check SD card periodically
    checkSDCard();

    // Update LED state
    updateLED();

    // Auto-scroll detection list every 3 seconds
    if (currentPage == PAGE_LIST &&
        !scrollPaused && detections.size() > 4 && now - lastScrollTime > 3000) {
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
        switch (currentPage) {
            case PAGE_MAIN:
                drawHeader();
                drawStatsPanel();
                drawLatestDetection();
                drawFooter();
                break;
            case PAGE_LIST:
                drawHeader();
                drawDetectionList();
                drawFooter();
                break;
            case PAGE_STATS:
                drawHeader();
                drawFullStatsList();
                drawFooter();
                break;
            case PAGE_SETTINGS:
                drawSettingsPage();
                break;
        }
        lastUpdate = now;
        needsRedraw = false;
    }
}

void DisplayHandler::handleButton() {
    bool currentState = (digitalRead(BOOT_BUTTON_PIN) == LOW);
    uint32_t now = millis();

    if (currentState && !buttonPressed) {
        // Button just pressed
        buttonPressed = true;
        buttonPressTime = now;
        longPressHandled = false;
    }
    else if (currentState && buttonPressed) {
        // Button held - check for long press
        if (!longPressHandled && (now - buttonPressTime > LONG_PRESS_MS)) {
            longPressHandled = true;

            if (adjustMode) {
                // Hold in adjust mode = exit adjust mode
                adjustMode = false;
                needsRedraw = true;
            }
            else if (currentPage == PAGE_SETTINGS) {
                if (settingsSelection == 2) {
                    // Long press on Exit = go back to HOME
                    setPage(PAGE_MAIN);
                } else {
                    // Long press on Display/LED = enter adjust mode
                    adjustMode = true;
                    needsRedraw = true;
                }
            }
            else {
                // Hold on other pages - reserved
            }
        }
    }
    else if (!currentState && buttonPressed) {
        // Button released
        buttonPressed = false;

        if (!longPressHandled && (now - buttonPressTime > DEBOUNCE_MS)) {
            // Short tap
            if (adjustMode) {
                // Tap in adjust mode = increase selected value
                if (settingsSelection == 0) {
                    brightness = (brightness >= 245) ? 50 : brightness + 25;
                    applyBrightness();
                } else {
                    rgbBrightness = (rgbBrightness >= 245) ? 25 : rgbBrightness + 25;
                    FastLED.setBrightness(rgbBrightness);
                    FastLED.show();
                }
                saveSettings();
                needsRedraw = true;
            }
            else if (currentPage == PAGE_SETTINGS) {
                // Short press on CONFIG = cycle selection
                settingsSelection = (settingsSelection + 1) % 3;
                needsRedraw = true;
            }
            else {
                // Tap on other pages = next page
                nextPage();
            }
        }
    }
}

void DisplayHandler::nextPage() {
    currentPage = (currentPage + 1) % PAGE_COUNT;
    scrollOffset = 0;
    adjustMode = false;
    needsRedraw = true;
    tft.fillScreen(TFT_BLACK);
    tft.fillRect(CONTENT_X, CONTENT_Y, CONTENT_WIDTH, CONTENT_HEIGHT, BG_COLOR);
}

void DisplayHandler::setPage(DisplayPage page) {
    if (page < PAGE_COUNT) {
        currentPage = page;
        scrollOffset = 0;
        adjustMode = false;
        needsRedraw = true;
        tft.fillScreen(TFT_BLACK);
        tft.fillRect(CONTENT_X, CONTENT_Y, CONTENT_WIDTH, CONTENT_HEIGHT, BG_COLOR);
    }
}

void DisplayHandler::drawHeader() {
    tft.fillRect(CONTENT_X, CONTENT_Y, CONTENT_WIDTH, HEADER_HEIGHT, HEADER_COLOR);

    // Title with page indicator
    tft.setTextSize(1);
    tft.setTextColor(TEXT_COLOR);
    tft.setCursor(CONTENT_X + 4, CONTENT_Y + 5);
    const char* pageNames[] = {"HOME", "LIST", "STATS", "CONFIG"};
    tft.printf("FLOCK YOU [%s]", pageNames[currentPage]);

    // Channel indicator
    tft.setTextColor(bleScanning ? BLE_COLOR : WIFI_COLOR);
    tft.setCursor(CONTENT_X + 130, CONTENT_Y + 5);
    tft.printf("%s CH:%d", bleScanning ? "BLE" : "WiFi", currentChannel);

    // SD indicator
    tft.setCursor(CONTENT_X + 220, CONTENT_Y + 5);
    if (sdCardPresent) {
        tft.setTextColor(SUCCESS_COLOR);
        tft.print("SD");
    } else {
        tft.setTextColor(ALERT_COLOR);
        tft.print("--");
    }

    // Threat count
    tft.setTextColor(flockDetections > 0 ? ALERT_COLOR : TEXT_DIM);
    tft.setCursor(CONTENT_X + 250, CONTENT_Y + 5);
    tft.printf("THR:%d", flockDetections);
}

void DisplayHandler::drawStatsPanel() {
    int y = CONTENT_Y + HEADER_HEIGHT + 2;

    // Stats background
    tft.fillRect(CONTENT_X, y, CONTENT_WIDTH, STAT_BOX_HEIGHT, BG_DARK);

    // Three stat boxes in landscape
    int boxW = (CONTENT_WIDTH - 10) / 3;
    int boxH = STAT_BOX_HEIGHT - 4;
    int spacing = 2;

    // WiFi detections
    int x1 = CONTENT_X + spacing;
    tft.drawRect(x1, y + 2, boxW, boxH, WIFI_COLOR);
    tft.setTextSize(2);
    tft.setTextColor(WIFI_COLOR);
    tft.setCursor(x1 + 8, y + 5);
    tft.printf("%d", totalDetections - bleDetections - flockDetections);
    tft.setTextSize(1);
    tft.setCursor(x1 + 50, y + 8);
    tft.print("WiFi");

    // BLE detections
    int x2 = x1 + boxW + spacing;
    tft.drawRect(x2, y + 2, boxW, boxH, BLE_COLOR);
    tft.setTextSize(2);
    tft.setTextColor(BLE_COLOR);
    tft.setCursor(x2 + 8, y + 5);
    tft.printf("%d", bleDetections);
    tft.setTextSize(1);
    tft.setCursor(x2 + 50, y + 8);
    tft.print("BLE");

    // Flock/threat detections
    int x3 = x2 + boxW + spacing;
    tft.drawRect(x3, y + 2, boxW, boxH, ALERT_COLOR);
    tft.setTextSize(2);
    tft.setTextColor(flockDetections > 0 ? ALERT_COLOR : TEXT_DIM);
    tft.setCursor(x3 + 8, y + 5);
    tft.printf("%d", flockDetections);
    tft.setTextSize(1);
    tft.setTextColor(ALERT_COLOR);
    tft.setCursor(x3 + 50, y + 8);
    tft.print("THREAT");
}

void DisplayHandler::drawLatestDetection() {
    int startY = CONTENT_Y + HEADER_HEIGHT + STAT_BOX_HEIGHT + 2;
    int endY = CONTENT_Y + CONTENT_HEIGHT - FOOTER_HEIGHT;
    int panelH = endY - startY - 2;

    // Clear area
    tft.fillRect(CONTENT_X, startY, CONTENT_WIDTH, panelH, BG_COLOR);

    if (detections.empty()) {
        tft.setTextSize(1);
        tft.setTextColor(TEXT_DIM);
        int cx = CONTENT_X + (CONTENT_WIDTH - 90) / 2;
        tft.setCursor(cx, startY + panelH / 2 - 10);
        tft.print("Scanning...");
        tft.setCursor(cx - 10, startY + panelH / 2 + 4);
        tft.print("No detections yet");
        return;
    }

    Detection& d = detections[0];

    // Determine if this is a threat
    bool isThreat = (d.type.indexOf("flock") >= 0 || d.type.indexOf("Flock") >= 0 ||
                     d.type.indexOf("penguin") >= 0 || d.type.indexOf("pigvision") >= 0);
    bool isBLE = (d.type == "ble");

    uint16_t accentColor = isThreat ? ALERT_COLOR : (isBLE ? BLE_COLOR : WIFI_COLOR);

    // Flash effect for new detections (first 2 seconds, 200ms toggle)
    uint32_t age = millis() - d.timestamp;
    bool isFlashing = d.isNew && age < 2000;
    bool flashOn = isFlashing && ((age / 200) % 2 == 0);

    if (isFlashing) {
        needsRedraw = true;  // Keep redrawing during flash
    }

    // Panel background flash
    if (flashOn) {
        tft.fillRect(CONTENT_X + 2, startY + 1, CONTENT_WIDTH - 4, panelH - 2, accentColor);
    }

    // Panel border
    tft.drawRect(CONTENT_X + 2, startY + 1, CONTENT_WIDTH - 4, panelH - 2, accentColor);
    if (isFlashing) {
        tft.drawRect(CONTENT_X + 3, startY + 2, CONTENT_WIDTH - 6, panelH - 4, accentColor);
    }

    // Left accent bar
    tft.fillRect(CONTENT_X + 2, startY + 1, 4, panelH - 2, accentColor);

    // "LATEST" label
    tft.setTextSize(1);
    tft.setTextColor(flashOn ? BG_DARK : accentColor);
    tft.setCursor(CONTENT_X + 10, startY + 4);
    tft.print(isFlashing ? "** NEW **" : "LATEST");

    // Time ago
    uint32_t ago = age / 1000;
    tft.setTextColor(flashOn ? BG_DARK : TEXT_DIM);
    tft.setCursor(CONTENT_X + 64, startY + 4);
    if (ago < 60) {
        tft.printf("%ds ago", ago);
    } else if (ago < 3600) {
        tft.printf("%dm ago", ago / 60);
    } else {
        tft.printf("%dh%dm", ago / 3600, (ago % 3600) / 60);
    }

    // Signal bars (top right)
    drawSignalBars(CONTENT_X + CONTENT_WIDTH - 34, startY + 2, d.rssi);

    // RSSI
    tft.setTextColor(flashOn ? BG_DARK : TEXT_COLOR);
    tft.setCursor(CONTENT_X + CONTENT_WIDTH - 70, startY + 4);
    tft.printf("%ddBm", d.rssi);

    // SSID / Vendor name (large)
    tft.setTextSize(2);
    tft.setTextColor(flashOn ? BG_DARK : (isThreat ? ALERT_COLOR : TEXT_COLOR));
    tft.setCursor(CONTENT_X + 10, startY + 18);
    String label = d.vendor.length() > 0 ? d.vendor : d.ssid;
    if (label.length() == 0) label = "Unknown";
    if (label.length() > 20) label = label.substring(0, 17) + "...";
    tft.print(label);

    // MAC address
    tft.setTextSize(1);
    tft.setTextColor(flashOn ? BG_DARK : TEXT_DIM);
    tft.setCursor(CONTENT_X + 10, startY + 38);
    tft.print(d.mac);

    // Type badge
    tft.setTextColor(flashOn ? BG_DARK : accentColor);
    tft.setCursor(CONTENT_X + 130, startY + 38);
    if (isThreat) {
        tft.print("!! THREAT !!");
    } else if (isBLE) {
        tft.print("BLE");
    } else {
        tft.print("WiFi");
    }

    // Total count at bottom right of panel
    tft.setTextColor(flashOn ? BG_DARK : TEXT_DIM);
    tft.setCursor(CONTENT_X + CONTENT_WIDTH - 80, startY + panelH - 12);
    tft.printf("Unique:%d", detections.size());

    // Clear new flag after flash period
    if (!isFlashing && d.isNew) {
        d.isNew = false;
    }
}

void DisplayHandler::drawDetectionList() {
    int startY = CONTENT_Y + HEADER_HEIGHT + 2;
    int endY = CONTENT_Y + CONTENT_HEIGHT - FOOTER_HEIGHT;
    int listHeight = endY - startY - 2;
    int maxItems = listHeight / LIST_ITEM_HEIGHT;

    // Clear list area
    tft.fillRect(CONTENT_X, startY, CONTENT_WIDTH, listHeight, BG_COLOR);

    if (detections.empty()) {
        tft.setTextSize(1);
        tft.setTextColor(TEXT_DIM);
        tft.setCursor(CONTENT_X + CONTENT_WIDTH / 2 - 40, startY + listHeight / 2 - 4);
        tft.print("No detections");
        return;
    }

    // Draw visible items (landscape - more room for text)
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
        tft.fillRect(CONTENT_X, itemY, 3, LIST_ITEM_HEIGHT - 2, barColor);

        // SSID or vendor
        tft.setTextSize(1);
        tft.setTextColor(d.isNew ? TEXT_COLOR : TEXT_DIM);
        tft.setCursor(CONTENT_X + 6, itemY + 3);
        String label = d.vendor.length() > 0 ? d.vendor : d.ssid;
        if (label.length() > 20) {
            label = label.substring(0, 17) + "...";
        }
        tft.print(label);

        // Hit count (if seen more than once)
        if (d.hitCount > 1) {
            tft.setTextColor(ALERT_WARN);
            tft.printf(" x%d", d.hitCount);
        }

        // MAC address + time ago
        tft.setTextColor(TEXT_DIM);
        tft.setCursor(CONTENT_X + 6, itemY + 12);
        tft.print(d.mac);

        // Time since last seen
        uint32_t ago = (millis() - d.timestamp) / 1000;
        tft.setCursor(CONTENT_X + 115, itemY + 12);
        if (ago < 60) {
            tft.printf("%ds", ago);
        } else if (ago < 3600) {
            tft.printf("%dm", ago / 60);
        } else {
            tft.printf("%dh", ago / 3600);
        }

        // RSSI and signal bars on right
        tft.setCursor(CONTENT_X + 210, itemY + 3);
        tft.printf("%ddBm", d.rssi);
        drawSignalBars(CONTENT_X + CONTENT_WIDTH - 30, itemY + 5, d.rssi);

        // Mark as not new after display
        d.isNew = false;
    }

    // Scroll indicator
    if (detections.size() > (size_t)maxItems) {
        int indicatorH = listHeight * maxItems / detections.size();
        int indicatorY = startY + (listHeight - indicatorH) * scrollOffset / (detections.size() - maxItems);
        tft.fillRect(CONTENT_X + CONTENT_WIDTH - 3, indicatorY, 3, indicatorH, TEXT_DIM);
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
    int y = CONTENT_Y + CONTENT_HEIGHT - FOOTER_HEIGHT;
    tft.fillRect(CONTENT_X, y, CONTENT_WIDTH, FOOTER_HEIGHT, FOOTER_COLOR);

    // Total count
    tft.setTextSize(1);
    tft.setTextColor(TEXT_COLOR);
    tft.setCursor(CONTENT_X + 4, y + 3);
    tft.printf("Total: %d", totalDetections);

    // Page indicator dots (centered)
    int dotsX = CONTENT_X + (CONTENT_WIDTH - (PAGE_COUNT * 10)) / 2;
    for (int i = 0; i < PAGE_COUNT; i++) {
        uint16_t dotColor = (i == currentPage) ? ACCENT_COLOR : TEXT_DIM;
        tft.fillCircle(dotsX + i * 10, y + 7, 2, dotColor);
    }

    // Button hint
    tft.setTextColor(TEXT_DIM);
    tft.setCursor(CONTENT_X + 210, y + 3);
    tft.print("TAP:next HOLD:act");
}

void DisplayHandler::drawSettingsPage() {
    // Full screen settings page with padding
    tft.fillRect(CONTENT_X, CONTENT_Y, CONTENT_WIDTH, CONTENT_HEIGHT, BG_DARK);

    // Title and mode indicator
    tft.setTextSize(2);
    tft.setTextColor(adjustMode ? SUCCESS_COLOR : ACCENT_COLOR);
    tft.setCursor(CONTENT_X + 4, CONTENT_Y + 4);
    tft.print(adjustMode ? "ADJUST" : "CONFIG");

    // Instructions based on mode
    tft.setTextSize(1);
    tft.setTextColor(TEXT_DIM);
    tft.setCursor(CONTENT_X + 90, CONTENT_Y + 8);
    if (adjustMode) {
        tft.setTextColor(SUCCESS_COLOR);
        tft.print("TAP:+  HOLD:done");
    } else if (settingsSelection == 2) {
        tft.print("TAP:sel  HOLD:home");
    } else {
        tft.print("TAP:sel  HOLD:edit");
    }

    int y = CONTENT_Y + 26;
    int boxW = 95;
    int boxH = 42;

    // Display brightness box
    bool dispSelected = (settingsSelection == 0);
    uint16_t dispBorder = dispSelected ? (adjustMode ? SUCCESS_COLOR : ACCENT_COLOR) : TEXT_DIM;
    int x1 = CONTENT_X + 2;
    tft.drawRect(x1, y, boxW, boxH, dispBorder);
    if (dispSelected) {
        tft.drawRect(x1 + 1, y + 1, boxW - 2, boxH - 2, dispBorder);
        if (adjustMode) {
            tft.drawRect(x1 + 2, y + 2, boxW - 4, boxH - 4, dispBorder);
        }
    }
    tft.setTextSize(1);
    tft.setTextColor(TEXT_COLOR);
    tft.setCursor(x1 + 6, y + 5);
    tft.print("DISPLAY");
    tft.setTextSize(2);
    tft.setTextColor(dispSelected ? (adjustMode ? SUCCESS_COLOR : ACCENT_COLOR) : TEXT_COLOR);
    tft.setCursor(x1 + 20, y + 20);
    tft.printf("%3d%%", (brightness * 100) / 255);

    // RGB LED brightness box
    bool ledSelected = (settingsSelection == 1);
    uint16_t ledBorder = ledSelected ? (adjustMode ? SUCCESS_COLOR : ACCENT_COLOR) : TEXT_DIM;
    int x2 = x1 + boxW + 4;
    tft.drawRect(x2, y, boxW, boxH, ledBorder);
    if (ledSelected) {
        tft.drawRect(x2 + 1, y + 1, boxW - 2, boxH - 2, ledBorder);
        if (adjustMode) {
            tft.drawRect(x2 + 2, y + 2, boxW - 4, boxH - 4, ledBorder);
        }
    }
    tft.setTextSize(1);
    tft.setTextColor(TEXT_COLOR);
    tft.setCursor(x2 + 6, y + 5);
    tft.print("RGB LED");
    tft.setTextSize(2);
    tft.setTextColor(ledSelected ? (adjustMode ? SUCCESS_COLOR : ACCENT_COLOR) : TEXT_COLOR);
    tft.setCursor(x2 + 20, y + 20);
    tft.printf("%3d%%", (rgbBrightness * 100) / 255);

    // EXIT box
    bool exitSelected = (settingsSelection == 2);
    uint16_t exitBorder = exitSelected ? ALERT_WARN : TEXT_DIM;
    int x3 = x2 + boxW + 4;
    int exitW = CONTENT_WIDTH - x3 + CONTENT_X - 2;
    tft.drawRect(x3, y, exitW, boxH, exitBorder);
    if (exitSelected) {
        tft.drawRect(x3 + 1, y + 1, exitW - 2, boxH - 2, exitBorder);
    }
    tft.setTextSize(2);
    tft.setTextColor(exitSelected ? ALERT_WARN : TEXT_DIM);
    tft.setCursor(x3 + 10, y + 14);
    tft.print("EXIT");

    // SD Card file status section
    y = CONTENT_Y + 74;
    tft.drawFastHLine(CONTENT_X + 2, y, CONTENT_WIDTH - 4, TEXT_DIM);
    y += 4;

    tft.setTextSize(1);
    tft.setTextColor(sdCardPresent ? SUCCESS_COLOR : ALERT_COLOR);
    tft.setCursor(CONTENT_X + 4, y);
    tft.printf("SD: %s", sdCardPresent ? "MOUNTED" : "NOT FOUND");

    if (sdCardPresent) {
        // Card type and size
        uint8_t cardType = SD_MMC.cardType();
        const char* typeStr = cardType == CARD_MMC ? "MMC" :
                              cardType == CARD_SD ? "SD" :
                              cardType == CARD_SDHC ? "SDHC" : "?";
        uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
        uint64_t usedSize = SD_MMC.usedBytes() / 1024;

        tft.setTextColor(TEXT_DIM);
        tft.setCursor(CONTENT_X + 120, y);
        tft.printf("%s %lluMB  Used:%lluKB", typeStr, cardSize, usedSize);

        // File listing
        y += 12;
        // Detection log
        tft.setTextColor(TEXT_DIM);
        tft.setCursor(CONTENT_X + 4, y);
        tft.print("Log:");
        if (SD_MMC.exists(logFileName)) {
            fs::File f = SD_MMC.open(logFileName, FILE_READ);
            if (f) {
                uint32_t fSize = f.size();
                f.close();
                tft.setTextColor(SUCCESS_COLOR);
                if (fSize > 1024) {
                    tft.printf(" %dKB", fSize / 1024);
                } else {
                    tft.printf(" %dB", fSize);
                }
                tft.setTextColor(TEXT_DIM);
                tft.printf(" (%d entries)", detectionsLogged);
            }
        } else {
            tft.setTextColor(TEXT_DIM);
            tft.print(" --");
        }

        // Settings file
        tft.setCursor(CONTENT_X + 4, y + 11);
        tft.setTextColor(TEXT_DIM);
        tft.print("Settings:");
        if (SD_MMC.exists(SETTINGS_FILE)) {
            tft.setTextColor(SUCCESS_COLOR);
            tft.print(" saved");
        } else {
            tft.setTextColor(ALERT_WARN);
            tft.print(" default");
        }

        // OUI database
        tft.setCursor(CONTENT_X + 120, y + 11);
        tft.setTextColor(TEXT_DIM);
        tft.print("OUI:");
        if (SD_MMC.exists("/oui.csv")) {
            fs::File f = SD_MMC.open("/oui.csv", FILE_READ);
            if (f) {
                uint32_t fSize = f.size();
                f.close();
                tft.setTextColor(SUCCESS_COLOR);
                tft.printf(" %dKB", fSize / 1024);
            }
        } else {
            tft.setTextColor(TEXT_DIM);
            tft.print(" --");
        }
    }

    // Uptime
    y += 24;
    uint32_t uptime = millis() / 1000;
    tft.setTextColor(TEXT_DIM);
    tft.setCursor(CONTENT_X + 4, y);
    tft.printf("Up:%02d:%02d:%02d", uptime / 3600, (uptime % 3600) / 60, uptime % 60);
    tft.setCursor(CONTENT_X + 100, y);
    tft.printf("Total:%d", totalDetections);

    // Page indicator dots at bottom
    y = CONTENT_Y + CONTENT_HEIGHT - 12;
    int dotsX = CONTENT_X + (CONTENT_WIDTH - (PAGE_COUNT * 10)) / 2;
    for (int i = 0; i < PAGE_COUNT; i++) {
        uint16_t dotColor = (i == currentPage) ? ACCENT_COLOR : TEXT_DIM;
        tft.fillCircle(dotsX + i * 10, y, 2, dotColor);
    }
}

void DisplayHandler::drawFullStatsList() {
    int startY = CONTENT_Y + HEADER_HEIGHT + 2;
    int endY = CONTENT_Y + CONTENT_HEIGHT - FOOTER_HEIGHT;
    int contentHeight = endY - startY - 2;

    // Clear content area
    tft.fillRect(CONTENT_X, startY, CONTENT_WIDTH, contentHeight, BG_COLOR);

    // Two-column layout: left = summary stats, right = threat list
    int leftW = 150;
    int rightX = CONTENT_X + leftW + 4;
    int rightW = CONTENT_WIDTH - leftW - 6;
    int y = startY + 2;

    uint32_t wifiCount = totalDetections - bleDetections - flockDetections;
    uint32_t uptime = millis() / 1000;

    // === LEFT COLUMN: Summary stats ===
    int lx = CONTENT_X + 4;
    tft.setTextSize(1);

    // Counts with color-coded labels
    tft.setTextColor(WIFI_COLOR);
    tft.setCursor(lx, y);
    tft.printf("WiFi: %d", wifiCount);
    if (totalDetections > 0) {
        tft.setTextColor(TEXT_DIM);
        tft.printf(" (%d%%)", (wifiCount * 100) / totalDetections);
    }

    y += 11;
    tft.setTextColor(BLE_COLOR);
    tft.setCursor(lx, y);
    tft.printf("BLE:  %d", bleDetections);
    if (totalDetections > 0) {
        tft.setTextColor(TEXT_DIM);
        tft.printf(" (%d%%)", (bleDetections * 100) / totalDetections);
    }

    y += 11;
    tft.setTextColor(flockDetections > 0 ? ALERT_COLOR : TEXT_DIM);
    tft.setCursor(lx, y);
    tft.printf("Threats: %d", flockDetections);

    // Divider
    y += 14;
    tft.drawFastHLine(lx, y, leftW - 8, TEXT_DIM);
    y += 4;

    // Unique vs total
    tft.setTextColor(TEXT_DIM);
    tft.setCursor(lx, y);
    tft.print("Unique:");
    tft.setTextColor(ACCENT_COLOR);
    tft.printf(" %d", detections.size());
    tft.setTextColor(TEXT_DIM);
    tft.printf(" / %d", totalDetections);

    // Rate
    y += 11;
    tft.setTextColor(TEXT_DIM);
    tft.setCursor(lx, y);
    tft.print("Rate: ");
    tft.setTextColor(TEXT_COLOR);
    if (uptime >= 60 && totalDetections > 0) {
        float rate = (float)totalDetections / ((float)uptime / 60.0f);
        if (rate >= 10.0f) {
            tft.printf("%d/min", (int)rate);
        } else {
            tft.printf("%.1f/min", rate);
        }
    } else {
        tft.print("--");
    }

    // Top channel
    y += 11;
    uint8_t topCh = 0;
    uint16_t topCount = 0;
    for (int i = 1; i <= 13; i++) {
        if (channelCounts[i] > topCount) {
            topCount = channelCounts[i];
            topCh = i;
        }
    }
    tft.setTextColor(TEXT_DIM);
    tft.setCursor(lx, y);
    tft.print("Top CH: ");
    tft.setTextColor(WIFI_COLOR);
    if (topCh > 0) {
        tft.printf("%d (%d)", topCh, topCount);
    } else {
        tft.print("--");
    }

    // Divider
    y += 14;
    tft.drawFastHLine(lx, y, leftW - 8, TEXT_DIM);
    y += 4;

    // Uptime and log
    tft.setTextColor(TEXT_DIM);
    tft.setCursor(lx, y);
    tft.printf("Up: %02d:%02d:%02d", uptime / 3600, (uptime % 3600) / 60, uptime % 60);

    if (sdCardPresent) {
        y += 11;
        tft.setTextColor(SUCCESS_COLOR);
        tft.setCursor(lx, y);
        tft.printf("Logged: %d", detectionsLogged);
    }

    // === RIGHT COLUMN: Threat list ===
    y = startY + 2;

    // Vertical divider
    tft.drawFastVLine(rightX - 3, startY + 2, contentHeight - 4, TEXT_DIM);

    // Header
    tft.setTextSize(1);
    tft.setTextColor(ALERT_COLOR);
    tft.setCursor(rightX, y);
    tft.print("THREATS");

    if (hadThreat) {
        // Closest / last info
        tft.setTextColor(TEXT_DIM);
        tft.setCursor(rightX + 52, y);
        tft.printf("pk:%ddB", closestThreatRssi);
    }

    y += 12;
    tft.drawFastHLine(rightX, y, rightW, ALERT_COLOR);
    y += 4;

    if (threats.empty()) {
        tft.setTextColor(TEXT_DIM);
        tft.setCursor(rightX + 10, y + 20);
        tft.print("No threats");
        tft.setCursor(rightX + 10, y + 32);
        tft.print("detected");
    } else {
        // List each unique threat
        int maxItems = (endY - y - 4) / 20;  // 20px per threat entry
        int count = min((int)threats.size(), maxItems);

        for (int i = 0; i < count; i++) {
            Detection& t = threats[i];

            // Red dot indicator
            tft.fillCircle(rightX + 3, y + 4, 2, ALERT_COLOR);

            // Name (vendor or SSID)
            tft.setTextColor(TEXT_COLOR);
            tft.setCursor(rightX + 9, y);
            String name = t.vendor.length() > 0 ? t.vendor : t.ssid;
            if (name.length() == 0) name = "Unknown";
            if (name.length() > 16) name = name.substring(0, 13) + "...";
            tft.print(name);

            // RSSI (peak) and time ago
            tft.setTextColor(TEXT_DIM);
            tft.setCursor(rightX + 9, y + 10);
            tft.printf("%ddB ", t.rssi);
            uint32_t ago = (millis() - t.timestamp) / 1000;
            if (ago < 60) {
                tft.printf("%ds", ago);
            } else if (ago < 3600) {
                tft.printf("%dm", ago / 60);
            } else {
                tft.printf("%dh%dm", ago / 3600, (ago % 3600) / 60);
            }

            y += 20;
        }

        // Overflow indicator
        if ((int)threats.size() > maxItems) {
            tft.setTextColor(TEXT_DIM);
            tft.setCursor(rightX + 9, y);
            tft.printf("+%d more", (int)threats.size() - maxItems);
        }
    }
}

void DisplayHandler::clear() {
    tft.fillScreen(TFT_BLACK);
    tft.fillRect(CONTENT_X, CONTENT_Y, CONTENT_WIDTH, CONTENT_HEIGHT, BG_COLOR);
    needsRedraw = true;
}

void DisplayHandler::setBrightness(uint8_t level) {
    brightness = level;
    applyBrightness();
    saveSettings();
}

void DisplayHandler::setRgbBrightness(uint8_t level) {
    rgbBrightness = level;
    FastLED.setBrightness(rgbBrightness);
    FastLED.show();
    saveSettings();
}

bool DisplayHandler::loadSettings() {
    if (!sdCardPresent) return false;
    if (!SD_MMC.exists(SETTINGS_FILE)) {
        Serial.println("  No settings file found, using defaults");
        return false;
    }

    fs::File file = SD_MMC.open(SETTINGS_FILE, FILE_READ);
    if (!file) {
        Serial.println("  Failed to open settings file");
        return false;
    }

    // Read settings: brightness, rgbBrightness
    String line;

    // Line 1: Display brightness
    line = file.readStringUntil('\n');
    if (line.length() > 0) {
        brightness = constrain(line.toInt(), 10, 255);
    }

    // Line 2: RGB LED brightness
    line = file.readStringUntil('\n');
    if (line.length() > 0) {
        rgbBrightness = constrain(line.toInt(), 0, 255);
    }

    file.close();

    // Apply loaded settings
    applyBrightness();
    FastLED.setBrightness(rgbBrightness);

    Serial.printf("  Settings loaded: brightness=%d, rgbBrightness=%d\n", brightness, rgbBrightness);
    return true;
}

bool DisplayHandler::saveSettings() {
    if (!sdCardPresent) return false;

    fs::File file = SD_MMC.open(SETTINGS_FILE, FILE_WRITE);
    if (!file) {
        Serial.println("Failed to save settings");
        return false;
    }

    file.println(brightness);
    file.println(rgbBrightness);
    file.close();

    return true;
}

bool DisplayHandler::initSDCard() {
    Serial.println("Initializing SD Card (SDMMC)...");

    // ESP32-S3 requires explicit SDMMC pin assignment
    // Waveshare 1.47": CLK=14, CMD=15, D0=16
    SD_MMC.setPins(14, 15, 16);

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

    // Track channel stats
    if (currentChannel >= 1 && currentChannel <= 13) {
        channelCounts[currentChannel]++;
    }

    // Update threat log (kept separate, never evicted)
    if (isThreat) {
        bool threatExists = false;
        for (auto& t : threats) {
            if (t.mac == mac) {
                if (rssi > t.rssi) t.rssi = rssi;  // Keep strongest signal
                t.timestamp = millis();
                threatExists = true;
                break;
            }
        }
        if (!threatExists) {
            Detection t;
            t.ssid = ssid;
            t.mac = mac;
            t.vendor = vendor;
            t.rssi = rssi;
            t.type = type;
            t.timestamp = millis();
            t.hitCount = 1;
            t.isNew = true;
            threats.insert(threats.begin(), t);
        }
    }

    // Check if already exists (update if so)
    for (auto& d : detections) {
        if (d.mac == mac) {
            d.rssi = rssi;
            d.timestamp = millis();
            d.hitCount++;
            d.isNew = true;
            totalDetections++;
            if (isThreat) {
                flockDetections++;
                lastThreatTime = millis();
                hadThreat = true;
                if (rssi > closestThreatRssi) closestThreatRssi = rssi;
                setLEDDetection(rssi);
            }
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
    d.hitCount = 1;
    d.isNew = true;

    detections.insert(detections.begin(), d);
    totalDetections++;

    if (isThreat) {
        flockDetections++;
        lastThreatTime = millis();
        hadThreat = true;
        if (rssi > closestThreatRssi) closestThreatRssi = rssi;
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
    threats.clear();
    totalDetections = 0;
    flockDetections = 0;
    bleDetections = 0;
    closestThreatRssi = -127;
    lastThreatTime = 0;
    hadThreat = false;
    memset(channelCounts, 0, sizeof(channelCounts));
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

        case 2:  // Detection - flashing red, rate based on RSSI
            {
                int flashInterval = map(constrain(detectionRssi, -90, -30), -90, -30, 400, 50);
                if (now - lastLedUpdate > (uint32_t)flashInterval) {
                    ledFlashState = !ledFlashState;
                    lastLedUpdate = now;
                }
                leds[0] = ledFlashState ? CRGB::Red : CRGB::Black;

                // No new detection for 10s -> signal lost, transition to orange
                if (now - lastDetectionTime > 10000) {
                    ledState = 3;
                    alertStartTime = now;
                }
            }
            break;

        case 3:  // Alert - solid orange (signal lost, persistent until reboot)
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
    lastDetectionTime = millis();
}

void DisplayHandler::setLEDAlert() {
    ledState = 3;
    alertStartTime = millis();
}

void DisplayHandler::setLEDOff() {
    ledState = 0;
}

void DisplayHandler::showAlert(String message, uint16_t color) {
    int alertW = 200;
    int alertH = 36;
    int x = CONTENT_X + (CONTENT_WIDTH - alertW) / 2;
    int y = CONTENT_Y + (CONTENT_HEIGHT - alertH) / 2;
    tft.fillRect(x, y, alertW, alertH, BG_DARK);
    tft.drawRect(x, y, alertW, alertH, color);
    tft.drawRect(x + 1, y + 1, alertW - 2, alertH - 2, color);
    tft.setTextSize(1);
    tft.setTextColor(color);
    int textX = x + (alertW - message.length() * 6) / 2;
    tft.setCursor(textX, y + 14);
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
