#pragma once
// DisplayManager.h -- v2
//
// Changes from v1:
//  * Redraws only when something on screen changed (plus the REC blink), instead of
//    pushing a 1 KB frame over I2C twenty times a second. That is what was stalling loop().
//  * Bus clock forced to 400 kHz.
//  * Wire.begin() is no longer here: call it once in setup() before screen.begin()/input.begin().
//  * Shows the real scale name (Major, Bhairav ...) and the instrument name instead of "Inst: 0".

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <string.h>
#include "SynthState.h"
#include "Looper.h"

class DisplayManager {
private:
    U8G2_SSD1309_128X64_NONAME0_F_HW_I2C u8g2;   // 2.42" 128x64 I2C OLED
    unsigned long lastCheck = 0;
    const unsigned long checkInterval = 50;      // how often we look for changes (not how often we draw)

    // Everything that appears on screen, so two snapshots can be compared byte for byte.
    struct Snap {
        int32_t key, scale, octave, inst, bpm, art10, rec, play, blink;
    };
    Snap last;
    bool haveDrawn = false;

    const char* getNoteName(int keyIndex) const {
        static const char* notes[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        return notes[((keyIndex % 12) + 12) % 12];
    }

public:
    DisplayManager() : u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE) {}

    // True if something acknowledges at this 7-bit I2C address.
    static bool i2cProbe(uint8_t addr) {
        Wire.beginTransmission(addr);
        return Wire.endTransmission() == 0;
    }

    void begin() {
        // Wire.begin(SDA, SCL) must already have been called (see main.cpp).

        // Boot diagnostics: list everything on the bus, so a blank screen can be told apart from
        // a wiring fault (nothing answers), a wrong address, or a pin problem (only some parts answer).
        Serial.print("I2C scan (SDA 40, SCL 39):");
        int found = 0;
        for (uint8_t a = 1; a < 127; a++) {
            if (i2cProbe(a)) { Serial.printf(" 0x%02X", a); found++; }
        }
        Serial.println(found ? "" : " nothing answered");
        bool oled3C = i2cProbe(0x3C), oled3D = i2cProbe(0x3D);
        Serial.printf("OLED at 0x3C: %s, at 0x3D: %s, MPR121 at 0x5A: %s\n",
                      oled3C ? "yes" : "no", oled3D ? "yes" : "no", i2cProbe(0x5A) ? "yes" : "no");
        // Many 2.42" SSD1309 modules ship set to 0x3D. U8g2 assumes 0x3C and then fails silently.
        if (!oled3C && oled3D) {
            u8g2.setI2CAddress(0x3D << 1);
            Serial.println("DisplayManager: OLED found at 0x3D, using that address.");
        } else if (!oled3C) {
            Serial.println("DisplayManager: no OLED answered at 0x3C or 0x3D. Check SDA 40 / SCL 39, 3V3, GND, and the module's I2C/SPI selector.");
        }

        u8g2.begin();
        u8g2.setBusClock(400000);
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_ncenB08_tr);
        u8g2.drawStr(15, 30, "PocketChord OS");
        u8g2.drawStr(30, 50, "Booting...");
        u8g2.sendBuffer();
    }

    void update(const SynthState& synth, const Looper& looper) {
        unsigned long nowMs = millis();
        if (nowMs - lastCheck < checkInterval) return;
        lastCheck = nowMs;

        Snap s;
        memset(&s, 0, sizeof(s));
        s.key   = synth.getKeyRoot();
        s.scale = synth.getScaleIndex();
        s.octave = synth.getOctaveOffset();
        s.inst  = synth.getInstrumentIndex();
        s.bpm   = synth.getBpm();
        s.art10 = (int32_t)(synth.getArticulation() * 10.0f + 0.5f);
        s.rec   = looper.getIsRecording() ? 1 : 0;
        s.play  = looper.getIsPlaying() ? 1 : 0;
        s.blink = s.rec ? (int32_t)((nowMs / 500) % 2) : 0;

        if (haveDrawn && memcmp(&s, &last, sizeof(s)) == 0) return;   // nothing changed: leave the bus alone
        last = s;
        haveDrawn = true;

        u8g2.clearBuffer();
        char buf[32];

        // ROW 1: big key name, scale name beside it, loop status at the right
        u8g2.setFont(u8g2_font_ncenB14_tr);
        u8g2.drawStr(0, 20, getNoteName(s.key));
        u8g2.setFont(u8g2_font_ncenB08_tr);
        u8g2.drawStr(38, 20, SCALES[s.scale].name);

        if (s.rec) {
            if (s.blink == 0) u8g2.drawStr(100, 20, "REC");
        } else if (s.play) {
            u8g2.drawStr(100, 20, "PLAY");
        }

        // ROW 2: octave and instrument name
        snprintf(buf, sizeof(buf), "Oct %+d", s.octave);
        u8g2.drawStr(0, 42, buf);
        u8g2.drawStr(60, 42, instrumentName(s.inst));

        // ROW 3: tempo and articulation
        snprintf(buf, sizeof(buf), "BPM: %d", s.bpm);
        u8g2.drawStr(0, 62, buf);
        snprintf(buf, sizeof(buf), "Art: %d.%d", s.art10 / 10, s.art10 % 10);
        u8g2.drawStr(70, 62, buf);

        u8g2.sendBuffer();
    }
};
