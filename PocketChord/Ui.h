#pragma once
#include <U8g2lib.h>
#include "State.h"
#include "Audio.h"

static U8G2_SSD1309_128X64_NONAME0_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);
static const int GRID_TOP = 25;
static const int CELL_W   = 32;
static const int CELL_H   = 13;

static void drawStatus() {
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(0, 9, lastChord);
  oled.drawStr(72, 9, audioOk ? OUT_NAME[outMode] : "(mute)");
  if (audioOk) {
    oled.drawFrame(100, 2, 28, 7);
    oled.drawBox(102, 4, (vol + 1) * 24 / VOL_STEPS, 3);
  }

  char l2[32];
  snprintf(l2, sizeof(l2), "%s %s  %s",
           NOTE_NAME[synth.getKeyRoot()],
           synth.getScaleName() == "minor" ? "min" : "maj",
           INSTRUMENTS[synth.getInstrumentIndex() % INSTRUMENT_COUNT].name);
  oled.drawStr(0, 20, l2);

  if (lastMod != MOD_CENTER) {
    const char* m = MOD_NAME[lastMod];
    oled.drawStr(128 - oled.getStrWidth(m), 20, m);
  }

  oled.drawHLine(0, 22, 128);
}

static void drawGrid(uint16_t touched) {
  oled.setFont(u8g2_font_5x8_tf);
  for (uint8_t e = 0; e < 12; e++) {
    int  col = e % PAD_COLS;
    int  row = e / PAD_COLS;
    int  x   = col * CELL_W + 1;
    int  y   = GRID_TOP + row * CELL_H;
    bool on  = touched & (1 << e);

    if (on) oled.drawBox(x, y, CELL_W - 2, CELL_H - 1);
    else    oled.drawFrame(x, y, CELL_W - 2, CELL_H - 1);

    oled.setDrawColor(on ? 0 : 1);
    const char* lab = PADS[e].label;
    int tw = oled.getStrWidth(lab);
    oled.drawStr(x + (CELL_W - 2 - tw) / 2, y + CELL_H - 4, lab);
    oled.setDrawColor(1);
  }
}

static void drawMenu() {
  oled.setFont(u8g2_font_6x10_tf);
  const char* title = "";
  const char* big   = "";
  char        bigBuf[24];

  switch (menuHeld) {
    case MENU_KEY:
      title = "KEY";
      snprintf(bigBuf, sizeof(bigBuf), "%s", NOTE_NAME[synth.getKeyRoot()]);
      big = bigBuf;
      break;
    case MENU_SOUND:
      title = "SOUND";
      big = INSTRUMENTS[synth.getInstrumentIndex() % INSTRUMENT_COUNT].name;
      break;
    case MENU_MODE:
      title = "MODE";
      snprintf(bigBuf, sizeof(bigBuf), "%d bpm", synth.getBpm());
      big = bigBuf;
      break;
    default: return;
  }

  oled.drawStr(0, 34, title);
  oled.setFont(u8g2_font_10x20_tf);
  oled.drawStr(0, 52, big);

  oled.setFont(u8g2_font_5x8_tf);
  const char* hint = (menuHeld == MENU_KEY)   ? "L/R key  U maj  D min"
                   : (menuHeld == MENU_SOUND) ? "L/R sound  U/D octave  1-6 pick"
                                              : "L/R bpm  4 rec  5 save";
  oled.drawStr(0, 63, hint);
}

static void uiDraw() {
  if (!oledOk) return;
  oled.clearBuffer();
  drawStatus();
  if (menuHeld != MENU_NONE) drawMenu();
  else                       drawGrid(lastTouched);
  oled.sendBuffer();
}
