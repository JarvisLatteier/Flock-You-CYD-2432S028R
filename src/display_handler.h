#ifndef DISPLAY_HANDLER_H
#define DISPLAY_HANDLER_H

#ifdef CYD_DISPLAY

#include <Arduino_GFX_Library.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <Preferences.h>
#include <vector>
#include <string>
#include <algorithm>

// SD Card Configuration for CYD
#define SD_CS 5
#define SD_MOSI 23
#define SD_MISO 19
#define SD_SCK 18

// Touch calibration struct
struct Cal {
    float a=1, b=0, c=0, d=0, e=1, f=0;
    bool valid=false;
};

// Cyberpunk/Hacker color scheme (Arduino_GFX uses RGB565)
#define BG_COLOR 0x0000        // Pure black
#define TEXT_COLOR 0x07FF      // Neon cyan
#define ALERT_COLOR 0xF81F     // Neon magenta/pink
#define SUCCESS_COLOR 0x07E0   // Neon green
#define WARNING_COLOR 0xFFE0   // Bright yellow
#define INFO_COLOR 0x07FF      // Neon cyan
#define HEADER_COLOR 0x4810    // Dark purple
#define ACCENT_COLOR 0xF81F    // Neon magenta
#define PANEL_DARK 0x2104      // Very dark purple
#define PANEL_DARKER 0x1082    // Even darker purple

// Display zones
#define HEADER_HEIGHT 40
#define FOOTER_HEIGHT 30
#define LIST_ITEM_HEIGHT 35
#define MAX_DISPLAY_ITEMS 10

// Touch zones
struct TouchZone {
    uint16_t x1, y1, x2, y2;
    void (*callback)();
    String label;
};

class DisplayHandler {
private:
    Arduino_DataBus *bus;
    Arduino_GFX *gfx;
    Preferences prefs;
    Cal gCal;

    // Display state
    bool displayActive;
    bool needsRedraw;
    uint32_t lastUpdate;
    uint8_t currentPage;
    uint8_t brightness;

    // Detection data
    struct Detection {
        String ssid;
        String mac;
        int8_t rssi;
        String type;
        uint32_t timestamp;
        bool isNew;
    };

    // Seen SSIDs list (for WiFi scanner display)
    struct SeenSSID {
        String ssid;
        int8_t rssi;
        uint8_t channel;
        uint32_t lastSeen;
    };

    // Seen BLE devices list
    struct SeenBLE {
        String name;
        String mac;
        int8_t rssi;
        uint32_t lastSeen;
    };

    std::vector<Detection> detections;
    std::vector<SeenSSID> seenSSIDs;
    std::vector<SeenBLE> seenBLE;
    uint32_t totalDetections;
    uint32_t flockDetections;
    uint32_t bleDetections;

    // Debug info
    uint8_t currentChannel;
    String lastSSID;
    int8_t lastRSSI;

    // Touch handling
    std::vector<TouchZone> touchZones;
    uint32_t lastTouchTime;
    bool touchDebounce;

    // SD Card
    bool sdCardAvailable;
    String currentLogFile;

    // Flash alert state
    bool isFlashing;
    uint32_t flashStartTime;
    bool flashState;

    // Display pages
public:
    enum DisplayPage {
        PAGE_MAIN = 0,
        PAGE_LIST,
        PAGE_STATS,
        PAGE_SETTINGS,
        PAGE_ABOUT
    };

private:

    // Private methods
    void drawHeader();
    void drawFooter();
    void drawMainPage();
    void drawListPage();
    void drawStatsPage();
    void drawSettingsPage();
    void drawAboutPage();
    void drawButton(uint16_t x, uint16_t y, uint16_t w, uint16_t h, String label, uint16_t color);
    void drawProgressBar(uint16_t x, uint16_t y, uint16_t w, uint16_t h, float progress, uint16_t color);
    void drawSignalStrength(uint16_t x, uint16_t y, int8_t rssi);
    void handleTouch();
    void runCalibration();
    bool xptReadRaw(uint16_t &rx, uint16_t &ry, uint16_t &z);
    bool touchReadScreen(int16_t &sx, int16_t &sy);
    bool mapRawToScreen(uint16_t rx, uint16_t ry, int16_t &sx, int16_t &sy);
    bool loadCal();
    void saveCal();
    void touchBegin();
    void addTouchZone(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, void (*callback)(), String label);
    void clearTouchZones();

    // SD Card methods
    bool initSDCard();
    void saveDetectionToSD(const Detection& det);
    String createLogFileName();

public:
    DisplayHandler();
    bool begin();
    void update();
    void clear();
    void setBrightness(uint8_t level);
    void sleep();
    void wake();

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

    // Status displays
    void showAlert(String message, uint16_t color = RED);
    void showInfo(String message);
    void showProgress(String message, float progress);
    void updateStatus(String status);
    void updateChannelInfo(uint8_t channel);
    void updateScanStatus(bool isScanning);
    void showDebugSSID(String ssid, int8_t rssi, uint8_t channel);
    void showDebugBLE(String name, String mac, int8_t rssi);

    // Settings
    void setRotation(uint8_t rotation);
    void enableTouch(bool enable);
    void setUpdateInterval(uint32_t interval);

    // SD Card status
    bool isSDCardAvailable() { return sdCardAvailable; }
};

// Global instance
extern DisplayHandler display;

// Touch callbacks
void onMainButtonPress();
void onListButtonPress();
void onStatsButtonPress();
void onSettingsButtonPress();
void onClearButtonPress();
void onBuzzerToggle();

#endif // CYD_DISPLAY
#endif // DISPLAY_HANDLER_H