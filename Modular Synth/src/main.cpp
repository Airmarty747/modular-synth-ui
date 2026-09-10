#include <Arduino.h>

// 1. IMPORT BLUEPRINTS & TESTING LAYERS
#include "SynthState.h"
#include "InputManager.h"
#include "Looper.h"
#include "ShareManager.h"
#include "DummyMemory.h"

// 2. GLOBAL INSTANTIATIONS
SynthState synth;
InputManager input;
ShareManager share;
DummyMemory hardwareCache;
Looper looper(&hardwareCache);

// 3. THE BOOT SEQUENCE
void setup() {
    Serial.begin(115200);
    delay(1000);

    // Initialize Hardware Managers
    input.begin();
    share.begin();

    Serial.println("PocketChord Standalone Boot Complete.");
}

// 4. THE ENGINE
void loop() {
    // STEP A: Read the physical world
    input.scanHardware();

    // STEP B: Process user actions
    if (input.hasNewAction()) {
        int buttonId = input.getLastPressedButton();
        input.handleButtonPress(buttonId, synth, looper);
    }

    // STEP C: Check for incoming shared tracks via aux cable
    share.listenForIncomingTrack(looper);
}
