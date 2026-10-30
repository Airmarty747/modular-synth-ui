#pragma once
// OledDiag.h -- finds out why an OLED that answers on I2C shows nothing.
// Compiled in ONLY with  -DPOCKETCHORD_OLED_DIAG  in build_flags. Remove the flag afterwards.
//
// Part 1 sends raw commands that make the panel light EVERY pixel, whatever the driver chip is.
//   * Panel lights up   -> power, wiring and reset are fine; the fault is the driver variant (Part 2 finds it).
//   * Panel stays black -> hardware: power voltage, the module's reset pin, or its I2C jumpers.
// Part 2 tries five U8g2 driver variants one after another, 4 s each, with its name drawn on the screen.
//   Note which variant number draws readable text, and report it.

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

namespace oleddiag {

static const uint8_t ADDR = 0x3C;

static bool cmd(const uint8_t* bytes, size_t n) {
    Wire.beginTransmission(ADDR);
    Wire.write((uint8_t)0x00);                        // control byte: everything that follows is a command
    for (size_t i = 0; i < n; i++) Wire.write(bytes[i]);
    return Wire.endTransmission() == 0;
}
static void c1(uint8_t a)             { cmd(&a, 1); }
static void c2(uint8_t a, uint8_t b)  { uint8_t t[2] = {a, b}; cmd(t, 2); }

static void rawInit(bool chargePump) {
    c2(0xFD, 0x12);            // SSD1309: unlock the command set (harmless on others)
    c1(0xAE);                  // display off
    c2(0xD5, 0x80);            // clock
    c2(0xA8, 0x3F);            // 64 rows
    c2(0xD3, 0x00);            // no vertical offset
    c1(0x40);                  // start line 0
    c1(0xA1);                  // segment remap
    c1(0xC8);                  // scan direction
    c2(0xDA, 0x12);            // COM pin config
    c2(0x81, 0xFF);            // full contrast
    c2(0xD9, 0xF1);            // pre-charge
    c2(0xDB, 0x40);            // VCOMH
    if (chargePump) c2(0x8D, 0x14);   // SSD1306-style built-in charge pump (SSD1309 modules ignore it)
    c1(0xA4);                  // follow RAM
    c1(0xA6);                  // normal (not inverted)
    c1(0xAF);                  // display on
}

static void rawAllOn(const char* label, bool chargePump) {
    Serial.printf("DIAG %s: sending raw commands, the whole panel should light up for 4 s...\n", label);
    rawInit(chargePump);
    c1(0xA5);                  // ALL PIXELS ON, ignoring RAM
    delay(4000);
    c1(0xA4);
    c1(0xAE);
    delay(500);
}

template <class D>
static void tryVariant(int n, const char* name) {
    D d(U8G2_R0, U8X8_PIN_NONE);
    Serial.printf("DIAG variant %d: %s -- look at the screen for 4 s\n", n, name);
    d.begin();
    d.clearBuffer();
    d.drawFrame(0, 0, 128, 64);
    d.setFont(u8g2_font_ncenB08_tr);
    char b[24];
    snprintf(b, sizeof(b), "Variant %d", n);
    d.drawStr(8, 20, b);
    d.drawStr(8, 38, name);
    d.drawStr(8, 56, "ABC abc 123");
    d.sendBuffer();
    delay(4000);
}

static void run() {
    Serial.println("DIAG ===== OLED diagnostic starts (about 35 s) =====");
    rawAllOn("PART 1a (no charge pump)", false);
    rawAllOn("PART 1b (with charge pump)", true);
    Serial.println("DIAG PART 2: five driver variants, 4 s each. Note which numbers show readable text.");
    tryVariant<U8G2_SSD1309_128X64_NONAME0_F_HW_I2C>(1, "SSD1309 NONAME0 (current)");
    tryVariant<U8G2_SSD1309_128X64_NONAME2_F_HW_I2C>(2, "SSD1309 NONAME2");
    tryVariant<U8G2_SSD1306_128X64_NONAME_F_HW_I2C>(3, "SSD1306");
    tryVariant<U8G2_SH1106_128X64_NONAME_F_HW_I2C>(4, "SH1106");
    tryVariant<U8G2_SSD1305_128X64_ADAFRUIT_F_HW_I2C>(5, "SSD1305");
    Serial.println("DIAG ===== done. Report: did Part 1a / 1b light the panel? Which variant numbers drew text? =====");
}

}  // namespace oleddiag
