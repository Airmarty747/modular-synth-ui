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

// 3. THE BOOT SEQUENCE
void setup() {
    Serial.begin(115200);
    // Serial goes out over the chip's built-in USB. If nothing is reading that
    // port, a write will otherwise sit and wait for a reader, which makes the
    // synth look dead when it is merely talking to nobody. Zero means never
    // wait: unread output is dropped and the music carries on.
    Serial.setTxTimeoutMs(0);
    delay(1000);
    Serial.println();
    Serial.println("=== PocketChord starting ===");

    // Initialize Hardware Managers
    screen.begin(); 
    input.begin();
    share.begin();

    // Boots the FreeRTOS audio task and wakes up the amplifier
    audio.begin();

    pinMode(47, OUTPUT);
    digitalWrite(47, HIGH); // Pull HIGH to wake up the amplifier
    
    // Four notes on the way up, before anything is touched. If you hear these,
    // the amplifier, the speaker and the I2S clocks are all good, and any
    // silence afterwards belongs to the pads rather than the audio path.
    Serial.println("Boot chime: playing C E G C -- you should hear four notes.");
    audio.bootChime();

    Serial.println("PocketChord Boot Sequence Complete.");
    Serial.println("Running in standalone hardware mode.");
    Serial.println("Note pads are 5-11. Pads 0-4 are controls and stay silent.");
    Serial.println("Waiting for input...");
}

// --- The Master Engine ---
void loop() {
    // STEP A: Read the physical world (pads play notes, Shift + pads drive the Looper)
    input.scanHardware(synth, audio, looper);

    // STEP B: Check for incoming shared tracks via aux cable
    share.listenForIncomingTrack(looper);
    
    // STEP C: Send any due looper events to the audio engine on their own voices
    LoopEvent evt;
    while (looper.updatePlayback(evt)) {
        uint8_t src = evt.buttonId + LOOP_VOICE_OFFSET;
        if (evt.noteOn) audio.playPad(evt.buttonId, synth, src);
        else            audio.stopNote(src, synth);
    }
    
    // STEP D: Update the UI
    screen.update(synth, looper);
}
