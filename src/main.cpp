/**
 * @file main.cpp
 * @brief Flock You - Surveillance Camera Detection System
 *
 * Detects Flock Safety cameras and similar surveillance devices using
 * WiFi promiscuous mode and BLE scanning. Outputs JSON over serial and
 * provides visual alerts via RGB LED.
 *
 * Target: ESP32-2432S028R (2.8" CYD)
 *
 * @see https://github.com/JarvisLatteier/Flock-You-CYD-2432S028R
 */

#include <Arduino.h>
#include <WiFi.h>
#include <NimBLEDevice.h>
#include <NimBLEScan.h>
#include <NimBLEAdvertisedDevice.h>
#include <ArduinoJson.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdint.h>
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_task_wdt.h"

#ifdef CYD_DISPLAY
#include "display_handler_28.h"
#endif

#ifdef WAVESHARE_147
#include "display_handler_147.h"
#endif

// Unified display macro for code that works with both display types
#if defined(CYD_DISPLAY) || defined(WAVESHARE_147)
#define HAS_DISPLAY 1
#endif

// ============================================================================
// CONFIGURATION
// ============================================================================

// Hardware Configuration
#define BUZZER_ENABLED 0  // Set to 1 to enable buzzer, 0 to disable

#ifdef CYD_DISPLAY
#define BUZZER_PIN 22  // GPIO22 - Available GPIO on CYD board
#else
#define BUZZER_PIN 3   // GPIO3 (D2) - PWM capable pin on Xiao ESP32 S3
#endif

// RGB LED — active LOW on ESP32-2432S028R (PWM for brightness)
// Waveshare 1.47" uses WS2812 addressable LED instead (handled by display handler)
#ifndef WAVESHARE_147
#define RGB_R  4
#define RGB_G  16
#define RGB_B  17
#define LED_CH_R  0  // LEDC channel for red
#define LED_CH_G  1  // LEDC channel for green
#define LED_CH_B  2  // LEDC channel for blue
#endif

// LED States
enum LedState {
    LED_SCANNING,    // Green at 50% - no detections
    LED_DETECTED,    // Red flashing - active detection
    LED_ALERT        // Orange solid - recent detection, signal lost
};

static LedState led_state = LED_SCANNING;
static bool     led_flash_on = false;
static uint32_t led_last_toggle = 0;
static uint32_t led_detection_time = 0;
static int8_t   led_detection_rssi = -100;  // Signal strength for flash rate
#define LED_ALERT_TIMEOUT  15000  // 15s after detection before returning to scanning
#define LED_FLASH_MIN_INTERVAL  50   // Fastest flash (strong signal)
#define LED_FLASH_MAX_INTERVAL  400  // Slowest flash (weak signal)

// Audio Configuration
#define LOW_FREQ 200      // Boot sequence - low pitch
#define HIGH_FREQ 800     // Boot sequence - high pitch & detection alert
#define DETECT_FREQ 1000  // Detection alert - high pitch (faster beeps)
#define HEARTBEAT_FREQ 600 // Heartbeat pulse frequency
#define BOOT_BEEP_DURATION 300   // Boot beep duration
#define DETECT_BEEP_DURATION 150 // Detection beep duration (faster)
#define HEARTBEAT_DURATION 100   // Short heartbeat pulse

// WiFi Promiscuous Mode Configuration
#define MAX_CHANNEL 13
// BLE SCANNING CONFIGURATION
#define BLE_SCAN_DURATION 1    // Seconds
#define BLE_SCAN_INTERVAL 2000 // Milliseconds between scans (was 5000)
static unsigned long last_ble_scan = 0;

// Detection Pattern Limits
#define MAX_SSID_PATTERNS 10
#define MAX_MAC_PATTERNS 50
#define MAX_DEVICE_NAMES 20

// ============================================================================
// DETECTION PATTERNS (Extracted from Real Flock Safety Device Databases)
// ============================================================================

// WiFi SSID patterns to detect (case-insensitive via strcasestr)
static const char* wifi_ssid_patterns[] = {
    "flock",          // Flock Safety (matches: flock, Flock, FLOCK, flock-test, etc.)
    "fs ext battery", // Flock Safety Extended Battery devices
    "penguin",        // Penguin surveillance devices
    "pigvision"       // Pigvision surveillance systems
};

// Known Flock Safety MAC address prefixes (from real device databases)
static const char* mac_prefixes[] = {
    // FS Ext Battery devices
    "58:8e:81", "cc:cc:cc", "ec:1b:bd", "90:35:ea", "04:0d:84", 
    "f0:82:c0", "1c:34:f1", "38:5b:44", "94:34:69", "b4:e3:f9",
    
    // Flock WiFi devices
    "70:c9:4e", "3c:91:80", "d8:f3:bc", "80:30:49", "14:5a:fc",
    "74:4c:a1", "08:3a:88", "9c:2f:9d", "94:08:53", "e4:aa:ea"
    
    // Penguin devices - these are NOT OUI based, so use local ouis
    // from the wigle.net db relative to your location 
    // "cc:09:24", "ed:c7:63", "e8:ce:56", "ea:0c:ea", "d8:8f:14",
    // "f9:d9:c0", "f1:32:f9", "f6:a0:76", "e4:1c:9e", "e7:f2:43",
    // "e2:71:33", "da:91:a9", "e1:0e:15", "c8:ae:87", "f4:ed:b2",
    // "d8:bf:b5", "ee:8f:3c", "d7:2b:21", "ea:5a:98"
};

// Device name patterns for BLE advertisement detection (case-insensitive via strcasestr)
static const char* device_name_patterns[] = {
    "fs ext battery",  // Flock Safety Extended Battery
    "penguin",         // Penguin surveillance devices
    "flock",           // Standard Flock Safety devices
    "pigvision"        // Pigvision surveillance systems
};

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================

