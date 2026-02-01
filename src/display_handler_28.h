/**
 * @file display_handler_28.h
 * @brief Touchscreen UI for ESP32-2432S028R (2.8" ILI9341 320x240)
 *
 * Provides 5-page navigation (HOME, LIST, STAT, CONF, CAL), touch calibration
 * with SD card persistence, and brightness controls.
 *
 * Hardware notes:
 * - Display on VSPI (SCK=14, MOSI=13, MISO=12, CS=15)
 * - Touch on separate HSPI (CLK=25, MOSI=32, MISO=39, CS=33, IRQ=36)
 * - Dual backlight PWM (GPIO 27 + 21)
 */

#ifndef DISPLAY_HANDLER_28_H
#define DISPLAY_HANDLER_28_H

#ifdef CYD_DISPLAY

#include <TFT_eSPI.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <vector>
#include <string>

// SD Card
#define SD_CS 5

// LDR (Light Dependent Resistor) for auto brightness
#define LDR_PIN 34

// Touch calibration defaults (can be overridden by SD card)
// RAW_Y maps to Screen X, RAW_X maps to Screen Y
#define TOUCH_RAW_Y_MIN_DEFAULT 407
#define TOUCH_RAW_Y_MAX_DEFAULT 3500
#define TOUCH_RAW_X_MIN_DEFAULT 604
#define TOUCH_RAW_X_MAX_DEFAULT 3571
#define TOUCH_CAL_FILE "/touch_cal.txt"
#define OUI_FILE "/oui.csv"

// Modern dark theme color scheme (RGB565)
#define BG_COLOR      0x0841          // Deep charcoal (#080808 -> darker feel)
#define BG_DARK       0x0000          // Pure black for contrast areas
#define TEXT_COLOR    0xFFFF          // White text
#define TEXT_DIM      0x8410          // Dimmed gray text
#define WIFI_COLOR    0x04FF          // Bright blue for WiFi
#define BLE_COLOR     0x781F          // Purple for BLE
#define ALERT_COLOR   0xF800          // Red for Flock alerts
#define ALERT_WARN    0xFD20          // Orange for warnings
#define SUCCESS_COLOR 0x07E0          // Green for success/scanning
#define HEADER_COLOR  0x10A2          // Dark blue-gray header
#define FOOTER_COLOR  0x0861          // Slightly lighter footer
#define ACCENT_COLOR  0x04FF          // Blue accent
#define BUTTON_ACTIVE 0x2945          // Active button background

// Legacy color aliases
#define INFO_COLOR    ACCENT_COLOR
#define WARNING_COLOR ALERT_WARN

// Display zones (adjusted for 320x240)
#define HEADER_HEIGHT 36
#define FOOTER_HEIGHT 32
#define LIST_ITEM_HEIGHT 24

// Touch zones
struct TouchZone {
    uint16_t x1, y1, x2, y2;
    void (*callback)();
    const char* label;
};

class DisplayHandler {
private:
    TFT_eSPI tft;
    SPIClass touchSPI;  // Separate HSPI for touch

    // Touch reading
    bool readTouchRaw(uint16_t &rawX, uint16_t &rawY);
    void mapTouchToScreen(uint16_t rawX, uint16_t rawY, int16_t &screenX, int16_t &screenY);

    // Display state
    bool needsRedraw;
    uint32_t lastUpdate;
    uint8_t currentPage;
    uint8_t brightness;

    // Detection data
    struct Detection {
        String ssid;
        String mac;
        String vendor;
        int8_t rssi;
        String type;
        uint32_t timestamp;
        bool isNew;
    };

    // OUI lookup
    String lookupOUI(const String& mac);
    String lookupOUIFromSD(const String& prefix);
    static const char* lookupEmbeddedOUI(const char* prefix);

    std::vector<Detection> detections;
    uint32_t totalDetections;
    uint32_t flockDetections;
    uint32_t bleDetections;

    // Touch handling
    std::vector<TouchZone> touchZones;
    uint32_t lastTouchTime;
    bool touchDebounce;

    // Scan state
    uint8_t currentChannel;
    bool bleScanning;  // true = BLE mode, false = WiFi mode

