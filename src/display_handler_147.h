/**
 * @file display_handler_147.h
 * @brief Display UI for Waveshare ESP32-S3-LCD-1.47 (172x320 ST7789)
 *
 * 4-page navigation using BOOT button (GPIO 0):
 * - Short press: cycle pages (HOME -> LIST -> STATS -> CONFIG)
 * - Long press: adjust settings (on CONFIG page) or toggle LED
 *
 * Settings persistence to SD card (/settings.txt).
 *
 * Hardware notes:
 * - Display: ST7789 172x320 on SPI (MOSI=45, SCLK=40, CS=42, DC=41, RST=39, BL=48)
 * - RGB LED: WS2812 addressable on GPIO 38
 * - SD Card: SDMMC interface (CMD=15, CLK=14, D0=16, D1=18, D2=17, D3=21)
 * - Boot button: GPIO 0 (active LOW)
 */

#ifndef DISPLAY_HANDLER_147_H
#define DISPLAY_HANDLER_147_H

#ifdef WAVESHARE_147

#include <TFT_eSPI.h>
#include <SPI.h>
#include <SD_MMC.h>
#include <FS.h>
#include <FastLED.h>
#include <vector>
#include <string>

// RGB LED (WS2812)
#define RGB_LED_PIN 38
#define NUM_LEDS 1

// Boot button (GPIO 0)
#define BOOT_BUTTON_PIN 0
#define LONG_PRESS_MS 500
#define DEBOUNCE_MS 50

// Settings persistence
#define SETTINGS_FILE "/settings.txt"

// Modern dark theme color scheme (RGB565)
#define BG_COLOR      0x0841          // Deep charcoal
#define BG_DARK       0x0000          // Pure black
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

// Display zones (320x172 landscape with padding for curved corners)
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 172
#define PADDING 8
#define CONTENT_X PADDING
#define CONTENT_Y PADDING
#define CONTENT_WIDTH (SCREEN_WIDTH - PADDING * 2)
#define CONTENT_HEIGHT (SCREEN_HEIGHT - PADDING * 2)
#define HEADER_HEIGHT 18
#define FOOTER_HEIGHT 14
#define STAT_BOX_HEIGHT 38
#define LIST_ITEM_HEIGHT 22

class DisplayHandler {
private:
    TFT_eSPI tft;
    CRGB leds[NUM_LEDS];

    // Display state
    bool needsRedraw;
    uint32_t lastUpdate;
    uint8_t brightness;
    uint8_t currentPage;

    // Button handling
    bool buttonPressed;
    uint32_t buttonPressTime;
    bool longPressHandled;
    bool adjustMode;            // true = adjusting settings
    uint8_t settingsSelection;  // 0=display, 1=LED, 2=exit
    void handleButton();

    // Detection data
    struct Detection {
        String ssid;
        String mac;
        String vendor;
        int8_t rssi;
        String type;
        uint32_t timestamp;
        uint16_t hitCount;
        bool isNew;
    };

    std::vector<Detection> detections;
    uint32_t totalDetections;
    uint32_t flockDetections;
    uint32_t bleDetections;

    // Stats tracking
    int8_t closestThreatRssi;       // Strongest threat RSSI (closest)
    uint32_t lastThreatTime;        // millis() of last threat
    bool hadThreat;                 // Whether any threat was ever seen
    uint16_t channelCounts[14];     // Detection count per channel (1-13)

    // Threat log (never evicted, kept separate from rolling detection list)
    std::vector<Detection> threats;

    // Auto-scroll state
    int scrollOffset;
    uint32_t lastScrollTime;
    bool scrollPaused;

    // Scan state
    uint8_t currentChannel;
    bool bleScanning;

    // SD Card
    bool sdCardPresent;
    String logFileName;
    uint32_t lastSdCheck;
    uint32_t detectionsLogged;
    void checkSDCard();

    // Brightness control (PWM for backlight)
    uint8_t rgbBrightness;  // RGB LED brightness (0-255)
    void setupBacklightPWM();
    void applyBrightness();

    // Settings persistence
    bool loadSettings();
    bool saveSettings();

    // OUI lookup
    String lookupOUI(const String& mac);
    String lookupOUIFromSD(const String& prefix);
    static const char* lookupEmbeddedOUI(const char* prefix);

    // LED control
    uint8_t ledState;  // 0=off, 1=scanning, 2=detection, 3=alert
    uint32_t lastLedUpdate;
    uint32_t lastDetectionTime;   // When last detection LED was triggered
    uint32_t alertStartTime;      // When alert (orange) state began
    bool ledFlashState;
    int8_t detectionRssi;
    void updateLED();

    // Private drawing methods
    void showBootAnimation();
    void drawHeader();
    void drawFooter();
    void drawStatsPanel();
    void drawDetectionList();
    void drawLatestDetection();
    void drawSignalBars(uint16_t x, uint16_t y, int8_t rssi);
    void drawSettingsPage();
    void drawFullStatsList();

public:
    enum DisplayPage {
        PAGE_MAIN = 0,
        PAGE_LIST,
        PAGE_STATS,
        PAGE_SETTINGS,
        PAGE_COUNT
    };

    DisplayHandler();
    bool begin();
    void update();
    void clear();
    void setBrightness(uint8_t level);
    uint8_t getBrightness() { return brightness; }
    void setRgbBrightness(uint8_t level);
    uint8_t getRgbBrightness() { return rgbBrightness; }

    // Page navigation
    void nextPage();
    void setPage(DisplayPage page);
    DisplayPage getCurrentPage() { return (DisplayPage)currentPage; }

    // SD Card
    bool initSDCard();
    void logDetection(const String& ssid, const String& mac, int8_t rssi, const String& type);
    bool isSDCardPresent() { return sdCardPresent; }
    uint32_t getDetectionsLogged() { return detectionsLogged; }

    // Data management
    void addDetection(String ssid, String mac, int8_t rssi, String type);
    void clearDetections();
    uint32_t getDetectionCount() { return totalDetections; }
    uint32_t getFlockCount() { return flockDetections; }
    uint32_t getBLECount() { return bleDetections; }

    // Status displays
    void showAlert(String message, uint16_t color = TFT_RED);
    void showInfo(String message);
    void updateChannelInfo(uint8_t channel);
    void updateScanMode(bool isBLE);
    void updateScanStatus(bool isScanning);
    void showDebugSSID(String ssid, int8_t rssi, uint8_t channel);
    void showDebugBLE(String name, String mac, int8_t rssi);

    // LED control
    void setLEDScanning();
    void setLEDDetection(int8_t rssi);
    void setLEDAlert();
    void setLEDOff();
};

// Global instance
extern DisplayHandler display;

#endif // WAVESHARE_147
#endif // DISPLAY_HANDLER_147_H