static uint8_t current_channel = 1;
static unsigned long last_channel_hop = 0;
static bool triggered = false;
static bool device_in_range = false;
static unsigned long last_detection_time = 0;
static uint32_t total_frames_seen = 0;
static uint32_t total_ssids_seen = 0;
static unsigned long last_heartbeat = 0;
static NimBLEScan* pBLEScan;

// Tracked device table: FNV-1a hash + per-device metadata
#define MAX_TRACKED 64
#define MAX_TRACKED_MASK (MAX_TRACKED - 1)
#define HASH_MAX_PROBE 8
#define DETECTION_TTL 300000  // 5 minutes — re-detect after this

// TrackedDevice struct defined in display_handler_28.h when CYD_DISPLAY is set
#ifndef CYD_DISPLAY
struct TrackedDevice {
    uint32_t mac_hash;           // FNV-1a hash (0 = empty slot)
    uint8_t  mac[6];             // Full MAC for display/logging
    int8_t   rssi_min;           // Weakest signal seen
    int8_t   rssi_max;           // Strongest signal seen
    int8_t   rssi_last;          // Most recent RSSI
    int32_t  rssi_sum;           // Running sum for average
    uint16_t hit_count;          // Total detections
    uint8_t  last_channel;       // Channel last seen on
    uint8_t  type;               // Last detection type
    uint32_t first_seen;         // millis() of first detection
    uint32_t last_seen;          // millis() of most recent detection
    uint32_t probe_interval_sum; // Sum of inter-detection intervals (ms)
    uint16_t probe_intervals;    // Count of intervals measured
};
#endif

static TrackedDevice tracked_devices[MAX_TRACKED] = {};
static uint32_t hash_entries = 0;
static uint32_t hash_collisions = 0;

// Channel memory: detection-aware channel biasing
static uint8_t  channel_detections[14] = {0};   // Matched detections per channel (lifetime)
static volatile uint32_t channel_sticky_until = 0;  // millis() — stay on current channel
#define CHANNEL_STICKY_DURATION 5000             // 5s sticky after detection
#define CHANNEL_DETECTION_BONUS 500              // Extra ms dwell per past detection
#define CHANNEL_MAX_DWELL 3000                   // Cap total dwell time

// FNV-1a hash of 6-byte MAC address
static uint32_t fnv1a_mac(const uint8_t* mac) {
    uint32_t hash = 2166136261u;  // FNV offset basis
    for (int i = 0; i < 6; i++) {
        hash ^= mac[i];
        hash *= 16777619u;  // FNV prime
    }
    // Ensure hash is never 0 (0 = empty slot sentinel)
    return hash ? hash : 1;
}

// FreeRTOS queue for detection events
struct DetectionEvent {
    uint8_t mac[6];
    char ssid[33];        // WiFi SSID or BLE name
    int8_t rssi;
    uint8_t channel;
    uint8_t type;         // 0=probe, 1=beacon, 2=ble_mac, 3=ble_name
};

static QueueHandle_t detectionQueue = NULL;
static TaskHandle_t processingTaskHandle = NULL;
static SemaphoreHandle_t displayMutex = NULL;
static uint32_t events_processed = 0;
static uint32_t events_dropped = 0;
static volatile bool pending_beep = false;  // Signal Core 1 to play detection beep

// Adaptive channel dwell
static volatile uint16_t channel_activity[14] = {0};  // frames per channel in current dwell
#define CHANNEL_DWELL_BASE    200   // ms - fast sweep of quiet channels
#define CHANNEL_DWELL_ACTIVE  800   // ms - stay longer on active channels
#define CHANNEL_DWELL_HIGH   1500   // ms - stay longest on very active channels
#define CHANNEL_ACTIVE_THRESHOLD  5
#define CHANNEL_HIGH_THRESHOLD   20



// ============================================================================
// AUDIO SYSTEM
// ============================================================================

void beep(int frequency, int duration_ms)
{
#if !BUZZER_ENABLED
    (void)frequency; (void)duration_ms;  // Suppress unused warnings
    return;
#else
#ifdef CYD_DISPLAY
    // CYD may not have a buzzer by default, implement visual/audio feedback
    // You can add an external buzzer to GPIO22
    if (digitalRead(BUZZER_PIN) == HIGH) {  // Check if buzzer is enabled
        tone(BUZZER_PIN, frequency, duration_ms);
        delay(duration_ms + 50);
    }
#else
    tone(BUZZER_PIN, frequency, duration_ms);
    delay(duration_ms + 50);
#endif
#endif
}

// ============================================================================
// RGB LED ALERT
// ============================================================================

// Set RGB LED using PWM (active LOW, so invert values)
// Applies rgbBrightness scaling when display handler is available
// Note: Waveshare 1.47" uses WS2812 LED handled by display handler
#ifndef WAVESHARE_147
static inline void rgb_pwm(uint8_t r, uint8_t g, uint8_t b) {
#ifdef CYD_DISPLAY
    // Scale by RGB brightness setting (0-255)
    uint8_t scale = display.getRgbBrightness();
    r = (r * scale) / 255;
    g = (g * scale) / 255;
    b = (b * scale) / 255;
#endif
    // Active LOW: 255 = off, 0 = full brightness
    ledcWrite(LED_CH_R, 255 - r);
    ledcWrite(LED_CH_G, 255 - g);
    ledcWrite(LED_CH_B, 255 - b);
}

