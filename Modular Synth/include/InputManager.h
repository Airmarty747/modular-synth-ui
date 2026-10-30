#pragma once
// InputManager.h -- v2 control layout. See the "PocketChord - Button Mapping" doc for the full table.
//
//   Pads, pressed alone:   0 Western/Indian   1 key +1   2 next instrument   3 next scale   4 note style
//                          5-11 chords
//   Left bumper (Shift) + pad:  0 strum   1 octave up   2 octave down   3 record   4 play/stop   5 articulation
//   Joystick:              12-way key select; with Shift: Up/Down BPM +/-5, Left/Right previous/next scale
//   Right bumper (GPIO 4): hold = the loop follows the live key / scale / instrument (re-harmonize)
//
// Loops stay fixed: when recording starts, the synth state is copied, and loop playback uses that copy
// (its key, scale, tradition, octave, instrument, strum) unless the right bumper is held. That is also what
// lets a loop in one tradition play while you play live in the other.
//
// The MPR121 is driven by direct register writes (the Adafruit library's begin() gives up if one register
// does not read back exactly, and leaves the chip stopped). Everything about the chip is unchanged from the
// version that was working on the hardware.

#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include "SynthState.h"
#include "Looper.h"
#include "AudioEngine.h"
#include "UiState.h"

class InputManager {
public:
    // kept from the previous version so nothing that referenced them breaks
    int selectedMenuItem = 0;
    const int MAX_MENU_ITEMS = 2;

private:
    static const uint8_t MPR_ADDR       = 0x5A;
    static const uint8_t R_TOUCH_STATUS = 0x00;
    static const uint8_t R_DEBOUNCE     = 0x5B;
    static const uint8_t R_CONFIG1      = 0x5C;
    static const uint8_t R_CONFIG2      = 0x5D;
    static const uint8_t R_ECR          = 0x5E;
    static const uint8_t R_SOFTRESET    = 0x80;

    bool     padsOk = false;
    uint16_t lastTouched = 0;

    bool mprWrite(uint8_t reg, uint8_t val) {
        Wire.beginTransmission(MPR_ADDR);
        Wire.write(reg);
        Wire.write(val);
        return Wire.endTransmission() == 0;
    }

    // endTransmission(false) keeps the bus held for a repeated START; a STOP makes the MPR121 rewind its
    // address pointer to 0x00 and every read then comes back as touch status.
    int mprRead16(uint8_t reg) {
        Wire.beginTransmission(MPR_ADDR);
        Wire.write(reg);
        if (Wire.endTransmission(false) != 0) return -1;
        if (Wire.requestFrom((int)MPR_ADDR, 2) != 2) return -1;
        uint8_t lo = Wire.read();
        uint8_t hi = Wire.read();
        return lo | (hi << 8);
    }

    bool mprInit() {
        if (!mprWrite(R_SOFTRESET, 0x63)) return false;   // the one write that must land
        delay(10);
        mprWrite(R_ECR, 0x00);                            // stop mode, required to configure

        mprWrite(0x2B, 0x01); mprWrite(0x2C, 0x01);       // baseline filter, rising
        mprWrite(0x2D, 0x0E); mprWrite(0x2E, 0x00);
        mprWrite(0x2F, 0x01); mprWrite(0x30, 0x05);       // falling
        mprWrite(0x31, 0x01); mprWrite(0x32, 0x00);
        mprWrite(0x33, 0x00); mprWrite(0x34, 0x00); mprWrite(0x35, 0x00);  // touched

        for (uint8_t e = 0; e < 12; e++) {
            mprWrite(0x41 + e * 2, 12);                   // touch threshold
            mprWrite(0x42 + e * 2, 6);                    // release threshold
        }

        mprWrite(R_DEBOUNCE, 0x00);
        mprWrite(R_CONFIG1,  0x10);                       // 16 uA charge current
        mprWrite(R_CONFIG2,  0x20);                       // 0.5 us charge, 4 samples, 1 ms
        mprWrite(R_ECR,      0x8F);                       // baseline tracking on + 12 electrodes
        delay(50);                                        // let it self-calibrate
        return true;
    }