    // SD Card
    bool sdCardPresent;
    String logFileName;
    uint32_t lastSdCheck;
    void checkSDCard();
    uint32_t detectionsLogged;

    // Brightness control (PWM)
    bool autoBrightness;
    uint8_t rgbBrightness;  // RGB LED brightness (0-255)
    uint32_t lastLdrRead;
    bool ledAlertsEnabled;  // LED alert toggle
    void setupBacklightPWM();
    void applyBrightness();
    void updateAutoBrightness();

    // Touch calibration (runtime values, can be loaded from SD)
    uint16_t touchRawYMin, touchRawYMax;
    uint16_t touchRawXMin, touchRawXMax;
    uint8_t calStep;  // 0=TL, 1=TR, 2=BL, 3=BR, 4=done
    uint16_t calRawX[4], calRawY[4];  // Raw values for each corner
    bool loadCalibration();
    void startCalibration();
    void processCalibrationTouch(uint16_t rawX, uint16_t rawY);
    bool validateAndApplyCalibration();

    // Private methods
    void drawHeader();
    void drawFooter();
    void clearContentArea();
    void drawMainPage();
    void drawListPage();
    void drawStatsPage();
    void drawSettingsPage();
    void drawAboutPage();
    void drawCalibrationPage();
    void handleCalibrationTouch();
    void drawProgressBar(uint16_t x, uint16_t y, uint16_t w, uint16_t h, float progress, uint16_t color);
    void drawSignalStrength(uint16_t x, uint16_t y, int8_t rssi);
    void handleTouch();
    void addTouchZone(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, void (*callback)(), const char* label);
    void clearTouchZones();

public:
    enum DisplayPage {
        PAGE_MAIN = 0,
        PAGE_LIST,
        PAGE_STATS,
        PAGE_SETTINGS,
        PAGE_ABOUT,
        PAGE_CALIBRATE
    };

    DisplayHandler();
    bool begin();
    void update();
    void clear();
    void setBrightness(uint8_t level);
    void increaseBrightness();
    void decreaseBrightness();
    uint8_t getBrightness() { return brightness; }
    void toggleAutoBrightness();
    bool isAutoBrightness() { return autoBrightness; }
    void setRgbBrightness(uint8_t level);
    void increaseRgbBrightness();
    void decreaseRgbBrightness();
    uint8_t getRgbBrightness() { return rgbBrightness; }
    void toggleLedAlerts();
    bool isLedAlertsEnabled() { return ledAlertsEnabled; }

    // SD Card
    bool initSDCard();
    void logDetection(const String& ssid, const String& mac, int8_t rssi, const String& type);
    bool isSDCardPresent() { return sdCardPresent; }
    uint32_t getDetectionsLogged() { return detectionsLogged; }
    bool saveCalibration();

    // Data management
    void addDetection(String ssid, String mac, int8_t rssi, String type);
    void clearDetections();
    uint32_t getDetectionCount() { return totalDetections; }
    uint32_t getFlockCount() { return flockDetections; }
    uint32_t getBLECount() { return bleDetections; }

    // Page navigation
    void nextPage();
    void previousPage();
    void setPage(DisplayPage page);
    DisplayPage getCurrentPage() { return (DisplayPage)currentPage; }

    // Status displays
    void showAlert(String message, uint16_t color = TFT_RED);
    void showInfo(String message);
    void updateChannelInfo(uint8_t channel);
    void updateScanMode(bool isBLE);  // true = BLE scanning, false = WiFi
    void updateScanStatus(bool isScanning);
    void showDebugSSID(String ssid, int8_t rssi, uint8_t channel);
    void showDebugBLE(String name, String mac, int8_t rssi);
};

// Global instance
extern DisplayHandler display;

// Touch callbacks
void onMainButtonPress();
void onListButtonPress();
void onStatsButtonPress();
void onSettingsButtonPress();
void onClearButtonPress();
void onBrightnessUp();
void onBrightnessDown();
void onAutoBrightnessToggle();
void onRgbBrightnessUp();
void onRgbBrightnessDown();
void onCalibratePress();
void onCalibrateSave();
void onLedAlertToggle();

#endif // CYD_DISPLAY
#endif // DISPLAY_HANDLER_28_H