// Simple on/off for backward compatibility
static inline void rgb_set(bool r, bool g, bool b) {
    rgb_pwm(r ? 255 : 0, g ? 255 : 0, b ? 255 : 0);
}

// Initialize RGB LED PWM
void led_init(void) {
    ledcSetup(LED_CH_R, 5000, 8);  // 5kHz, 8-bit
    ledcSetup(LED_CH_G, 5000, 8);
    ledcSetup(LED_CH_B, 5000, 8);
    ledcAttachPin(RGB_R, LED_CH_R);
    ledcAttachPin(RGB_G, LED_CH_G);
    ledcAttachPin(RGB_B, LED_CH_B);
    rgb_pwm(0, 0, 0);  // Start off
}
#else
// Waveshare stub functions - LED handled by display handler
static inline void rgb_pwm(uint8_t r, uint8_t g, uint8_t b) { (void)r; (void)g; (void)b; }
static inline void rgb_set(bool r, bool g, bool b) { (void)r; (void)g; (void)b; }
void led_init(void) { }
#endif

// Call on detection — triggers red flash state
void led_flash_trigger(int8_t rssi) {
    led_state = LED_DETECTED;
    led_detection_time = millis();
    led_detection_rssi = rssi;
    led_flash_on = true;
    led_last_toggle = millis();
#ifdef WAVESHARE_147
    // Mutex already held by caller (processingTask) or acquired here
    if (displayMutex && xSemaphoreTake(displayMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        display.setLEDDetection(rssi);
        xSemaphoreGive(displayMutex);
    }
#else
    rgb_pwm(255, 0, 0);  // Immediate red flash
#endif
}

// Overload for backward compatibility
void led_flash_trigger(void) {
    led_flash_trigger(-70);  // Default RSSI
}

// Calculate flash interval based on signal strength
uint32_t get_flash_interval(int8_t rssi) {
    // Stronger signal (less negative) = faster flash
    // RSSI typically -30 (very strong) to -90 (very weak)
    if (rssi >= -40) return LED_FLASH_MIN_INTERVAL;      // Very strong: 50ms
    if (rssi >= -50) return 100;
    if (rssi >= -60) return 150;
    if (rssi >= -70) return 200;
    if (rssi >= -80) return 300;
    return LED_FLASH_MAX_INTERVAL;  // Weak: 400ms
}

// Call every loop iteration — handles LED state machine
void led_flash_update(void) {
    uint32_t now = millis();

#ifdef WAVESHARE_147
    // Waveshare LED is handled by display handler's update() method
    // Just track state transitions here (called from loop on Core 1)
    if (led_state == LED_DETECTED && now - led_detection_time >= 5000) {
        led_state = LED_ALERT;
        if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            display.setLEDAlert();
            xSemaphoreGive(displayMutex);
        }
    }
    if (led_state == LED_ALERT && now - led_detection_time >= LED_ALERT_TIMEOUT) {
        led_state = LED_SCANNING;
        if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            display.setLEDScanning();
            xSemaphoreGive(displayMutex);
        }
    }
    return;
#endif

#ifdef CYD_DISPLAY
    // Check if LED alerts are disabled
    if (!display.isLedAlertsEnabled()) {
        rgb_pwm(0, 0, 0);  // LED off when disabled
        return;
    }
#endif

    switch (led_state) {
        case LED_SCANNING:
            // Green at 50% brightness
            rgb_pwm(0, 128, 0);
            break;

        case LED_DETECTED: {
            // Red flashing, speed based on signal strength
            uint32_t interval = get_flash_interval(led_detection_rssi);

            if (now - led_last_toggle >= interval) {
                led_flash_on = !led_flash_on;
                rgb_pwm(led_flash_on ? 255 : 0, 0, 0);
                led_last_toggle = now;
            }

            // After 5 seconds of no new detection, transition to alert
            if (now - led_detection_time >= 5000) {
                led_state = LED_ALERT;
            }
            break;
        }

        case LED_ALERT:
            // Orange solid (red + green)
            rgb_pwm(255, 100, 0);

            // After timeout, return to scanning
            if (now - led_detection_time >= LED_ALERT_TIMEOUT) {
                led_state = LED_SCANNING;
            }
            break;
    }
}

void boot_beep_sequence()
{
    printf("Initializing audio system...\n");
    printf("Playing boot sequence: Low -> High pitch\n");
    beep(LOW_FREQ, BOOT_BEEP_DURATION);
    beep(HIGH_FREQ, BOOT_BEEP_DURATION);
    printf("Audio system ready\n\n");
}

void flock_detected_beep_sequence()
{
    printf("FLOCK SAFETY DEVICE DETECTED!\n");
    printf("Playing alert sequence: 3 fast high-pitch beeps\n");
    for (int i = 0; i < 3; i++) {
        beep(DETECT_FREQ, DETECT_BEEP_DURATION);
        if (i < 2) delay(50); // Short gap between beeps
    }
    printf("Detection complete - device identified!\n\n");
    
    // Mark device as in range and start heartbeat tracking
    device_in_range = true;
    last_detection_time = millis();
    last_heartbeat = millis();
}

void heartbeat_pulse()
{
    printf("Heartbeat: Device still in range\n");
    beep(HEARTBEAT_FREQ, HEARTBEAT_DURATION);
    delay(100);
    beep(HEARTBEAT_FREQ, HEARTBEAT_DURATION);
}

// ============================================================================
// JSON OUTPUT FUNCTIONS
// ============================================================================

