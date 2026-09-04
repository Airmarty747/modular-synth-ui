#pragma once
#include <Arduino.h>

class IStorage {
public:
    // The "= 0" forces any real hardware class to include these exact functions
    virtual void saveTrack(String data, int trackId) = 0;
    virtual String loadTrack(int trackId) = 0;
    virtual bool isConnected() = 0;
};