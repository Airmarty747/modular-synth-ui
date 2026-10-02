#pragma once

enum PadRole : uint8_t {
  ROLE_NONE = 0,  
  ROLE_CHORD,     
  ROLE_MENU      
};

enum MenuId : uint8_t { MENU_KEY = 0, MENU_SOUND = 1, MENU_MODE = 2, MENU_NONE = 255 };

struct PadDef {
  PadRole     role;
  uint8_t     arg;   
  uint8_t     wsId;    
  const char* label;
};


static const PadDef PADS[12] = {
  { ROLE_NONE,  0,          0,  "-"   },
  { ROLE_MENU,  MENU_KEY,   1,  "KEY" },
  { ROLE_MENU,  MENU_SOUND, 2,  "SND" },
  { ROLE_MENU,  MENU_MODE,  3,  "MOD" },

  { ROLE_NONE,  0,          0,  "-"   },
  { ROLE_CHORD, 1,          9,  "2"   },
  { ROLE_CHORD, 3,          10, "4"   },
  { ROLE_CHORD, 5,          11, "6"   },

  { ROLE_CHORD, 0,          12, "1"   },
  { ROLE_CHORD, 2,          13, "3"   },
  { ROLE_CHORD, 4,          14, "5"   },
  { ROLE_CHORD, 6,          15, "7"   }
};
static const uint8_t PAD_COLS = 4;