void output_wifi_detection_json(const char* ssid, const uint8_t* mac, int rssi, const char* detection_type, TrackedDevice* dev = nullptr)
{
    DynamicJsonDocument doc(2048);

    // Core detection info
    doc["timestamp"] = millis();
    doc["detection_time"] = String(millis() / 1000.0, 3) + "s";
    doc["protocol"] = "wifi";
    doc["detection_method"] = detection_type;
    doc["alert_level"] = "HIGH";
    doc["device_category"] = "FLOCK_SAFETY";

    // WiFi specific info
    doc["ssid"] = ssid;
    doc["ssid_length"] = strlen(ssid);
    doc["rssi"] = rssi;
    doc["signal_strength"] = rssi > -50 ? "STRONG" : (rssi > -70 ? "MEDIUM" : "WEAK");
    doc["channel"] = current_channel;

    // MAC address info
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    doc["mac_address"] = mac_str;

#ifdef HAS_DISPLAY
    // Add detection to display (mutex for thread safety with Core 1 display.update())
    if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
#ifdef CYD_DISPLAY
        display.addDetection(String(ssid), String(mac_str), rssi, String(detection_type), dev);
#else
        display.addDetection(String(ssid), String(mac_str), rssi, String(detection_type));
#endif
        xSemaphoreGive(displayMutex);
    }
#endif

    char mac_prefix[9];
    snprintf(mac_prefix, sizeof(mac_prefix), "%02x:%02x:%02x", mac[0], mac[1], mac[2]);
    doc["mac_prefix"] = mac_prefix;
    doc["vendor_oui"] = mac_prefix;

    // Detection pattern matching
    bool ssid_match = false;
    bool mac_match = false;

    for (size_t i = 0; i < sizeof(wifi_ssid_patterns)/sizeof(wifi_ssid_patterns[0]); i++) {
        if (strcasestr(ssid, wifi_ssid_patterns[i])) {
            doc["matched_ssid_pattern"] = wifi_ssid_patterns[i];
            doc["ssid_match_confidence"] = "HIGH";
            ssid_match = true;
            break;
        }
    }

    for (size_t i = 0; i < sizeof(mac_prefixes)/sizeof(mac_prefixes[0]); i++) {
        if (strncasecmp(mac_prefix, mac_prefixes[i], 8) == 0) {
            doc["matched_mac_pattern"] = mac_prefixes[i];
            doc["mac_match_confidence"] = "HIGH";
            mac_match = true;
            break;
        }
    }

    // Detection summary
    doc["detection_criteria"] = ssid_match && mac_match ? "SSID_AND_MAC" : (ssid_match ? "SSID_ONLY" : "MAC_ONLY");
    doc["threat_score"] = ssid_match && mac_match ? 100 : (ssid_match || mac_match ? 85 : 70);

    // Frame type details
    if (strcmp(detection_type, "probe_request") == 0 || strcmp(detection_type, "probe_request_mac") == 0) {
        doc["frame_type"] = "PROBE_REQUEST";
        doc["frame_description"] = "Device actively scanning for networks";
    } else if (strcmp(detection_type, "probe_response") == 0 || strcmp(detection_type, "probe_response_mac") == 0) {
        doc["frame_type"] = "PROBE_RESPONSE";
        doc["frame_description"] = "Device responding to network scan";
    } else {
        doc["frame_type"] = "BEACON";
        doc["frame_description"] = "Device advertising its network";
    }

    // Enriched tracking data
    if (dev) {
        doc["rssi_min"] = dev->rssi_min;
        doc["rssi_max"] = dev->rssi_max;
        doc["rssi_avg"] = dev->hit_count > 0 ? (int)(dev->rssi_sum / dev->hit_count) : rssi;
        doc["hit_count"] = dev->hit_count;
        if (dev->probe_intervals > 0) {
            doc["avg_probe_interval_ms"] = dev->probe_interval_sum / dev->probe_intervals;
        }
        int8_t range = dev->rssi_max - dev->rssi_min;
        doc["signal_trend"] = range < 10 ? "stable" : (range < 20 ? "moderate" : "moving");
    }

    String json_output;
    serializeJson(doc, json_output);
    Serial.println(json_output);
}

void output_ble_detection_json(const char* mac, const char* name, int rssi, const char* detection_method, TrackedDevice* dev = nullptr)
{
#ifdef HAS_DISPLAY
    // Add BLE detection to display (mutex for thread safety with Core 1 display.update())
    if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
#ifdef CYD_DISPLAY
        display.addDetection(name ? String(name) : "Unknown", String(mac), rssi, "BLE", dev);
#else
        display.addDetection(name ? String(name) : "Unknown", String(mac), rssi, "BLE");
#endif
        xSemaphoreGive(displayMutex);
    }
