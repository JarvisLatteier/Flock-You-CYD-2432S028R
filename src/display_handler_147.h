/**
 * @file display_handler_147.h
 * @brief Display UI for Waveshare ESP32-S3-LCD-1.47 (172x320 ST7789)
 *
 * Single-page dashboard optimized for small screen without touch.
 * Auto-scrolling detection list, animated stats display.
 *
 * Hardware notes:
 * - Display: ST7789 172x320 on SPI (MOSI=45, SCLK=40, CS=42, DC=41, RST=39, BL=48)
 * - RGB LED: WS2812 addressable on GPIO 38
 * - SD Card: SDMMC interface (CMD=15, CLK=14, D0=16, D1=18, D2=17, D3=21)
 * - No touch controller
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

// Display zones (172x320)
#define HEADER_HEIGHT 24
#define FOOTER_HEIGHT 20
#define STAT_BOX_HEIGHT 50
#define LIST_ITEM_HEIGHT 28

class DisplayHandler {
private:
    TFT_eSPI tft;
    CRGB leds[NUM_LEDS];

    // Display state
    bool needsRedraw;
    uint32_t lastUpdate;
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

    std::vector<Detection> detections;
    uint32_t totalDetections;
    uint32_t flockDetections;
    uint32_t bleDetections;

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
    void setupBacklightPWM();
    void applyBrightness();

    // OUI lookup
    String lookupOUI(const String& mac);
    String lookupOUIFromSD(const String& prefix);
    static const char* lookupEmbeddedOUI(const char* prefix);

    // LED control
    uint8_t ledState;  // 0=off, 1=scanning, 2=detection, 3=alert
    uint32_t lastLedUpdate;
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

public:
    DisplayHandler();
    bool begin();
    void update();
    void clear();
    void setBrightness(uint8_t level);
    uint8_t getBrightness() { return brightness; }

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
