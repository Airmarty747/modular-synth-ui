#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <math.h> // For joystick trigonometry
#include "SynthState.h"
#include "Looper.h"
#include "AudioEngine.h"

class InputManager {
public:
    // Menu navigation variables for the OLED
    int selectedMenuItem = 0;
    const int MAX_MENU_ITEMS = 2; // 0=Root, 1=Scale, 2=Articulation

private:
    // The MPR121 is driven by direct register writes rather than through
    // Adafruit_MPR121. That library's begin() reads CONFIG2 after the soft
    // reset and refuses to continue unless it reads back exactly 0x24 -- and
    // when it refuses it has already parked the chip in stop mode with every
    // electrode disabled, so touched() then returns 0 forever. The sketch that
    // works on this hardware writes the registers blindly and never asks the
    // chip to prove its identity, so that is what happens here too.
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

    // endTransmission(false) keeps the bus held for a repeated START. Sending a
    // STOP instead makes the MPR121 rewind its address pointer to 0x00, after
    // which every read quietly comes back as touch status whatever you asked
    // for -- which looks exactly like pads that never change.
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

    // Hardware Pins based on your config.h
    const int pinVrx = 1;
    const int pinVry = 2;
    // The stick module is mounted so that both axes read backwards: pushing up
    // raises the reading instead of lowering it, and right lowers it instead of
    // raising it. The radial maths below is written expecting the opposite, so
    // both axes are flipped at the point of reading. Set either to false if the
    // module is ever remounted the other way up.
    const bool invertX = true;
    const bool invertY = true;
    const int pinShift = 41;

    bool isShiftHeld = false;
    
    // Radial Joystick Variables
    const int deadzone = 900;
    int lastRadialKey = -1; // Tracks which of the 12 pie slices we are currently pointing at
    
    // Cooldown for shifted up/down actions (like BPM)
    uint32_t lastJoyTrigger = 0;
    const unsigned long joyCooldown = 250; 

    // Pads that started a live note, so their release always sends a note-off
    // (even if Shift was pressed in between) and gets recorded by the Looper
    uint16_t notePadsHeld = 0;

public:
    InputManager() {}

    void begin() {
        // Configure Shift Button (Uses internal pullup, goes LOW when pressed)
        pinMode(pinShift, INPUT_PULLUP);

        // Configure Joystick Precision
        analogReadResolution(12);

        // Initialize MPR121 Capacitive Touch on I2C address 0x5A.
        // DisplayManager already called Wire.begin(40, 39), so the bus is up;
        // 400 kHz is what the working sketch runs it at and the MPR121 and the
        // OLED are both happy there.
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

        // 1. SCAN SHIFT BUTTON (Physical Pin 41)
        bool currentShift = (digitalRead(pinShift) == LOW);
        if (currentShift != isShiftHeld) {
            isShiftHeld = currentShift;
            Serial.println(isShiftHeld ? "Shift key engaged." : "Shift key released.");
        }

      // 2. SCAN MPR121 CAPACITIVE KEYPAD (12 Electrodes)
        if (!padsOk) return;
        int rawTouch = mprRead16(R_TOUCH_STATUS);
        // A failed read is not the same as nothing being held: treating -1 as
        // "all released" would fire a note-off for every pad under your finger.
        if (rawTouch < 0) return;
        uint16_t currTouched = (uint16_t)(rawTouch & 0x0FFF);
        for (uint8_t i = 0; i < 12; i++) {
            // Note ON (Newly Touched)
            if ((currTouched & _BV(i)) && !(lastTouched & _BV(i))) {
                Serial.printf("InputManager: Pad %d PRESSED\n", i); // Confirm the press
                handleButtonPress(i, synth, looper, audio);
            }
            // Note OFF (Newly Released)
            else if (!(currTouched & _BV(i)) && (lastTouched & _BV(i))) {
                Serial.printf("InputManager: Pad %d RELEASED\n", i); // Confirm the release
                if (notePadsHeld & _BV(i)) {
                    notePadsHeld &= ~_BV(i);
                    // Tell the audio engine to trigger the release envelope for this pad
                    audio.stopNote(i, synth);
                    looper.logEvent(i, false);
                }
            }
        }
        lastTouched = currTouched;

        // 3. SCAN ANALOG JOYSTICK (Radial Math)
        int rawX = analogRead(pinVrx) - 2048;
        int rawY = analogRead(pinVry) - 2048;
        if (invertX) rawX = -rawX;
        if (invertY) rawY = -rawY;

        handleJoystick(rawX, rawY, synth, now);
    }

    void handleJoystick(int rawX, int rawY, SynthState& synth, uint32_t now) {
        // Calculate magnitude (distance from center)
        float magnitude = sqrt(pow(rawX, 2) + pow(rawY, 2));
        
        // If stick is in the center deadzone, reset the tracker and exit
        if (magnitude < deadzone) {
            lastRadialKey = -1;
            lastJoyTrigger = 0;
            return;
        }

        if (isShiftHeld) {
            // SHIFT + JOYSTICK = Standard Up/Down/Left/Right control
            if (now - lastJoyTrigger > joyCooldown) {
                if (rawY < -deadzone) synth.setBpm(synth.getBpm() + 5);      // UP
                if (rawY > deadzone)  synth.setBpm(synth.getBpm() - 5);      // DOWN
                if (rawX > deadzone)  synth.setScale("major");               // RIGHT
                if (rawX < -deadzone) synth.setScale("minor");               // LEFT
                lastJoyTrigger = now;
            }
        } else {
            // UN-SHIFTED = 12-Slice Radial Key Selection
            
            // atan2 returns radians. Convert to degrees.
            float angle = atan2(rawY, rawX) * 180.0 / PI;

            // Standard math puts 0 degrees at "Right". 
            // We add 90 degrees so 0 degrees points straight "Up".
            // (Assuming rawY goes negative when pushed UP based on your config.h inverted Y axis)
            angle += 90.0; 
            
            // Normalize negative angles to 0-360
            if (angle < 0) angle += 360.0;

            // Divide 360 degrees into 12 slices of 30 degrees each
            int slice = round(angle / 30.0);
            if (slice >= 12) slice = 0; // Wrap 360 degrees back to 0 (Up)

            // Only trigger if we entered a new 30-degree zone to prevent spamming
            if (slice != lastRadialKey) {
                synth.setKey(slice);
                lastRadialKey = slice;
            }
        }
    }

private:
    void handleButtonPress(int buttonId, SynthState& synth, Looper& looper, AudioEngine& audio) {
        if (isShiftHeld) {
            switch (buttonId) {
                case 1: synth.shiftOctave(1); break;
                case 2: synth.shiftOctave(-1); break;
                case 3: looper.toggleRecording(); break; // RECORD: Shift + Pad 3
                case 4: looper.togglePlayback(); break;  // PLAY:   Shift + Pad 4
                default: Serial.println("Shift Action: Unmapped button."); break;
            }
        } else if (PADS[buttonId].role == ROLE_CHORD) {
            Serial.printf("Action: Playing note for Pad %d\n", buttonId);
            audio.playPad(buttonId, synth);
            looper.logEvent(buttonId, true);
            notePadsHeld |= _BV(buttonId);
        }
    }
};