#endif

    DynamicJsonDocument doc(2048);

    // Core detection info
    doc["timestamp"] = millis();
    doc["detection_time"] = String(millis() / 1000.0, 3) + "s";
    doc["protocol"] = "bluetooth_le";
    doc["detection_method"] = detection_method;
    doc["alert_level"] = "HIGH";
    doc["device_category"] = "FLOCK_SAFETY";

    // BLE specific info
    doc["mac_address"] = mac;
    doc["rssi"] = rssi;
    doc["signal_strength"] = rssi > -50 ? "STRONG" : (rssi > -70 ? "MEDIUM" : "WEAK");

    // Device name info
    if (name && strlen(name) > 0) {
        doc["device_name"] = name;
        doc["device_name_length"] = strlen(name);
        doc["has_device_name"] = true;
    } else {
        doc["device_name"] = "";
        doc["device_name_length"] = 0;
        doc["has_device_name"] = false;
    }

    // MAC address analysis
    char mac_prefix[9];
    strncpy(mac_prefix, mac, 8);
    mac_prefix[8] = '\0';
    doc["mac_prefix"] = mac_prefix;
    doc["vendor_oui"] = mac_prefix;

    // Detection pattern matching
    bool name_match = false;
    bool mac_match = false;

    // Check MAC prefix patterns
    for (size_t i = 0; i < sizeof(mac_prefixes)/sizeof(mac_prefixes[0]); i++) {
        if (strncasecmp(mac, mac_prefixes[i], strlen(mac_prefixes[i])) == 0) {
            doc["matched_mac_pattern"] = mac_prefixes[i];
            doc["mac_match_confidence"] = "HIGH";
            mac_match = true;
            break;
        }
    }

    // Check device name patterns
    if (name && strlen(name) > 0) {
        for (size_t i = 0; i < sizeof(device_name_patterns)/sizeof(device_name_patterns[0]); i++) {
            if (strcasestr(name, device_name_patterns[i])) {
                doc["matched_name_pattern"] = device_name_patterns[i];
                doc["name_match_confidence"] = "HIGH";
                name_match = true;
                break;
            }
        }
    }

    // Detection summary
    doc["detection_criteria"] = name_match && mac_match ? "NAME_AND_MAC" :
                               (name_match ? "NAME_ONLY" : "MAC_ONLY");
    doc["threat_score"] = name_match && mac_match ? 100 :
                         (name_match || mac_match ? 85 : 70);

    // BLE advertisement type analysis
    doc["advertisement_type"] = "BLE_ADVERTISEMENT";
    doc["advertisement_description"] = "Bluetooth Low Energy device advertisement";

    // Detection method details
    if (strcmp(detection_method, "mac_prefix") == 0) {
        doc["primary_indicator"] = "MAC_ADDRESS";
        doc["detection_reason"] = "MAC address matches known Flock Safety prefix";
    } else if (strcmp(detection_method, "device_name") == 0) {
        doc["primary_indicator"] = "DEVICE_NAME";
        doc["detection_reason"] = "Device name matches Flock Safety pattern";
    }

    // Enriched tracking data
    if (dev) {
        doc["rssi_min"] = dev->rssi_min;
        doc["rssi_max"] = dev->rssi_max;
        doc["rssi_avg"] = dev->hit_count > 0 ? (int)(dev->rssi_sum / dev->hit_count) : rssi;
        doc["hit_count"] = dev->hit_count;
        if (dev->probe_intervals > 0) {
            doc["avg_probe_interval_ms"] = dev->probe_interval_sum / dev->probe_intervals;
        }
        int8_t range = dev->rssi_max - dev->rssi_min;
        doc["signal_trend"] = range < 10 ? "stable" : (range < 20 ? "moderate" : "moving");
    }

    String json_output;
    serializeJson(doc, json_output);
    Serial.println(json_output);
}

// ============================================================================
// DETECTION HELPER FUNCTIONS
// ============================================================================

// Forward declaration
static void update_tracked_device(TrackedDevice* dev, int8_t rssi, uint8_t channel, uint8_t type);

// Find tracked device by MAC hash, returns pointer or nullptr
static TrackedDevice* find_tracked(const uint8_t* mac) {
    uint32_t hash = fnv1a_mac(mac);
    uint32_t idx = hash & MAX_TRACKED_MASK;

    for (int probe = 0; probe < HASH_MAX_PROBE; probe++) {
        uint32_t slot = (idx + probe) & MAX_TRACKED_MASK;
        if (tracked_devices[slot].mac_hash == 0) return nullptr;
        if (tracked_devices[slot].mac_hash == hash) return &tracked_devices[slot];
    }
    return nullptr;
}

// Check if device was already detected and still within TTL
bool is_already_detected(const uint8_t* mac)
{
    TrackedDevice* dev = find_tracked(mac);
    if (!dev) return false;

    // TTL check: if last seen > DETECTION_TTL ago, treat as expired
    if (millis() - dev->last_seen > DETECTION_TTL) return false;

    return true;
}

// Create a new tracked device entry
void add_detected_device(const uint8_t* mac, int8_t rssi, uint8_t channel, uint8_t type)
{
    uint32_t hash = fnv1a_mac(mac);
    uint32_t idx = hash & MAX_TRACKED_MASK;
    uint32_t now = millis();

    for (int probe = 0; probe < HASH_MAX_PROBE; probe++) {
        uint32_t slot = (idx + probe) & MAX_TRACKED_MASK;
        if (tracked_devices[slot].mac_hash == 0) {
            // Empty slot — create new entry
            TrackedDevice& dev = tracked_devices[slot];
            dev.mac_hash = hash;
            memcpy(dev.mac, mac, 6);
            dev.rssi_min = rssi;
            dev.rssi_max = rssi;
            dev.rssi_last = rssi;
            dev.rssi_sum = rssi;
            dev.hit_count = 1;
            dev.last_channel = channel;
            dev.type = type;
            dev.first_seen = now;
            dev.last_seen = now;
            dev.probe_interval_sum = 0;
            dev.probe_intervals = 0;
            hash_entries++;
            if (probe > 0) hash_collisions++;
            return;
        }
        if (tracked_devices[slot].mac_hash == hash) {
            // Already exists — update it
            update_tracked_device(&tracked_devices[slot], rssi, channel, type);
            return;
        }
    }
    printf("[WARN] Tracked device table probe limit reached (%u entries)\n", hash_entries);
}

// Update existing tracked device with new detection data
static void update_tracked_device(TrackedDevice* dev, int8_t rssi, uint8_t channel, uint8_t type) {
    uint32_t now = millis();

    // RSSI trending
    dev->rssi_last = rssi;
    if (rssi < dev->rssi_min) dev->rssi_min = rssi;
    if (rssi > dev->rssi_max) dev->rssi_max = rssi;
    dev->rssi_sum += rssi;

    // Probe interval timing
    uint32_t interval = now - dev->last_seen;
    if (interval > 10 && interval < 30000) {
        dev->probe_interval_sum += interval;
        dev->probe_intervals++;
    }

    dev->hit_count++;
    dev->last_seen = now;
    dev->last_channel = channel;
    dev->type = type;
}

