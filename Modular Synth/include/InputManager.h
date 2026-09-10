#pragma once
#include <Arduino.h>
#include "SynthState.h"
#include "Looper.h"

class InputManager {
public:
    // Menu navigation variables for the OLED
    int selectedMenuItem = 0;
    const int MAX_MENU_ITEMS = 2; // 0=Root, 1=Scale, 2=Articulation

private:
    bool isShiftHeld;
    
    // Define the GPIO pins for the 4x4 matrix and joystick
    const int rowPins[4] = {18, 19, 20, 21}; 
    const int colPins[4] = {22, 23, 14, 6};  
    const int pinVrx = 2;
    const int pinVry = 3;

    // Debouncing and state tracking arrays for 16 buttons
    bool keyRaw[16] = {false};
    bool keyState[16] = {false};
    uint32_t keyTime[16] = {0};
    const unsigned long debounceDelay = 8; // 8ms debounce filter

    int lastX = 0;
    int lastY = 0;
    const int deadzone = 900;

public:
    InputManager() {
        isShiftHeld = false;
    }

    void begin() {
        // 1. Configure the 4x4 Matrix Pins
        for (int i = 0; i < 4; i++) {
            pinMode(rowPins[i], OUTPUT);
            digitalWrite(rowPins[i], HIGH); // Default rows to HIGH (inactive)
            pinMode(colPins[i], INPUT_PULLUP); // Columns use internal pull-up resistors
        }

        // 2. Configure Joystick Pins
        analogReadResolution(12);
        Serial.println("InputManager: Matrix rows/cols and joystick initialized.");
    }

    void scanHardware(SynthState& synth, Looper& looper) {
        uint32_t now = millis();

        // ---------------------------------------------
        // 1. SCAN 4x4 BUTTON MATRIX
        // ---------------------------------------------
        for (int r = 0; r < 4; r++) {
            digitalWrite(rowPins[r], LOW);
            delayMicroseconds(60); // Allow signal to settle

            for (int c = 0; c < 4; c++) {
                int i = r * 4 + c;
                bool down = (digitalRead(colPins[c]) == LOW);

                // Raw state change triggers timestamp reset
                if (down != keyRaw[i]) {
                    keyRaw[i] = down;
                    keyTime[i] = now;
                }

                // Debounce validation: confirm state persists past the delay
                if (down != keyState[i] && (now - keyTime[i] > debounceDelay)) {
                    keyState[i] = down;
                    
                    if (down) {
                        handleButtonPress(i, synth, looper);
                    } else {
                        handleButtonRelease(i, synth);
                    }
                }
            }
            digitalWrite(rowPins[r], HIGH);
        }

        // ---------------------------------------------
        // 2. SCAN ANALOG JOYSTICK (OLED NAVIGATION)
        // ---------------------------------------------
        int rawX = analogRead(pinVrx) - 2048;
        int rawY = analogRead(pinVry) - 2048;
        
        int x = (rawX > deadzone) ? 1 : (rawX < -deadzone) ? -1 : 0;
        int y = (rawY > deadzone) ? 1 : (rawY < -deadzone) ? -1 : 0;

        if (x != lastX || y != lastY) {
            lastX = x;
            lastY = y;
            
            // Y-Axis: Move Menu Cursor Up/Down
            if (y == 1) {
                selectedMenuItem++;
                if (selectedMenuItem > MAX_MENU_ITEMS) selectedMenuItem = 0;
            } else if (y == -1) {
                selectedMenuItem--;
                if (selectedMenuItem < 0) selectedMenuItem = MAX_MENU_ITEMS;
            }

            // X-Axis: Adjust Parameter Left/Right
            if (x != 0) {
                switch(selectedMenuItem) {
                    case 0:
                        // Adjust Root Note
                        break;
                    case 1:
                        // Adjust Scale
                        break;
                    case 2:
                        // Adjust Articulation
                        float newArtic = synth.getArticulation() + (x * 0.1f);
                        if (newArtic > 1.0f) newArtic = 1.0f;
                        if (newArtic < 0.1f) newArtic = 0.1f;
                        synth.setArticulation(newArtic);
                        break;
                }
            }
        }
    }

private:
    void handleButtonPress(int buttonId, SynthState& synth, Looper& looper) {
        // Button 15 is the physical bottom-right key on a 4x4 matrix
        if (buttonId == 15) {
            isShiftHeld = true; 
            Serial.println("Shift key engaged.");
            return; // Exit early so we don't play a note for the shift key
        }

        if (isShiftHeld) {
            switch (buttonId) {
                case 1: 
                    synth.shiftOctave(1);
                    Serial.println("Shift Action: Octave Up");
                    break;
                case 2: 
                    synth.shiftOctave(-1);
                    Serial.println("Shift Action: Octave Down");
                    break;
                case 3:
                    looper.saveCurrentLoop(1); 
                    break;
                default:
                    Serial.println("Shift Action: Unmapped button.");
                    break;
            }
        } else {
            // Normal Note Play
            Serial.print("Action: Playing note for button ");
            Serial.println(buttonId);
            looper.logEvent(buttonId);
            
            // Trigger the audio engine!
            synth.noteOn(buttonId);
        }
    }

    void handleButtonRelease(int buttonId, SynthState& synth) {
        if (buttonId == 15) {
            isShiftHeld = false;
            Serial.println("Shift key released.");
        } else {
            // Stop the note when the key is released
            synth.noteOff(buttonId);
        }
    }
};
