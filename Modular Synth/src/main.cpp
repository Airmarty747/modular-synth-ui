#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>

// 1. IMPORT BLUEPRINTS & TESTING LAYERS
#include "page.h"
#include "SynthState.h"
#include "InputManager.h"
#include "Looper.h"
#include "ShareManager.h"
#include "DummyMemory.h"

// 2. NETWORK & GLOBAL INSTANTIATIONS
const char* AP_SSID = "PocketChord";
const char* AP_PASS = "chordchord";

WebServer http(80);
WebSocketsServer ws(81);

SynthState synth;
InputManager input;
ShareManager share;
DummyMemory hardwareCache;
Looper looper(&hardwareCache);

void onWs(uint8_t num, WStype_t type, uint8_t* payload, size_t len) {
    if (type == WStype_CONNECTED) {
        Serial.printf("Browser client #%u connected to GUI\n", num);
    }
}

// 3. THE BOOT SEQUENCE
void setup() {
    Serial.begin(115200);
    delay(1000);

    // Start Wi-Fi Access Point
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);

    // Initialize Web Server routes
    http.on("/", []() { http.send_P(200, "text/html", PAGE); });
    http.onNotFound([]() { http.send_P(200, "text/html", PAGE); });
    http.begin();

    // Initialize WebSockets
    ws.begin();
    ws.onEvent(onWs);

    // Initialize Hardware Managers
    input.begin();
    share.begin();

    Serial.println("PocketChord Boot Sequence Complete.");
    Serial.println("Connect to Wi-Fi 'PocketChord' then open http://192.168.4.1");
}

// 4. THE ENGINE
void loop() {
    // Keep web server and WebSocket connection active
    ws.loop();
    http.handleClient();

    // STEP A: Read the physical world
    input.scanHardware();

    // STEP B: Process user actions
    if (input.hasNewAction()) {
        int buttonId = input.getLastPressedButton();
        input.handleButtonPress(buttonId, synth, looper);
    }

    // STEP C: Check for incoming shared tracks via aux cable
    share.listenForIncomingTrack(looper);
}