bool check_mac_prefix(const uint8_t* mac)
{
    char mac_str[9];  // Only need first 3 octets for prefix check
    snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x", mac[0], mac[1], mac[2]);
    
    for (int i = 0; i < sizeof(mac_prefixes)/sizeof(mac_prefixes[0]); i++) {
        if (strncasecmp(mac_str, mac_prefixes[i], 8) == 0) {
            return true;
        }
    }
    return false;
}

bool check_ssid_pattern(const char* ssid)
{
    if (!ssid) return false;
    
    for (int i = 0; i < sizeof(wifi_ssid_patterns)/sizeof(wifi_ssid_patterns[0]); i++) {
        if (strcasestr(ssid, wifi_ssid_patterns[i])) {
            return true;
        }
    }
    return false;
}

bool check_device_name_pattern(const char* name)
{
    if (!name) return false;
    
    for (int i = 0; i < sizeof(device_name_patterns)/sizeof(device_name_patterns[0]); i++) {
        if (strcasestr(name, device_name_patterns[i])) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// WIFI PROMISCUOUS MODE HANDLER
// ============================================================================

typedef struct {
    unsigned frame_ctrl:16;
    unsigned duration_id:16;
    uint8_t addr1[6]; /* receiver address */
    uint8_t addr2[6]; /* sender address */
    uint8_t addr3[6]; /* filtering address */
    unsigned sequence_ctrl:16;
    uint8_t addr4[6]; /* optional */
} wifi_ieee80211_mac_hdr_t;

typedef struct {
    wifi_ieee80211_mac_hdr_t hdr;
    uint8_t payload[0]; /* network data ended with 4 bytes csum (CRC32) */
} wifi_ieee80211_packet_t;

void wifi_sniffer_packet_handler(void* buff, wifi_promiscuous_pkt_type_t type)
{
    total_frames_seen++;

    const wifi_promiscuous_pkt_t *ppkt = (wifi_promiscuous_pkt_t *)buff;
    const wifi_ieee80211_packet_t *ipkt = (wifi_ieee80211_packet_t *)ppkt->payload;
    const wifi_ieee80211_mac_hdr_t *hdr = &ipkt->hdr;

    // Track channel activity for adaptive dwell
    uint8_t ch = ppkt->rx_ctrl.channel;
    if (ch >= 1 && ch <= 13) channel_activity[ch]++;

    // Check for probe requests (0x04), probe responses (0x05), and beacons (0x08)
    uint8_t frame_type = (hdr->frame_ctrl & 0xFF) >> 2;

    if (frame_type != 0x10 && frame_type != 0x14 && frame_type != 0x20) {
        return;  // Not probe req (0x10), probe resp (0x14), or beacon (0x20)
    }

    // Extract SSID from management frame
    char ssid[33] = {0};
    uint8_t *payload = (uint8_t *)ipkt->payload;

    if (frame_type == 0x14 || frame_type == 0x20) {
        // Probe response & beacon: skip timestamp(8) + beacon_interval(2) + capability(2) = 12 bytes
        payload += 12;
    }

    // Parse SSID element (tag 0, length, data)
    if (payload[0] == 0 && payload[1] > 0 && payload[1] <= 32) {
        memcpy(ssid, &payload[2], payload[1]);
        ssid[payload[1]] = '\0';
        total_ssids_seen++;
    }

    // Enqueue event for processing task — runs in WiFi task context (not ISR)
    if (detectionQueue) {
        DetectionEvent evt;
        memcpy(evt.mac, hdr->addr2, 6);
        strncpy(evt.ssid, ssid, 32);
        evt.ssid[32] = '\0';
        evt.rssi = ppkt->rx_ctrl.rssi;
        evt.channel = ch;
        evt.type = (frame_type == 0x10) ? 0 : ((frame_type == 0x14) ? 4 : 1);  // 0=probe_req, 1=beacon, 4=probe_resp

        if (xQueueSend(detectionQueue, &evt, 0) != pdTRUE) {
            events_dropped++;
        }
    }
}

// ============================================================================
// BLE SCANNING
// ============================================================================

class AdvertisedDeviceCallbacks: public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* advertisedDevice) {

        NimBLEAddress addr = advertisedDevice->getAddress();
        std::string addrStr = addr.toString();
        uint8_t mac[6];
        sscanf(addrStr.c_str(), "%02x:%02x:%02x:%02x:%02x:%02x",
               &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);

        int rssi = advertisedDevice->getRSSI();
        std::string name = "";
        if (advertisedDevice->haveName()) {
            name = advertisedDevice->getName();
        }

        // Quick check: does this device match any pattern?
        bool mac_match = check_mac_prefix(mac);
        bool name_match = !name.empty() && check_device_name_pattern(name.c_str());

        if (!mac_match && !name_match) {
#ifdef HAS_DISPLAY
            // Still show non-matching BLE devices on display for debug
            if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                display.showDebugBLE(String(name.c_str()), String(addrStr.c_str()), rssi);
                xSemaphoreGive(displayMutex);
            }
#endif
            return;
        }

        // Enqueue matching BLE detection for processing
        if (detectionQueue) {
            DetectionEvent evt;
            memcpy(evt.mac, mac, 6);
            strncpy(evt.ssid, name.c_str(), 32);
            evt.ssid[32] = '\0';
            evt.rssi = rssi;
            evt.channel = 0;  // No channel for BLE
            evt.type = mac_match ? 2 : 3;  // 2=ble_mac, 3=ble_name

            xQueueSend(detectionQueue, &evt, pdMS_TO_TICKS(10));
        }
    }
};

