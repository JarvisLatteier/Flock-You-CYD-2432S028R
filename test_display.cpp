// Test sketch for Hosyond ST7796U display
// Based on working Amazon review code
// Build with: pio run -e esp32_cyd_35_st7796 --target upload

#include <Arduino.h>
#include <SPI.h>
#include <Arduino_GFX_Library.h>

// Pins from review
#define TFT_DC 2
#define TFT_CS 15
#define TFT_RST 4
#define TFT_BL 27
#define TOUCH_CS 33
#define TOUCH_IRQ 36
#define BUS_SCK 14
#define BUS_MOSI 13
#define BUS_MISO 12

Arduino_DataBus *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, BUS_SCK, BUS_MOSI, BUS_MISO, VSPI);
Arduino_GFX *gfx = new Arduino_ST7796(bus, TFT_RST, 1); // rotation=1 (landscape)

void setup() {
    Serial.begin(115200);
    delay(60);
    Serial.println("\n[ST7796 Test]");

    // Backlight ON
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);

    // CS idle high
    pinMode(TFT_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);
    pinMode(TOUCH_CS, OUTPUT);
    digitalWrite(TOUCH_CS, HIGH);

    // TFT hard reset
    pinMode(TFT_RST, OUTPUT);
    digitalWrite(TFT_RST, LOW);
    delay(20);
    digitalWrite(TFT_RST, HIGH);
    delay(120);

    if (!gfx->begin()) {
        Serial.println("gfx->begin FAILED");
        while(1) { delay(1000); }
    }

    Serial.println("gfx->begin OK");
    Serial.printf("Display: %dx%d\n", gfx->width(), gfx->height());

    // Draw color bars
    gfx->fillScreen(BLACK);

    gfx->setTextColor(WHITE);
    gfx->setTextSize(3);
    gfx->setCursor(80, 100);
    gfx->print("FLOCK YOU");

    gfx->setTextSize(2);
    gfx->setTextColor(CYAN);
    gfx->setCursor(60, 140);
    gfx->print("Display Test OK!");

    // Draw color bars
    uint16_t bars[] = {RED, GREEN, BLUE, CYAN, MAGENTA, YELLOW, WHITE};
    int w = gfx->width() / 7;
    for (int i = 0; i < 7; i++) {
        gfx->fillRect(i * w, 200, w, 80, bars[i]);
    }

    gfx->setTextSize(1);
    gfx->setTextColor(WHITE);
    gfx->setCursor(10, 290);
    gfx->printf("Resolution: %dx%d", gfx->width(), gfx->height());
}

void loop() {
    delay(1000);
}
