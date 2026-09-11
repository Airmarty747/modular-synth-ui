
#include <Wire.h>
#include <math.h>

#include "Config.h"
#include "PadMap.h"
#include "Theory.h"
#include "Audio.h"
#include "State.h"
#include "Ui.h"
#include "Bridge.h"

#define R_TOUCH_STATUS 0x00
#define R_TOUCH_TH(e)  (0x41 + (e) * 2)
#define R_REL_TH(e)    (0x42 + (e) * 2)
#define R_DEBOUNCE     0x5B
#define R_CONFIG1      0x5C
#define R_CONFIG2      0x5D
#define R_ECR          0x5E
#define R_SOFTRESET    0x80

static bool mprWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPR_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}


static int mprRead16(uint8_t reg) {
  Wire.beginTransmission(MPR_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return -1;
  if (Wire.requestFrom((int)MPR_ADDR, 2) != 2) return -1;
  uint8_t lo = Wire.read();
  uint8_t hi = Wire.read();
  return lo | (hi << 8);
}

static bool mprInit() {
  if (!mprWrite(R_SOFTRESET, 0x63)) return false;
  delay(10);
  mprWrite(R_ECR, 0x00);                      

  mprWrite(0x2B, 0x01); mprWrite(0x2C, 0x01);   
  mprWrite(0x2D, 0x0E); mprWrite(0x2E, 0x00);
  mprWrite(0x2F, 0x01); mprWrite(0x30, 0x05);   
  mprWrite(0x31, 0x01); mprWrite(0x32, 0x00);
  mprWrite(0x33, 0x00); mprWrite(0x34, 0x00); mprWrite(0x35, 0x00); 

  for (uint8_t e = 0; e < 12; e++) {
    mprWrite(R_TOUCH_TH(e), TOUCH_TH);
    mprWrite(R_REL_TH(e),   RELEASE_TH);
  }

  mprWrite(R_DEBOUNCE, 0x00);
  mprWrite(R_CONFIG1,  0x10);                 
  mprWrite(R_CONFIG2,  0x20);                 
  mprWrite(R_ECR,      0x8F);                  
  delay(50);                                    
  return true;
}


static void ampService() {
  if (!speakerWanted()) return;
  if (gVoicesActive) {
    ampOffAt = millis() + AMP_TAIL_MS;
    digitalWrite(PIN_AMP_SD, HIGH);
  } else if (ampOffAt && (int32_t)(millis() - ampOffAt) >= 0) {
    digitalWrite(PIN_AMP_SD, LOW);
    ampOffAt = 0;
  }
}

static void applyOutput() {
  if (!speakerWanted()) {                      
    digitalWrite(PIN_AMP_SD, LOW);
    ampOffAt = 0;
  }
  if (PIN_DAC_XSMT >= 0) digitalWrite(PIN_DAC_XSMT, phonesWanted() ? HIGH : LOW);
}

static void cycleOutput() {
  outMode = (OutMode)((outMode + 1) % 3);
  applyOutput();
  Serial.printf("output -> %s\n", OUT_NAME[outMode]);
  uiBlip(84);                                 
  uiDraw();
}

static void stepVolume() {
  vol     = (vol + 1) % VOL_STEPS;           
  gVolume = VOL_LEVELS[vol];
  Serial.printf("volume -> %u/%u (%.2f)\n", vol + 1, VOL_STEPS, VOL_LEVELS[vol]);
  uiBlip(72);                                
  uiDraw();
}

static bool btnPoll(Button& b) {
  bool raw = (digitalRead(b.pin) == b.active);

#if BTN_DEBUG
  if (!b.dbgSeen || raw != b.dbgRaw) {
    b.dbgSeen = true;
    b.dbgRaw  = raw;
    b.dbgFlips++;
  }
#endif

  if (raw) { if (b.integ < BTN_INTEGRATOR) b.integ++; }
  else     { if (b.integ > 0)              b.integ--; }

  if (!b.down && b.integ >= BTN_INTEGRATOR) {
    b.down = true;
#if BTN_DEBUG
    Serial.printf("btn %d press   (%u raw edges)\n", b.pin, b.dbgFlips);
    b.dbgFlips = 0;
#endif
    return true;                              
  }
  if (b.down && b.integ == 0) {
    b.down = false;
#if BTN_DEBUG
    Serial.printf("btn %d release (%u raw edges)\n", b.pin, b.dbgFlips);
    b.dbgFlips = 0;
#endif
    return BTN_LATCHING;                      
  }
  return false;
}

static uint8_t btnDetect(int pin) {
  if (BTN_ACTIVE_LEVEL == 0) { pinMode(pin, INPUT_PULLUP);  return LOW;  }
  if (BTN_ACTIVE_LEVEL == 1) { pinMode(pin, INPUT_PULLDOWN); return HIGH; }
  
  pinMode(pin, INPUT);
  delay(2);
  bool high = (digitalRead(pin) == HIGH);
  pinMode(pin, high ? INPUT_PULLUP : INPUT_PULLDOWN);
  return high ? LOW : HIGH;
}

static void btnInit() {
  btnVol.active = btnDetect(PIN_BTN_VOL);
  btnOut.active = btnDetect(PIN_BTN_OUT);

  
  if (digitalRead(PIN_BTN_VOL) == btnVol.active) { btnVol.integ = BTN_INTEGRATOR; btnVol.down = true; }
  if (digitalRead(PIN_BTN_OUT) == btnOut.active) { btnOut.integ = BTN_INTEGRATOR; btnOut.down = true; }

  Serial.printf("Buttons: %s. VOL(GPIO %d) now %s, OUT(GPIO %d) now %s\n",
                BTN_LATCHING ? "latching" : "momentary",
                PIN_BTN_VOL, btnVol.down ? "closed" : "open",
                PIN_BTN_OUT, btnOut.down ? "closed" : "open");
}

static void btnService() {
 
  static uint32_t nextSample = 0;
  uint32_t now = millis();
  if ((int32_t)(now - nextSample) < 0) return;
  nextSample = now + BTN_SAMPLE_MS;

  if (btnPoll(btnVol)) stepVolume();
  if (btnPoll(btnOut)) cycleOutput();
}

static void menuTap(MenuId m) {
  switch (m) {
    case MENU_KEY:   synth.setKey(synth.getKeyRoot() + 1); break;
    case MENU_SOUND: synth.setInstrument((synth.getInstrumentIndex() + 1) % INSTRUMENT_COUNT); break;

    case MENU_MODE:  synth.setBpm(synth.getBpm() + 5); break;
    default: break;
  }
}

static void menuStick(MenuId m, int8_t x, int8_t y) {
  menuUsed = true;
  switch (m) {
    case MENU_KEY:
      if (x > 0) synth.setKey(synth.getKeyRoot() + 1);
      if (x < 0) synth.setKey(synth.getKeyRoot() + 11);
      if (y < 0) synth.setScale("major");       
      if (y > 0) synth.setScale("minor");
      break;
    case MENU_SOUND:
      if (x > 0) synth.setInstrument((synth.getInstrumentIndex() + 1) % INSTRUMENT_COUNT);
      if (x < 0) synth.setInstrument((synth.getInstrumentIndex() + INSTRUMENT_COUNT - 1) % INSTRUMENT_COUNT);
      if (y < 0) synth.shiftOctave(1);
      if (y > 0) synth.shiftOctave(-1);
      break;
    case MENU_MODE:
      if (x > 0) synth.setBpm(synth.getBpm() + 5);
      if (x < 0) synth.setBpm(synth.getBpm() - 5);
      break;
    default: break;
  }
}


static void menuChord(MenuId m, uint8_t deg) {
  menuUsed = true;
  const int8_t* S = (synth.getScaleName() == "minor") ? SCALE_MINOR : SCALE_MAJOR;
  switch (m) {
    case MENU_KEY:   synth.setKey(synth.getKeyRoot() + degOf(S, deg)); break;
    case MENU_SOUND: if (deg < INSTRUMENT_COUNT) synth.setInstrument(deg); break;
    case MENU_MODE:
    
      if (deg == 3) looper.toggleRecording();
      if (deg == 4) looper.saveCurrentLoop(1);
      if (deg == 5) synth.setBpm(synth.getBpm() - 5);
      if (deg == 6) synth.setBpm(synth.getBpm() + 5);
      break;
    default: break;
  }
}


static void padPress(uint8_t e) {
  const PadDef& p = PADS[e];
  bridgeButton(p.wsId, true);             
  if (p.role == ROLE_MENU) {
    menuHeld = (MenuId)p.arg;
    menuUsed = false;
    return;
  }
  if (p.role != ROLE_CHORD) return;

  if (menuHeld != MENU_NONE) { menuChord(menuHeld, p.arg); return; }

  Chord c = buildChord(synth, p.arg, lastMod);
  chordOn(e, synth, c);
  snprintf(lastChord, sizeof(lastChord), "%s", c.label);

  looper.logEvent(p.wsId);

  Serial.printf("[%2u] %-8s  %s\n", e, c.label, MOD_NAME[lastMod]);
}

static void padRelease(uint8_t e) {
  const PadDef& p = PADS[e];
  bridgeButton(p.wsId, false);

  if (p.role == ROLE_MENU) {
  
    if (menuHeld == (MenuId)p.arg) {
      if (!menuUsed) menuTap(menuHeld);
      menuHeld = MENU_NONE;
    }
    return;
  }
  if (p.role == ROLE_CHORD && menuHeld == MENU_NONE) chordOff(e, synth);
}

static void padService() {
  static uint32_t lastPoll = 0;
  if (millis() - lastPoll < 20) return;
  lastPoll = millis();

  int raw = mprRead16(R_TOUCH_STATUS);
  if (raw < 0) return;                         
  uint16_t now = raw & 0x0FFF;
  if (now == lastTouched) return;

  for (uint8_t e = 0; e < 12; e++) {
    bool is  = now         & (1 << e);
    bool was = lastTouched & (1 << e);
    if (is && !was)      padPress(e);
    else if (!is && was) padRelease(e);
  }

  lastTouched = now;
  uiDraw();
}

static void joyInit() {
  pinMode(PIN_JOY_SW, INPUT_PULLUP);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  long sx = 0, sy = 0;
  for (int i = 0; i < 16; i++) {              
    sx += analogRead(PIN_JOY_X);
    sy += analogRead(PIN_JOY_Y);
    delay(4);
  }
  joyCenterX = sx / 16;
  joyCenterY = sy / 16;

  joyOk = (joyCenterX > 200 && joyCenterX < 3900 &&
           joyCenterY > 200 && joyCenterY < 3900);
  Serial.printf("Joystick centre: x=%d y=%d  %s\n", joyCenterX, joyCenterY,
                joyOk ? "ok" : "-- out of range, check VRx/VRy wiring");
}

static int8_t joyAxis(int rawv, int center, bool invert) {
  int d = rawv - center;
  if (invert) d = -d;
  if (d >  JOY_DEADZONE) return  1;
  if (d < -JOY_DEADZONE) return -1;
  return 0;
}

static void joyService() {
  if (!joyOk) return;

  static uint32_t lastPoll = 0;
  uint32_t now = millis();
  if (now - lastPoll < 20) return;
  lastPoll = now;

  int8_t nx = joyAxis(analogRead(PIN_JOY_X), joyCenterX, JOY_INVERT_X);
  int8_t ny = joyAxis(analogRead(PIN_JOY_Y), joyCenterY, JOY_INVERT_Y);

 
  static int8_t   pendX = 0, pendY = 0;
  static uint32_t settleAt = 0;

  if (nx != pendX || ny != pendY) {
    pendX = nx; pendY = ny;
    settleAt = now + JOY_SETTLE_MS;
  }
  if ((pendX != stickX || pendY != stickY) && (int32_t)(now - settleAt) >= 0) {
    if (menuHeld != MENU_NONE) {
   
      menuStick(menuHeld, pendX, pendY);
      stickX = pendX; stickY = pendY;
      uiDraw();
    } else {
      stickX  = pendX;
      stickY  = pendY;
      lastMod = modFromStick(stickX, stickY);
      uiDraw();
    }
 
    bridgeStick(stickX, stickY);
  }

  bool down = (digitalRead(PIN_JOY_SW) == LOW);  
  static bool     wasDown  = false;
  static uint32_t downAt   = 0;
  static bool     holdDone = false;

  if (down && !wasDown) {
    downAt   = now;
    holdDone = false;
  } else if (down && !holdDone && now - downAt >= HOLD_MS) {
    holdDone = true;                           
    cycleOutput();
  } else if (!down && wasDown && !holdDone) {
    if (now - downAt >= 25) bridgePower();      
  }
  wasDown = down;
}


void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println("\n=== PocketChord ===");

  
  if (PIN_LED_ONBOARD >= 0) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    rgbLedWrite(PIN_LED_ONBOARD, 0, 0, 0);
#else
    neopixelWrite(PIN_LED_ONBOARD, 0, 0, 0);
#endif
    pinMode(PIN_LED_ONBOARD, OUTPUT);
    digitalWrite(PIN_LED_ONBOARD, LOW);
  }

  Wire.setPins(PIN_SDA, PIN_SCL);
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);         

  Serial.println(mprInit() ? "MPR121 ready."
                           : "MPR121 NOT responding -- run I2C_Scanner.");

  oled.setI2CAddress(OLED_ADDR << 1);         
  oledOk = oled.begin();
  Serial.println(oledOk ? "OLED ready." : "OLED NOT responding -- check address.");

  pinMode(PIN_AMP_SD, OUTPUT); digitalWrite(PIN_AMP_SD, LOW);
  if (PIN_DAC_XSMT >= 0) { pinMode(PIN_DAC_XSMT, OUTPUT); digitalWrite(PIN_DAC_XSMT, LOW); }

  sineTabInit();
  evtQ    = xQueueCreate(16, sizeof(AudioEvt));
  audioOk = (evtQ != nullptr) && audioBegin();
  if (audioOk) {
   
    xTaskCreatePinnedToCore(audioTask, "audio", 8192, NULL, 10, NULL, 1);
    delay(20);                                  
    applyOutput();
    uiBlip(76);                                  
    Serial.printf("Audio ready. Output: %s\n", OUT_NAME[outMode]);
  } else {
    Serial.println("Audio init FAILED -- check the I2S pins.");
  }

  joyInit();
  btnInit();

  
  bridgeBegin();
  share.begin();                               

  uiDraw();
}

void loop() {
  bridgeService();       
  ampService();
  joyService();
  btnService();
  padService();

  static uint32_t lastSeen = 0, lastReport = 0;
  uint32_t n = gUnderruns;
  if (n != lastSeen && millis() - lastReport > 1000) {
    Serial.printf("I2S underruns: %u (+%u)\n", n, n - lastSeen);
    lastSeen = n; lastReport = millis();
  }
}