// ============================================================================
// CHANNEL HOPPING
// ============================================================================

void hop_channel()
{
    unsigned long now = millis();

    // Sticky channel: don't hop if we recently detected on this channel
    if (now < channel_sticky_until) return;

    // Adaptive dwell: check activity on current channel
    uint16_t activity = channel_activity[current_channel];
    uint32_t dwell_time = CHANNEL_DWELL_BASE;
    if (activity >= CHANNEL_HIGH_THRESHOLD) {
        dwell_time = CHANNEL_DWELL_HIGH;
    } else if (activity >= CHANNEL_ACTIVE_THRESHOLD) {
        dwell_time = CHANNEL_DWELL_ACTIVE;
    }

    // Add detection bonus for channels with past detections
    uint8_t ch = current_channel;
    if (ch >= 1 && ch <= 13 && channel_detections[ch] > 0) {
        dwell_time += CHANNEL_DETECTION_BONUS * channel_detections[ch];
        if (dwell_time > CHANNEL_MAX_DWELL) dwell_time = CHANNEL_MAX_DWELL;
    }

    if (now - last_channel_hop > dwell_time) {
        // Reset activity counter for channel we're leaving
        channel_activity[current_channel] = 0;

        current_channel++;
        if (current_channel > MAX_CHANNEL) {
            current_channel = 1;
        }
        esp_wifi_set_channel(current_channel, WIFI_SECOND_CHAN_NONE);
        last_channel_hop = now;
#ifdef HAS_DISPLAY
        if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            display.updateChannelInfo(current_channel);
            xSemaphoreGive(displayMutex);
        }
#endif
    }
}

// ============================================================================
// PROCESSING TASK (Core 0) — dequeues detection events, does pattern matching
// ============================================================================

void processingTask(void* parameter) {
    (void)parameter;
    DetectionEvent evt;

    while (true) {
        // Yield to IDLE0 to feed watchdog — critical when queue stays full
        vTaskDelay(1);

        if (xQueueReceive(detectionQueue, &evt, pdMS_TO_TICKS(100)) == pdTRUE) {
            events_processed++;

            char mac_str[18];
            snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
                     evt.mac[0], evt.mac[1], evt.mac[2],
                     evt.mac[3], evt.mac[4], evt.mac[5]);

            if (evt.type <= 1 || evt.type == 4) {
                // WiFi event (0=probe_req, 1=beacon, 4=probe_resp)

#ifdef HAS_DISPLAY
                // Show debug SSID on display
                if (strlen(evt.ssid) > 0) {
                    if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                        display.showDebugSSID(String(evt.ssid), evt.rssi, evt.channel);
                        xSemaphoreGive(displayMutex);
                    }
                }
#endif

                bool ssid_match = strlen(evt.ssid) > 0 && check_ssid_pattern(evt.ssid);
                bool mac_match = check_mac_prefix(evt.mac);

                if (ssid_match || mac_match) {
                    // Track channel detections for channel memory
                    if (evt.channel >= 1 && evt.channel <= 13) {
                        channel_detections[evt.channel]++;
                        channel_sticky_until = millis() + CHANNEL_STICKY_DURATION;
                    }

                    if (!is_already_detected(evt.mac)) {
                        const char* detection_type;
                        const char* ssid_out = strlen(evt.ssid) > 0 ? evt.ssid : "hidden";

                        if (ssid_match) {
                            detection_type = (evt.type == 0) ? "probe_request" :
                                             (evt.type == 4) ? "probe_response" : "beacon";
                        } else {
                            detection_type = (evt.type == 0) ? "probe_request_mac" :
                                             (evt.type == 4) ? "probe_response_mac" : "beacon_mac";
                        }

                        add_detected_device(evt.mac, evt.rssi, evt.channel, evt.type);
                        TrackedDevice* dev = find_tracked(evt.mac);
                        output_wifi_detection_json(ssid_out, evt.mac, evt.rssi, detection_type, dev);

                        if (!triggered) {
                            triggered = true;
                            flock_detected_beep_sequence();
                            led_flash_trigger(evt.rssi);
                        }
                    } else {
                        // Re-detection: update tracking data
                        TrackedDevice* dev = find_tracked(evt.mac);
                        if (dev) update_tracked_device(dev, evt.rssi, evt.channel, evt.type);
                    }
                    last_detection_time = millis();
                }
            } else {
                // BLE event (mac_prefix or device_name)
                const char* method = (evt.type == 2) ? "mac_prefix" : "device_name";

                if (!is_already_detected(evt.mac)) {
                    add_detected_device(evt.mac, evt.rssi, 0, evt.type);
                    TrackedDevice* dev = find_tracked(evt.mac);
                    output_ble_detection_json(mac_str, evt.ssid, evt.rssi, method, dev);

                    if (!triggered) {
                        triggered = true;
                        pending_beep = true;
                        led_flash_trigger(evt.rssi);
                    }
                } else {
                    // Re-detection: update tracking data
                    TrackedDevice* dev = find_tracked(evt.mac);
                    if (dev) update_tracked_device(dev, evt.rssi, 0, evt.type);
                }
                last_detection_time = millis();
            }
        }
    }
}

// ============================================================================
// MAIN FUNCTIONS
// ============================================================================

