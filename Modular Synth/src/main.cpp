#include <Arduino.h>

// 1. IMPORT BLUEPRINTS & TESTING LAYERS
#include "SynthState.h"
#include "InputManager.h"
#include "Looper.h"
#include "ShareManager.h"
#include "DummyMemory.h"
#include "DisplayManager.h" // <-- 1. Include the blueprint

// 2. GLOBAL INSTANTIATIONS
SynthState synth;
InputManager input;
ShareManager share;
DummyMemory hardwareCache;
Looper looper(&hardwareCache);
DisplayManager screen; // <-- 2. Declare the screen object here

// 3. THE BOOT SEQUENCE
void setup() {
    Serial.begin(115200);
    delay(1000);

    // Initialize Hardware Managers
    screen.begin(); 
    input.begin();
    share.begin();

    Serial.println("PocketChord Boot Sequence Complete.");
    Serial.println("Running in standalone hardware mode.");
}

// 4. THE ENGINE
void loop() {
    // STEP A: Read the physical world
    input.scanHardware(synth); // Pass the synth state to allow joystick adjustments

    // STEP B: Process user actions
    if (input.hasNewAction()) {
        int buttonId = input.getLastPressedButton();
        input.handleButtonPress(buttonId, synth, looper);
    }

    // STEP C: Check for incoming shared tracks via aux cable
    share.listenForIncomingTrack(looper);
    
    // Check if the looper is playing back and has a note for us
    int looperNote = looper.updatePlayback();
    if (looperNote != -1) {
        Serial.printf("Looper: Playing back button %d\n", looperNote);
        // TODO: Send looperNote to the audio engine later
    }
    
    // STEP D: Update the UI
    screen.update(synth,looper); // <-- Pass the looper to the screen update function
}