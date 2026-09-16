#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include "SynthState.h"

class DisplayManager {
private:
    // U8g2 driver specifically for 2.42" 128x64 I2C OLEDs
    U8G2_SSD1309_128X64_NONAME0_F_HW_I2C u8g2; 
    unsigned long lastUpdate = 0;
    const unsigned long refreshRate = 50; // 50ms = 20fps

    const char* getNoteName(int keyIndex) const {
        const char* notes[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        return notes[keyIndex % 12];
    }

public:
    DisplayManager() : u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE) {}

    void begin() {
        u8g2.begin();
        u8g2.clearBuffer();
        
        // A clean, bold 8-pixel font
        u8g2.setFont(u8g2_font_ncenB08_tr); 
        
        u8g2.drawStr(15, 30, "PocketChord OS");
        u8g2.drawStr(30, 50, "Booting...");
        u8g2.sendBuffer();
    }

    void update(const SynthState& synth, const Looper& looper) {
        // Draw Looper Status in the corner
        if (looper.getIsRecording()) {
            u8g2.drawStr(100, 20, "REC");
        } else if (looper.getIsPlaying()) {
            u8g2.drawStr(100, 20, "PLAY");
        }if (millis() - lastUpdate < refreshRate) return;
        lastUpdate = millis();

        u8g2.clearBuffer();
        char buf[32];

        // ROW 1: Large Key & Scale (Y=20)
        u8g2.setFont(u8g2_font_ncenB14_tr); // Switch to a 14-pixel font for the main note
        snprintf(buf, sizeof(buf), "%s %s", 
                 getNoteName(synth.getKeyRoot()), 
                 synth.getScaleName() == "major" ? "Maj" : "Min");
        u8g2.drawStr(0, 20, buf);

        // Switch back to standard 8-pixel font for the details
        u8g2.setFont(u8g2_font_ncenB08_tr); 

        // ROW 2: Octave & Instrument (Y=42)
        snprintf(buf, sizeof(buf), "Octave: %+d", synth.getOctaveOffset());
        u8g2.drawStr(0, 42, buf);
        
        snprintf(buf, sizeof(buf), "Inst: %d", synth.getInstrumentIndex());
        u8g2.drawStr(70, 42, buf); // Shifted right for a 2-column layout

        // ROW 3: BPM & Articulation (Y=62)
        snprintf(buf, sizeof(buf), "BPM: %d", synth.getBpm());
        u8g2.drawStr(0, 62, buf);

        snprintf(buf, sizeof(buf), "Art: %.1f", synth.getArticulation());
        u8g2.drawStr(70, 62, buf);

        // Push the buffer to the physical screen
        u8g2.sendBuffer();
    }
};