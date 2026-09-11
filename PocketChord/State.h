#pragma once
#include "Config.h"
#include "PadMap.h"
#include "Theory.h"
#include "src/marty/SynthState.h"
#include "src/marty/Looper.h"
#include "src/marty/DummyMemory.h"
#include "src/marty/ShareManager.h"


SynthState  synth;                  
DummyMemory storage;                  
Looper      looper(&storage);      
ShareManager share;                  


uint16_t lastTouched = 0;             
bool     oledOk      = false;
bool     joyOk       = false;

int    joyCenterX = 2048, joyCenterY = 2048;
int8_t stickX = 0, stickY = 0;         

MenuId menuHeld = MENU_NONE;
bool   menuUsed = false;       
char lastChord[12] = "-";          
ChordMod lastMod   = MOD_CENTER;

enum OutMode { OUT_SPEAKER, OUT_PHONES, OUT_BOTH };
OutMode outMode = OUT_SPEAKER;
static const char* OUT_NAME[3] = { "SPK", "HP", "SPK+HP" };

static bool speakerWanted() { return outMode == OUT_SPEAKER || outMode == OUT_BOTH; }
static bool phonesWanted()  { return outMode == OUT_PHONES  || outMode == OUT_BOTH; }

uint32_t ampOffAt = 0;         
struct Button {
  int      pin;
  uint8_t  active;    
  uint8_t  integ;   
  bool     down;   
#if BTN_DEBUG
  bool     dbgSeen;    
  bool     dbgRaw;      
  uint16_t dbgFlips;   
#endif
};

Button btnVol = { PIN_BTN_VOL, LOW, 0, false };
Button btnOut = { PIN_BTN_OUT, LOW, 0, false };
