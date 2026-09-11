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
    bool isRecording;
    uint32_t loopStartTime;

public:
    // Constructor: We pass the hardware memory in when creating the object
    Looper(IStorage* storageHardware) {
        memory = storageHardware;
        isRecording = false;
        
        // Pre-allocate space in the PSRAM to prevent audio stuttering during recording
        activeLoop.reserve(2000); 
    }

    void toggleRecording() {
        isRecording = !isRecording;
        if (isRecording) {
            activeLoop.clear(); // Wipe the old loop
            loopStartTime = millis();
            Serial.println("Looper: Recording started.");
        } else {
            Serial.printf("Looper: Recording stopped. %d notes captured.\n", activeLoop.size());
        }
    }

    void logEvent(int buttonId) {
        if (isRecording) {
            // Calculate how many milliseconds into the loop this note was played
            uint32_t relativeTime = millis() - loopStartTime;
            
            // Add the event to our PSRAM vector
            activeLoop.push_back({relativeTime, buttonId});
        }
    }

    // THE SAVE FUNCTION
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
        
        // 1. Serialize the vector data into a String
        String serializedData = "";
        for (const auto& event : activeLoop) {
            serializedData += String(event.timestamp) + ":" + String(event.buttonId) + ";";
        }

        // 2. Hand the string off to the hardware to do the physical writing
        memory->saveTrack(serializedData, trackId);
    }
};