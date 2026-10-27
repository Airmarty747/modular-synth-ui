#pragma once
// ShareManager.h -- v2
//
// Change from v1: the reader no longer blocks. v1 used readStringUntil('\n'), which waits
// up to a second for a newline; one stray byte on a floating RX pin froze the whole synth
// for that second. This version only takes bytes that are already waiting, keeps
// the partial line between calls, and gives up on lines that grow past MAX_LINE.
//
// Set the real aux-jack pins here (or with -DSHARE_RX_PIN=.. -DSHARE_TX_PIN=.. in platformio.ini).
// -1 means "use the core's default Serial1 pins", which may not be where you wired the jack.

#include <Arduino.h>
#include "Looper.h"

#ifndef SHARE_RX_PIN
#define SHARE_RX_PIN -1
#endif
#ifndef SHARE_TX_PIN
#define SHARE_TX_PIN -1
#endif

class ShareManager {
private:
    const unsigned long BAUD_RATE = 9600;
    static const size_t MAX_LINE = 32768;
    String line;
    bool overflow = false;

    void handleLine(const String& msg) {
        if (overflow) { Serial.println("ShareManager: line too long, dropped."); return; }
        int startIndex = msg.indexOf("<START>");
        int endIndex   = msg.indexOf("<END>");
        if (startIndex < 0 || endIndex < 0) return;
        Serial.println("ShareManager: Incoming track detected from another device!");
        String trackData = msg.substring(startIndex + 7, endIndex);
        (void)trackData;
        // looper.loadExternalData(trackData);   // enabled when the sequencer can load tracks
    }

public:
    void begin() {
        if (SHARE_RX_PIN >= 0 && SHARE_TX_PIN >= 0) Serial1.begin(BAUD_RATE, SERIAL_8N1, SHARE_RX_PIN, SHARE_TX_PIN);
        else                                          Serial1.begin(BAUD_RATE);
        Serial.println("ShareManager: Auxiliary link initialized.");
    }

    void sendTrack(String serializedData) {
        Serial.println("ShareManager: Transmitting track data over aux cable...");
        Serial1.print("<START>");
        Serial1.print(serializedData);
        Serial1.println("<END>");
    }

    // Non-blocking. Reads at most 256 waiting bytes per call so it can never hog the main loop.
    void listenForIncomingTrack(Looper& looper) {
        (void)looper;
        int budget = 256;
        while (budget-- > 0 && Serial1.available()) {
            int c = Serial1.read();
            if (c < 0) break;
            if (c == '\n') {
                handleLine(line);
                line = "";
                overflow = false;
            } else if (c != '\r') {
                if (line.length() < MAX_LINE) line += (char)c;
                else overflow = true;
            }
        }
    }
};
