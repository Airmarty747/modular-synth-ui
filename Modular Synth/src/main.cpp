#include <Arduino.h>

// 1. IMPORT BLUEPRINTS & TESTING LAYERS
#include "SynthState.h"
#include "InputManager.h"
#include "Looper.h"
#include "ShareManager.h"
#include "DummyMemory.h"
#include "DisplayManager.h" // <-- 1. Include the blueprint for the OLED Screen
#include "AudioEngine.h"  // <-- 1. Include the audio engine

// 2. GLOBAL INSTANTIATIONS
SynthState synth;
InputManager input;
ShareManager share;
DummyMemory hardwareCache;
Looper looper(&hardwareCache);
DisplayManager screen; // <-- 2. Declare the screen object here
AudioEngine audio;

    // 1. Draw Header
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("--- POCKETCHORD ---");
    display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

    // 2. Draw Menu Item 0: Key Root
    // If selected, invert colors (Black text on White background)
    if (input.selectedMenuItem == 0) display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    else display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
    display.setCursor(0, 15);
    display.printf("Root Key: %d  ", synth.getKeyRoot());

    // Initialize Hardware Managers
    screen.begin(); 
    input.begin();

    // Boots the FreeRTOS audio task and wakes up the amplifier
    audio.begin();

    pinMode(47, OUTPUT);
    digitalWrite(47, HIGH); // Pull HIGH to wake up the amplifier
    
    Serial.println("PocketChord Boot Sequence Complete.");
    Serial.println("Running in standalone hardware mode.");
}

// --- The Master Engine ---
void loop() {
    // STEP A: Read the physical world
    input.scanHardware(synth, audio); // Pass the synth state to allow joystick adjustments

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
