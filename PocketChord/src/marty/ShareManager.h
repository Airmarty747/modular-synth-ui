#pragma once
#include <Arduino.h>
#include "Looper.h"

class ShareManager {
private:
    // We use Serial1 (the secondary hardware serial pins on the ESP32) 
    // wired to an aux/trrs jack for device-to-device communication.
    const unsigned long BAUD_RATE = 9600;

public:
    void begin() {
        // Initialize Serial1 on specific pins (e.g., TX and RX pins)
        // Serial1.begin(BAUD_RATE, SERIAL_8N1, RX_PIN, TX_PIN);
        Serial1.begin(BAUD_RATE);
        Serial.println("ShareManager: Auxiliary link initialized.");
    }

    // Send a recorded track string to another physical PocketChord over an aux cable
    void sendTrack(String serializedData) {
        Serial.println("ShareManager: Transmitting track data over aux cable...");
        
        // Send a start marker, the data, and an end marker so the receiving synth knows when to stop reading
        Serial1.print("<START>");
        Serial1.print(serializedData);
        Serial1.println("<END>");
    }

    // Continuously listen on the RX pin for incoming data from another synth
    void listenForIncomingTrack(Looper& looper) {
        if (Serial1.available()) {
            String incomingMessage = Serial1.readStringUntil('\n');
            
            // Check if it's a valid track broadcast
            if (incomingMessage.indexOf("<START>") >= 0 && incomingMessage.indexOf("<END>") >= 0) {
                Serial.println("ShareManager: Incoming track detected from another device!");
                
                // Strip the markers to get the raw event string
                int startIndex = incomingMessage.indexOf("<START>") + 7;
                int endIndex = incomingMessage.indexOf("<END>");
                String trackData = incomingMessage.substring(startIndex, endIndex);

                // Hand the received data directly into the Looper memory
                // looper.loadExternalData(trackData);
            }
        }
    }
};