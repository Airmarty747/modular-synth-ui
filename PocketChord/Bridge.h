#pragma once
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include "Config.h"
#include "src/marty/page.h"

static WebServer        http(80);
static WebSocketsServer ws(81);
static bool             bridgeUp = false;

static void onWsEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t len) {
  if (type == WStype_CONNECTED) {
    ws.sendTXT(num, "J0,0");
    Serial.printf("GUI client #%u connected\n", num);
  } else if (type == WStype_DISCONNECTED) {
    Serial.printf("GUI client #%u disconnected\n", num);
  }
}

static void bridgeBegin() {
  WiFi.mode(WIFI_AP);
  bool ok = WiFi.softAP(AP_SSID, AP_PASS);

  http.on("/", []() { http.send_P(200, "text/html", PAGE); });
  http.onNotFound([]() { http.send_P(200, "text/html", PAGE); });
  http.begin();

  ws.begin();
  ws.onEvent(onWsEvent);

  bridgeUp = ok;
  Serial.printf("AP '%s' %s -- connect, then open http://%s\n",
                AP_SSID, ok ? "up" : "FAILED",
                WiFi.softAPIP().toString().c_str());
}

static void bridgeService() {
  ws.loop();
  http.handleClient();
}

static void bridgeSay(String s) {
  if (bridgeUp) ws.broadcastTXT(s);
  Serial.println(s);
}

static void bridgeButton(uint8_t wsId, bool down) {
  if (!wsId) return;                      
  bridgeSay(String("B") + (int)wsId + (down ? "D" : "U"));
}

static void bridgeStick(int8_t x, int8_t y) {
  bridgeSay(String("J") + (int)x + "," + (int)y);
}

static void bridgePower() { bridgeSay("P"); }