    // --- pins ---
    const int pinVrx    = 1;
    const int pinVry    = 2;
    const int pinShift  = 41;   // left bumper
    const int pinReharm = 4;    // right bumper
    // The stick module reads both axes backwards (up raises the reading, right lowers it), so both are
    // flipped where they are read. Set either to false if the module is remounted the other way up.
    const bool invertX = true;
    const bool invertY = true;

    // --- debounced buttons (active low, internal pull-ups) ---
    struct Debounced { bool stable = false; bool raw = false; uint32_t since = 0; };
    static bool debounce(Debounced& d, bool rawNow, uint32_t now, uint32_t ms = 25) {
        if (rawNow != d.raw) { d.raw = rawNow; d.since = now; }
        if (d.raw != d.stable && now - d.since >= ms) d.stable = d.raw;
        return d.stable;
    }
    Debounced dbShift, dbReharm;

    bool isShiftHeld = false;
    bool reharmonize = false;

    // --- loop snapshot (loops stay fixed) ---
    SynthState loopSnap;
    bool haveSnap = false;

    // --- joystick ---
    const int deadzone = 900;
    int lastRadialKey = -1;            // which of the 12 slices we are in
    uint32_t lastJoyTrigger = 0;       // cooldown for shifted actions
    const unsigned long joyCooldown = 250;

    // Pads that started a live note, so their release always sends a note-off (even if Shift was pressed
    // in between) and gets recorded by the Looper.
    uint16_t notePadsHeld = 0;

public:
    InputManager() {}

    void begin() {
        pinMode(pinShift, INPUT_PULLUP);
        pinMode(pinReharm, INPUT_PULLUP);
        analogReadResolution(12);

        // Wire.begin(40, 39) has already been called in setup(). 400 kHz suits both the MPR121 and the OLED.
        Wire.setClock(400000);

        padsOk = mprInit();
        if (padsOk) {
            Serial.println("MPR121 Keypad Initialized (thresholds 12 / 6, 12 electrodes).");
            int probe = mprRead16(R_TOUCH_STATUS);
            Serial.print("MPR121 touch status reads back 0x");
            Serial.print(probe < 0 ? 0xFFFF : probe & 0x0FFF, HEX);
            Serial.println("  (0 = nothing held right now).");
        } else {
            Serial.println("Error: MPR121 did not acknowledge on I2C -- check SDA 40 / SCL 39 and 3V3.");
        }
    }

    void scanHardware(SynthState& synth, AudioEngine& audio, Looper& looper) {
        uint32_t now = millis();

        // 1. LEFT BUMPER (Shift) and RIGHT BUMPER (re-harmonize), both debounced
        bool shiftNow = debounce(dbShift, digitalRead(pinShift) == LOW, now);
        if (shiftNow != isShiftHeld) {
            isShiftHeld = shiftNow;
            Serial.println(isShiftHeld ? "Shift key engaged." : "Shift key released.");
        }
        bool rhNow = debounce(dbReharm, digitalRead(pinReharm) == LOW, now);
        if (rhNow != reharmonize) {
            reharmonize = rhNow;
            Serial.println(reharmonize ? "Re-harmonize ON: the loop follows the live key and scale." : "Re-harmonize OFF: the loop is back in its own key and scale.");
        }

        // 2. MPR121 PADS
        if (!padsOk) return;
        int rawTouch = mprRead16(R_TOUCH_STATUS);
        // A failed read is not "everything released": treating -1 that way would fire a note-off for every held pad.
        if (rawTouch < 0) return;
        uint16_t currTouched = (uint16_t)(rawTouch & 0x0FFF);
        for (uint8_t i = 0; i < 12; i++) {
            if ((currTouched & _BV(i)) && !(lastTouched & _BV(i))) {
                Serial.printf("InputManager: Pad %d PRESSED\n", i);
                handleButtonPress(i, synth, looper, audio);
            }
            else if (!(currTouched & _BV(i)) && (lastTouched & _BV(i))) {
                Serial.printf("InputManager: Pad %d RELEASED\n", i);
                if (notePadsHeld & _BV(i)) {
                    notePadsHeld &= ~_BV(i);
                    audio.stopNote(i, synth);
                    looper.logEvent(i, false);
                }
            }
        }
        lastTouched = currTouched;

        // 3. JOYSTICK
        int rawX = analogRead(pinVrx) - 2048;
        int rawY = analogRead(pinVry) - 2048;
        if (invertX) rawX = -rawX;
        if (invertY) rawY = -rawY;
        handleJoystick(rawX, rawY, synth, now);
    }

