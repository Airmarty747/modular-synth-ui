#include <Arduino.h>
#include <Wire.h>

#include "SynthState.h"
#include "InputManager.h"
#include "Looper.h"
#include "ShareManager.h"
#include "DummyMemory.h"
#include "DisplayManager.h"
#include "AudioEngine.h"

SynthState synth;
InputManager input;
ShareManager share;
DummyMemory hardwareCache;
Looper looper(&hardwareCache);
DisplayManager screen;
AudioEngine audio;

void setup() {
    Serial.begin(115200);
    // Serial goes out over the built-in USB. With a timeout of zero, unread output is
    // dropped instead of making the synth wait for a reader that is not there.
    Serial.setTxTimeoutMs(0);
    delay(1000);
    Serial.println();
    Serial.println("=== PocketChord starting ===");

    // The one and only I2C setup. The OLED (0x3C) and the MPR121 (0x5A) share this bus,
    // so it has to exist before either of them starts.
    Wire.begin(40, 39);
    Wire.setClock(400000);

    screen.begin();
    input.begin();
    share.begin();

    // Starts the audio task and wakes the amplifier (AudioEngine owns pin 47).
    audio.begin();

    Serial.println("Boot chime: playing C E G C -- you should hear four notes.");
    audio.bootChime();

    Serial.println("PocketChord Boot Sequence Complete.");
    Serial.println("Waiting for input...");
}

void loop() {
    static uint32_t lastStatsMs = 0;
    static uint32_t maxLoopUs = 0;
    uint32_t loopStart = micros();

    // STEP A: read the pads, shift and joystick
    input.scanHardware(synth, audio, looper);

    // STEP B: incoming tracks from the aux cable (non-blocking now)
    share.listenForIncomingTrack(looper);

    // STEP C: due looper events go to the audio engine on their own voices
    // (for now loops still follow the live key/scale; the sequencer rewrite changes that)
    LoopEvent evt;
    while (looper.updatePlayback(evt)) {
        uint8_t src = evt.buttonId + LOOP_VOICE_OFFSET;
        if (evt.noteOn) audio.playPad(evt.buttonId, synth, src);
        else            audio.stopNote(src, synth);
    }

    // STEP D: screen (only touches the I2C bus when something changed)
    screen.update(synth, looper);

    // Health report every 2 s: audio CPU load and the slowest pass through loop().
    uint32_t dt = micros() - loopStart;
    if (dt > maxLoopUs) maxLoopUs = dt;
    if (millis() - lastStatsMs >= 2000) {
        lastStatsMs = millis();
        audio.printStats();
        Serial.printf("Main loop: slowest pass %u us\n", (unsigned)maxLoopUs);
        maxLoopUs = 0;
    }
}
