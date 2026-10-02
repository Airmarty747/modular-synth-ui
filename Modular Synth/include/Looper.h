#pragma once
#include <Arduino.h>
#include <vector>
#include "IStorage.h"

// A simple structure to hold one musical event
struct LoopEvent {
    uint32_t timestamp; 
    int buttonId;
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

public:
    Looper(IStorage* storageHardware) {
        memory = storageHardware;
        isRecording = false;
        isPlaying = false;
        
        loopDuration = 0;
        playbackIndex = 0;
        lastElapsed = 0;
        
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
            Serial.printf("Looper: Recording stopped. %d notes captured. Length: %d ms\n", activeLoop.size(), loopDuration);
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
            Serial.println("Looper: Playback started.");
        } else {
            Serial.println("Looper: Playback stopped.");
        }
    }

    // --- ENGINE LOGIC ---

    void logEvent(int buttonId) {
        if (isRecording) {
            uint32_t relativeTime = millis() - loopStartTime;
            activeLoop.push_back({relativeTime, buttonId});
        }
    }

    // Called constantly in main loop. Returns a buttonId if a note needs to be triggered, or -1 if nothing is playing.
    int updatePlayback() {
        if (!isPlaying || activeLoop.empty() || loopDuration == 0) return -1;

        // Calculate where we are in the current loop sequence
        uint32_t elapsed = (millis() - playbackStartTime) % loopDuration;

        // If the elapsed time drops, the modulo wrapped around, meaning the loop restarted
        if (elapsed < lastElapsed) {
            playbackIndex = 0; 
        }
        lastElapsed = elapsed;

        // Check if it's time to play the next event
        if (playbackIndex < activeLoop.size()) {
            if (elapsed >= activeLoop[playbackIndex].timestamp) {
                int btn = activeLoop[playbackIndex].buttonId;
                playbackIndex++;
                return btn;
            }
        }
        return -1; // No note to play at this exact millisecond
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
            serializedData += String(event.timestamp) + ":" + String(event.buttonId) + ";";
        }

        memory->saveTrack(serializedData, trackId);
    }

    // --- UI GETTERS ---
    bool getIsRecording() const { return isRecording; }
    bool getIsPlaying() const { return isPlaying; }
};