#include <Arduino.h>
#include <Wire.h>

#include "SynthState.h"
#include "InputManager.h"
#include "Looper.h"
#include "ShareManager.h"
#include "DummyMemory.h"
#include "DisplayManager.h"
#include "AudioEngine.h"
#ifdef POCKETCHORD_OLED_DIAG
#include "OledDiag.h"   // one-off screen diagnostic, only with -DPOCKETCHORD_OLED_DIAG
#endif
#ifdef POCKETCHORD_TEST_CMDS
#include "TestCommands.h"   // bench-test serial commands, only when built with -DPOCKETCHORD_TEST_CMDS
#endif

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
    // The USB serial port re-appears after every reset, so a monitor that is already open
    // misses the first lines. Wait up to 4 s for it to connect so the boot messages are caught.
    for (uint32_t t0 = millis(); !Serial && millis() - t0 < 4000; ) delay(10);
    delay(500);
    Serial.println();
    Serial.println("=== PocketChord starting ===");

    // The one and only I2C setup. The OLED (0x3C) and the MPR121 (0x5A) share this bus,
    // so it has to exist before either of them starts.
    Wire.begin(40, 39);
    Wire.setClock(400000);

#ifdef POCKETCHORD_OLED_DIAG
    oleddiag::run();
#endif

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
    // The loop plays in the settings it was recorded with (key, scale, instrument...), unless the right
    // bumper is held, in which case it follows the live settings.
    const SynthState& loopState = input.loopVoiceState(synth);
    LoopEvent evt;
    while (looper.updatePlayback(evt)) {
        uint8_t src = evt.buttonId + LOOP_VOICE_OFFSET;
        if (evt.noteOn) audio.playPad(evt.buttonId, loopState, src);
        else            audio.stopNote(src, loopState);
    }

#ifdef POCKETCHORD_TEST_CMDS
    testCommandsPoll(synth, audio);
#endif

    // STEP D: screen (only touches the I2C bus when something changed)
    screen.update(synth, looper, input.getUiState());

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
