#pragma once
#include <Arduino.h>
#include <vector>
#include "IStorage.h"

// A simple structure to hold one musical event
struct LoopEvent {
    uint32_t timestamp; 
    int buttonId;
    bool noteOn;        // true = pad pressed, false = pad released
};

class Looper {
private:
    IStorage* memory; 
    std::vector<LoopEvent> activeLoop; 
    
    // Recording state
    bool isRecording;
    uint32_t loopStartTime;
    uint32_t loopDuration;

    // Playback state
    bool isPlaying;
    uint32_t playbackStartTime;
    size_t playbackIndex;
    uint32_t lastElapsed;
    bool wrapPending;        // Loop wrapped; finish the previous pass before restarting
    uint16_t soundingMask;   // Pads whose note-on has played back without a matching note-off

    // Emits a note-off for one pad that playback left sounding. Returns false when none are left.
    bool releaseSounding(LoopEvent& out, uint32_t elapsed) {
        if (soundingMask == 0) return false;
        int pad = __builtin_ctz(soundingMask);
        soundingMask &= ~(1 << pad);
        out = {elapsed, pad, false};
        return true;
    }

    LoopEvent nextEvent() {
        LoopEvent e = activeLoop[playbackIndex++];
        if (e.noteOn) soundingMask |= (1 << e.buttonId);
        else          soundingMask &= ~(1 << e.buttonId);
        return e;
    }

public:
    Looper(IStorage* storageHardware) {
        memory = storageHardware;
        isRecording = false;
        isPlaying = false;
        
        loopDuration = 0;
        playbackIndex = 0;
        lastElapsed = 0;
        wrapPending = false;
        soundingMask = 0;
        
        // Pre-allocate space in the PSRAM to prevent audio stuttering during recording
        activeLoop.reserve(2000); 
    }

    // --- STATE TOGGLES ---

    void toggleRecording() {
        if (isPlaying) togglePlayback(); // Stop playback if we are recording a new loop

        isRecording = !isRecording;
        if (isRecording) {
            activeLoop.clear(); 
            loopStartTime = millis();
            loopDuration = 0;
            Serial.println("Looper: Recording started.");
        } else {
            // Lock in the total length of the loop when recording stops
            loopDuration = millis() - loopStartTime; 
            Serial.printf("Looper: Recording stopped. %u events captured. Length: %u ms\n", (unsigned)activeLoop.size(), (unsigned)loopDuration);
        }
    }

    void togglePlayback() {
        if (isRecording) toggleRecording(); // Automatically close the loop if we hit play while recording
        
        if (activeLoop.empty()) {
            Serial.println("Looper: Cannot play an empty track.");
            return;
        }

        isPlaying = !isPlaying;
        if (isPlaying) {
            playbackStartTime = millis();
            playbackIndex = 0;
            lastElapsed = 0;
            wrapPending = false;
            Serial.println("Looper: Playback started.");
        } else {
            Serial.println("Looper: Playback stopped.");
        }
    }

    // --- ENGINE LOGIC ---

    void logEvent(int buttonId, bool noteOn = true) {
        if (isRecording) {
            uint32_t relativeTime = millis() - loopStartTime;
            activeLoop.push_back({relativeTime, buttonId, noteOn});
        }
    }

    // Called constantly in main loop. Returns true and fills `out` when an event is due.
    // Call it in a while loop: several events can be due in the same pass.
    // When playback stops or the loop wraps, it also emits note-offs for any pads it
    // left sounding (e.g. a pad still held when recording was stopped).
    bool updatePlayback(LoopEvent& out) {
        if (!isPlaying || loopDuration == 0) {
            return releaseSounding(out, 0);
        }

        // Calculate where we are in the current loop sequence
        uint32_t elapsed = (millis() - playbackStartTime) % loopDuration;

        // If the elapsed time drops, the modulo wrapped around, meaning the loop restarted
        if (elapsed < lastElapsed) {
            wrapPending = true;
        }
        lastElapsed = elapsed;

        if (wrapPending) {
            // Flush events from the end of the previous pass we didn't reach in time
            if (playbackIndex < activeLoop.size()) {
                out = nextEvent();
                return true;
            }
            if (releaseSounding(out, elapsed)) return true;
            playbackIndex = 0;
            wrapPending = false;
        }

        // Check if it's time to play the next event
        if (playbackIndex < activeLoop.size() && elapsed >= activeLoop[playbackIndex].timestamp) {
            out = nextEvent();
            return true;
        }
        return false; // Nothing due at this exact millisecond
    }

    // --- MEMORY MANAGEMENT ---

    void saveCurrentLoop(int trackId) {
        if (!memory->isConnected()) {
            Serial.println("Looper Error: Storage hardware not connected.");
            return;
        }

        if (activeLoop.empty()) {
            Serial.println("Looper: Nothing to save.");
            return;
        }

        Serial.println("Looper: Serializing data for storage...");
        
        String serializedData = "";
        for (const auto& event : activeLoop) {
            // Format per event: "timestamp:buttonId:on;" where on is 1 (press) or 0 (release)
            serializedData += String(event.timestamp) + ":" + String(event.buttonId) + ":" + String(event.noteOn ? 1 : 0) + ";";
        }

        memory->saveTrack(serializedData, trackId);
    }

    // --- UI GETTERS ---
    bool getIsRecording() const { return isRecording; }
    bool getIsPlaying() const { return isPlaying; }
};