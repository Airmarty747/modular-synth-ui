#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MPR121.h>
#include <math.h> // For joystick trigonometry
#include "SynthState.h"
#include "Looper.h"

class InputManager {
private:
    Adafruit_MPR121 cap = Adafruit_MPR121();
    uint16_t lastTouched = 0;

    // Hardware Pins based on your config.h
    const int pinVrx = 1;
    const int pinVry = 2;
    const int pinShift = 41; 

    bool isShiftHeld = false;
    
    // Radial Joystick Variables
    const int deadzone = 900;
    int lastRadialKey = -1; // Tracks which of the 12 pie slices we are currently pointing at
    
    // Cooldown for shifted up/down actions (like BPM)
    uint32_t lastJoyTrigger = 0;
    const unsigned long joyCooldown = 250; 

    bool newActionReady = false;
    int lastPressedButton = -1;

public:
    InputManager() {}

    void begin() {
        // Configure Shift Button (Uses internal pullup, goes LOW when pressed)
        pinMode(pinShift, INPUT_PULLUP);

        // Configure Joystick Precision
        analogReadResolution(12);

        // Initialize MPR121 Capacitive Touch on I2C address 0x5A
        // Because DisplayManager already called Wire.begin(40, 39), the bus is ready!
        if (!cap.begin(0x5A)) {
            Serial.println("Error: MPR121 keypad not found on I2C bus!");
        } else {
            Serial.println("MPR121 Keypad Initialized.");
        }
    }

    void scanHardware(SynthState& synth) {
        uint32_t now = millis();

        // 1. SCAN SHIFT BUTTON (Physical Pin 41)
        bool currentShift = (digitalRead(pinShift) == LOW);
        if (currentShift != isShiftHeld) {
            isShiftHeld = currentShift;
            Serial.println(isShiftHeld ? "Shift key engaged." : "Shift key released.");
        }

        // 2. SCAN MPR121 CAPACITIVE KEYPAD (12 Electrodes)
        uint16_t currTouched = cap.touched();
        for (uint8_t i = 0; i < 12; i++) {
            // If button *is* touched now, and *wasn't* touched last frame
            if ((currTouched & _BV(i)) && !(lastTouched & _BV(i))) {
                lastPressedButton = i;
                newActionReady = true;
            }
        }
        lastTouched = currTouched;

        // 3. SCAN ANALOG JOYSTICK (Radial Math)
        int rawX = analogRead(pinVrx) - 2048;
        int rawY = analogRead(pinVry) - 2048;

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

    bool hasNewAction() { return newActionReady; }
    
    int getLastPressedButton() {
        newActionReady = false;
        return lastPressedButton;
    }

    void handleButtonPress(int buttonId, SynthState& synth, Looper& looper) {
        if (isShiftHeld) {
            switch (buttonId) {
                case 1: synth.shiftOctave(1); break;
                case 2: synth.shiftOctave(-1); break;
                case 3: looper.toggleRecording(); break;
                case 4: looper.togglePlayback(); break;
                default: Serial.println("Shift Action: Unmapped button."); break;
            }
        } else {
            Serial.printf("Action: Playing note for Pad %d\n", buttonId);
            looper.logEvent(buttonId);
        }
    }
};