void setup()
{
    Serial.begin(115200);
    delay(1000);

#ifdef HAS_DISPLAY
    // Initialize display first for visual feedback
    if (!display.begin()) {
        Serial.println("Failed to initialize display!");
    }
    // Note: Don't show info here - display.begin() may have started calibration mode (CYD only)
#endif

    // Initialize buzzer
#if BUZZER_ENABLED
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
#endif

#ifndef WAVESHARE_147
    // Initialize RGB LED with PWM (Waveshare uses WS2812 via display handler)
    led_init();

    // LED boot test - cycle R, G, B to verify hardware
    printf("LED boot test: RED...\n");
    rgb_pwm(255, 0, 0);   // Red
    delay(400);
    printf("LED boot test: GREEN...\n");
    rgb_pwm(0, 255, 0);   // Green
    delay(400);
    printf("LED boot test: BLUE...\n");
    rgb_pwm(0, 0, 255);   // Blue
    delay(400);
    printf("LED boot test: ORANGE...\n");
    rgb_pwm(255, 100, 0);  // Orange
    delay(400);
    printf("LED boot test: Scanning mode (green 50%%)\n");
    rgb_pwm(0, 128, 0);   // Green 50%
    delay(400);
#endif

    boot_beep_sequence();

    printf("Starting Flock Squawk Enhanced Detection System...\n\n");

    // Create FreeRTOS queue and mutex before starting WiFi/BLE
    detectionQueue = xQueueCreate(16, sizeof(DetectionEvent));
    displayMutex = xSemaphoreCreateMutex();
    if (!detectionQueue || !displayMutex) {
        printf("[FATAL] Failed to create queue or mutex!\n");
    }

    // Start processing task on Core 0
    xTaskCreatePinnedToCore(
        processingTask,        // Task function
        "detect",              // Name
        4096,                  // Stack size
        NULL,                  // Parameter
        1,                     // Priority (above idle)
        &processingTaskHandle, // Task handle
        0                      // Core 0
    );
    printf("[INIT] Processing task started on Core 0\n");

    // Remove IDLE0 from task watchdog — Core 0 is dedicated to detection processing
    esp_task_wdt_delete(xTaskGetIdleTaskHandleForCPU(0));
    printf("[INIT] IDLE0 removed from task watchdog\n");

    // Initialize WiFi in promiscuous mode
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(&wifi_sniffer_packet_handler);
    esp_wifi_set_channel(current_channel, WIFI_SECOND_CHAN_NONE);

    printf("WiFi promiscuous mode enabled on channel %d\n", current_channel);
    printf("Monitoring probe requests and beacons...\n");

    // Initialize BLE
    printf("Initializing BLE scanner...\n");
    NimBLEDevice::init("");
    pBLEScan = NimBLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new AdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(false);  // Passive scan — lower power, still gets ads
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);

    printf("BLE scanner initialized (passive mode)\n");
    printf("System ready - hunting for Flock Safety devices...\n\n");

#ifdef HAS_DISPLAY
    // Only show system ready if not in calibration mode (CYD has touch calibration)
#ifdef CYD_DISPLAY
    if (display.getCurrentPage() != DisplayHandler::PAGE_CALIBRATE) {
#endif
        display.updateScanStatus(true);
#ifdef CYD_DISPLAY
    }
#endif
#endif

    last_channel_hop = millis();
}

void loop()
{
    // Service the RGB LED strobe (non-blocking)
    led_flash_update();

    // Play detection beep on Core 1 (deferred from processingTask on Core 0)
    if (pending_beep) {
        pending_beep = false;
        flock_detected_beep_sequence();
    }

    // Print stats every 5 seconds (includes queue diagnostics)
    static unsigned long last_stats = 0;
    if (millis() - last_stats > 5000) {
        UBaseType_t queueDepth = detectionQueue ? uxQueueMessagesWaiting(detectionQueue) : 0;
        printf("[STATS] Frames: %u, SSIDs: %u, Ch: %d | Queue: %u/16, Processed: %u, Dropped: %u | Tracked: %u/%d, Collisions: %u\n",
               total_frames_seen, total_ssids_seen, current_channel,
               (unsigned)queueDepth, events_processed, events_dropped,
               hash_entries, MAX_TRACKED, hash_collisions);
        last_stats = millis();
    }

#ifdef HAS_DISPLAY
    // Update display (mutex protects against concurrent addDetection from processing task)
    if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        display.update();
        xSemaphoreGive(displayMutex);
    }
#endif

    // Handle channel hopping for WiFi promiscuous mode
    hop_channel();

    // Handle heartbeat pulse if device is in range
    if (device_in_range) {
        unsigned long now = millis();

        // Check if 10 seconds have passed since last heartbeat
        if (now - last_heartbeat >= 10000) {
            heartbeat_pulse();
            last_heartbeat = now;
        }

        // Check if device has gone out of range (no detection for 30 seconds)
        if (now - last_detection_time >= 30000) {
            printf("Device out of range - stopping heartbeat\n");
            device_in_range = false;
            triggered = false; // Allow new detections
        }
    }

    if (millis() - last_ble_scan >= BLE_SCAN_INTERVAL && !pBLEScan->isScanning()) {
        pBLEScan->start(BLE_SCAN_DURATION, false);
        last_ble_scan = millis();
#ifdef HAS_DISPLAY
        if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            display.updateScanMode(true);
            xSemaphoreGive(displayMutex);
        }
#endif
    }

    if (pBLEScan->isScanning() == false && millis() - last_ble_scan > BLE_SCAN_DURATION * 1000) {
        pBLEScan->clearResults();
#ifdef HAS_DISPLAY
        if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            display.updateScanMode(false);
            xSemaphoreGive(displayMutex);
        }
#endif
    }

    vTaskDelay(pdMS_TO_TICKS(10));  // 10ms yield instead of 100ms delay
}