    void handleJoystick(int rawX, int rawY, SynthState& synth, uint32_t now) {
        float magnitude = sqrt((float)rawX * rawX + (float)rawY * rawY);

        // In the centre dead zone: forget the slice so the next push registers, and re-arm the cooldown.
        if (magnitude < deadzone) {
            lastRadialKey = -1;
            lastJoyTrigger = 0;
            return;
        }

        if (isShiftHeld) {
            // SHIFT + stick: Up/Down = BPM +/-5, Left/Right = previous/next scale. Only the stronger axis counts,
            // so a diagonal push does one thing instead of two.
            if (now - lastJoyTrigger > joyCooldown) {
                int ay = rawY < 0 ? -rawY : rawY, ax = rawX < 0 ? -rawX : rawX;
                if (ay >= ax) {
                    if (rawY < 0) synth.setBpm(synth.getBpm() + 5);        // UP
                    else          synth.setBpm(synth.getBpm() - 5);        // DOWN
                } else {
                    synth.cycleScale(rawX > 0 ? +1 : -1);                  // RIGHT = next, LEFT = previous
                }
                lastJoyTrigger = now;
            }
        } else {
            // 12-slice key selection: atan2 gives degrees with 0 at "right"; +90 puts 0 at "up".
            float angle = atan2((float)rawY, (float)rawX) * 180.0f / PI;
            angle += 90.0f;
            if (angle < 0) angle += 360.0f;
            int slice = (int)roundf(angle / 30.0f);
            if (slice >= 12) slice = 0;
            if (slice != lastRadialKey) {
                synth.setKey(slice);
                lastRadialKey = slice;
            }
        }
    }

    // What the display needs.
    UiState getUiState() const {
        UiState u;
        u.shift       = isShiftHeld;
        u.reharmonize = reharmonize;
        u.loopHas     = haveSnap;
        u.loopKey     = loopSnap.getKeyRoot();
        u.loopScale   = loopSnap.getScaleIndex();
        return u;
    }

    // The settings the looper's notes should be played with: the snapshot taken when the loop was
    // recorded, or the live settings while the right bumper is held (or if there is no snapshot).
    const SynthState& loopVoiceState(const SynthState& live) const {
        return (reharmonize || !haveSnap) ? live : loopSnap;
    }

private:
    void handleButtonPress(int pad, SynthState& synth, Looper& looper, AudioEngine& audio) {
        if (isShiftHeld) {
            switch (pad) {
                case 0: synth.cycleStrum(); break;                          // Shift + 0: strum speed
                case 1: synth.shiftOctave(1); break;                        // Shift + 1: octave up
                case 2: synth.shiftOctave(-1); break;                       // Shift + 2: octave down
                case 3:                                                     // Shift + 3: record
                    looper.toggleRecording();
                    if (looper.getIsRecording()) {                          // a new take: remember how it was played
                        loopSnap = synth;
                        haveSnap = true;
                        Serial.println("Loop snapshot taken (key, scale, octave, instrument, strum).");
                    }
                    break;
                case 4: looper.togglePlayback(); break;                     // Shift + 4: play / stop
                case 5: synth.cycleArticulation(+1); break;                 // Shift + 5: articulation
                default: Serial.println("Shift Action: Unmapped button."); break;
            }
            return;
        }

        if (PADS[pad].role == ROLE_CHORD) {
            Serial.printf("Action: Playing note for Pad %d\n", pad);
            audio.playPad(pad, synth);
            looper.logEvent(pad, true);
            notePadsHeld |= _BV(pad);
            return;
        }

        switch (pad) {                                                      // control pads, pressed alone
            case 0: synth.toggleTradition(); break;                         // Western <-> Indian
            case 1: synth.setKey(synth.getKeyRoot() + 1); break;            // key up one semitone
            case 2: synth.cycleInstrument(+1); break;                       // next instrument
            case 3: synth.cycleScale(+1); break;                            // next scale in this tradition
            case 4: synth.cycleNoteStyle(); break;                          // Gated / Fixed / Latch
            default: break;
        }
    }
};
