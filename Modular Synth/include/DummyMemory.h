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
        return "100:8:1;400:8:0;500:9:1;900:9:0;1200:10:1;1600:10:0;"; // Returning fake loop data
    }

    bool isConnected() override {
        return true; 
    }
};