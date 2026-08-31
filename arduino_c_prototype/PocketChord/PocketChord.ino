#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include "page.h"

const bool  USE_HOTSPOT = true;
const char* AP_SSID   = "PocketChord";
const char* AP_PASS   = "chordchord";


const int ROW_PIN[4] = {18, 19, 20, 21};   // keypad R1 R2 R3 R4 
const int COL_PIN[4] = {22, 23, 14,  6};   // keypad C1 C2 C3 C4
const int PIN_VRX = 2;                     
const int PIN_VRY = 3;                     
const int PIN_SW  = -1;                    // joystick click not wired

const int  DEADZONE = 900; 


WebServer        http(80);
WebSocketsServer ws(81);

bool     keyRaw[16]  = {false};
bool     keyState[16]= {false};
uint32_t keyTime[16] = {0};
int      lastX = 0, lastY = 0;
bool     lastSw = false;
uint32_t swTime = 0;

void say(String s) { 
  ws.broadcastTXT(s);
  Serial.println(s); 
}


void scanKeys() {
  uint32_t now = millis();
  for (int r = 0; r < 4; r++) {
    digitalWrite(ROW_PIN[r], LOW);
    delayMicroseconds(60);
    for (int c = 0; c < 4; c++) {
      int  i    = r * 4 + c;
      bool down = (digitalRead(COL_PIN[c]) == LOW);
      if (down != keyRaw[i]) { keyRaw[i] = down; keyTime[i] = now; }
      if (down != keyState[i] && now - keyTime[i] > 8) {  
        keyState[i] = down;
        say(String("B") + i + (down ? "D" : "U"));
      }
    }
    digitalWrite(ROW_PIN[r], HIGH);
  }
}

int axis(int raw, bool invert) {
  int v = raw - 2048;
  if (invert) v = -v;
  if (v >  DEADZONE) return  1;
  if (v < -DEADZONE) return -1;
  return 0;
}

void scanStick() {
  int x = axis(analogRead(PIN_VRX), INVERT_X);
  int y = axis(analogRead(PIN_VRY), INVERT_Y);
  if (x != lastX || y != lastY) {
    lastX = x; lastY = y;
    say(String("J") + x + "," + y);
  }
  if (PIN_SW < 0) return;
  bool sw = (digitalRead(PIN_SW) == LOW);
  if (sw != lastSw && millis() - swTime > 30) {
    swTime = millis();
    lastSw = sw;
    if (sw) say("P");
  }
}

void onWs(uint8_t num, WStype_t type, uint8_t* payload, size_t len) {
  if (type == WStype_CONNECTED) {
    Serial.printf("browser %u connected\n", num);
    ws.sendTXT(num, "J0,0");
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);

  for (int i = 0; i < 4; i++) {
    pinMode(ROW_PIN[i], OUTPUT);
    digitalWrite(ROW_PIN[i], HIGH);
    pinMode(COL_PIN[i], INPUT_PULLUP);
  }
  if (PIN_SW >= 0) pinMode(PIN_SW, INPUT_PULLUP);
  analogReadResolution(12);

  if (!USE_HOTSPOT && strlen(HOME_SSID)) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(HOME_SSID, HOME_PASS);
    Serial.print("joining wifi");
    while (WiFi.status() != WL_CONNECTED) { delay(400); Serial.print("."); }
    Serial.println();
    Serial.print("PocketChord is at  http://");
    Serial.println(WiFi.localIP());
  } else {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.println("hotspot up");
    Serial.print("join wifi '"); Serial.print(AP_SSID);
    Serial.print("' pass '");    Serial.print(AP_PASS);
    Serial.println("' then open  http://192.168.4.1");
  }

  http.on("/", []() { http.send_P(200, "text/html", PAGE); });
  http.onNotFound([]() { http.send_P(200, "text/html", PAGE); });
  http.begin();

  ws.begin();
  ws.onEvent(onWs);
  Serial.println("ready");
}

void loop() {
  ws.loop();
  http.handleClient();
  scanKeys();
  scanStick();
}
