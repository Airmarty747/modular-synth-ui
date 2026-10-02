#pragma once
#include <Arduino.h>
#include "IStorage.h"

class DummyMemory : public IStorage {
public:
    void saveTrack(String data, int trackId) override {
        Serial.println("--- HARDWARE SIMULATION ---");
        Serial.printf("Writing to memory sector for Track %d...\n", trackId);
        Serial.println("Data saved: " + data);
        Serial.println("---------------------------");
    }

    String loadTrack(int trackId) override {
        return "100:4;500:5;1200:1;"; // Returning fake loop data
    }

    bool isConnected() override {
        return true; 
    }
};