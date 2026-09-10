#pragma once
#include <Arduino.h>
#include "SynthState.h"
#include "Looper.h"

class InputManager {
private:
    bool isShiftHeld;
    int lastPressedButton;
    bool newActionReady;

    // Define the GPIO pins for the 4x4 matrix and joystick
    const int rowPins[4] = {18, 19, 20, 21}; // These need to be set to the actual GPIO pins used for the rows
    const int colPins[4] = {22, 23, 14, 6}; // these need to be set to the actual GPIO pins used for the columns
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
        lastPressedButton = -1;
        newActionReady = false;
    }

    void begin() {
        // 1. Configure the 4x4 Matrix Pins
        for (int i = 0; i < 4; i++) {
            pinMode(rowPins[i], OUTPUT);
            digitalWrite(rowPins[i], HIGH); // Default rows to HIGH (inactive)
            
            pinMode(colPins[i], INPUT_PULLUP); // Columns use internal pull-up resistors
        }

        // 2. Configure Joystick Pins
        // On the ESP32-S3, analog pins are set up automatically via analogRead(), 
        // but we can set the resolution to 12-bit (0 to 4095) for high precision.
        analogReadResolution(12);

        Serial.println("InputManager: Matrix rows/cols and joystick initialized.");
    }

    void scanHardware() {
            uint32_t now = millis();

            // 1. Scan 4x4 Button Matrix
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
                        
                        // We only trigger actions on the physical PRESS (down = true)
                        if (down) {
                            lastPressedButton = i;
                            newActionReady = true;
                        } else {
                            // Handle release if it's the shift key (button 16 / index 15 or designated key)
                            handleButtonRelease(i);
                        }
                    }
                }
                digitalWrite(rowPins[r], HIGH);
            }

            // 2. Scan Analog Joystick
            int rawX = analogRead(pinVrx) - 2048;
            int rawY = analogRead(pinVry) - 2048;
            
            int x = (rawX > deadzone) ? 1 : (rawX < -deadzone) ? -1 : 0;
            int y = (rawY > deadzone) ? 1 : (rawY < -deadzone) ? -1 : 0;

            if (x != lastX || y != lastY) {
                lastX = x;
                lastY = y;
                Serial.printf("Joystick moved -> X: %d, Y: %d\n", x, y);
            }
        }


            // 2. Scan Analog Joystick
            int rawX = analogRead(pinVrx) - 2048;
            int rawY = analogRead(pinVry) - 2048;
            
            int x = (rawX > deadzone) ? 1 : (rawX < -deadzone) ? -1 : 0;
            int y = (rawY > deadzone) ? 1 : (rawY < -deadzone) ? -1 : 0;

            if (x != lastX || y != lastY) {
                lastX = x;
                lastY = y;
                Serial.printf("Joystick moved -> X: %d, Y: %d\n", x, y);
            }
        }

    bool hasNewAction() {
        return newActionReady;
    }

    int getLastPressedButton() {
        newActionReady = false; 
        return lastPressedButton;
    }

    void handleButtonPress(int buttonId, SynthState& synth, Looper& looper) {
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
            if (buttonId == 16) {
                isShiftHeld = true; 
                Serial.println("Shift key engaged.");
            } else {
                Serial.print("Action: Playing note for button ");
                Serial.println(buttonId);
                looper.logEvent(buttonId);
            }
        }
    }

    void handleButtonRelease(int buttonId) {
        if (buttonId == 16) {
            isShiftHeld = false;
            Serial.println("Shift key released.");
        }
    }
};
