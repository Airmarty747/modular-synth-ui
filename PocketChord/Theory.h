#pragma once
#include "src/marty/SynthState.h"

static const int8_t SCALE_MAJOR[7] = { 0, 2, 4, 5, 7, 9, 11 };
static const int8_t SCALE_MINOR[7] = { 0, 2, 3, 5, 7, 8, 10 };

enum ChordMod : uint8_t {
  MOD_CENTER = 0,
  MOD_UP,
  MOD_UPRIGHT,
  MOD_RIGHT,
  MOD_DOWNRIGHT,
  MOD_DOWN,
  MOD_DOWNLEFT,
  MOD_LEFT,
  MOD_UPLEFT
};

static const char* MOD_NAME[9] = {
  "none", "7th", "add9", "maj/min", "6th", "sus2", "sus4", "slash", "power"
};

static const char* NOTE_NAME[12] = {
  "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

static ChordMod modFromStick(int8_t x, int8_t y) {
  bool u = (y < 0), dn = (y > 0), l = (x < 0), r = (x > 0);
  if (u  && l) return MOD_UPLEFT;
  if (u  && r) return MOD_UPRIGHT;
  if (dn && l) return MOD_DOWNLEFT;
  if (dn && r) return MOD_DOWNRIGHT;
  if (u)  return MOD_UP;
  if (dn) return MOD_DOWN;
  if (l)  return MOD_LEFT;
  if (r)  return MOD_RIGHT;
  return MOD_CENTER;
}

static inline int degOf(const int8_t* s, int d) {
  int idx = ((d % 7) + 7) % 7;
  int oct = (d >= 0) ? (d / 7) : -(((-d) + 6) / 7);
  return s[idx] + 12 * oct;
}

struct Chord {
  int8_t  notes[4]; 
  uint8_t count;
  int8_t  bass;       
  char    label[12];  
};

static Chord buildChord(const SynthState& st, int degIdx, ChordMod mod) {
  const int8_t* S = (st.getScaleName() == "minor") ? SCALE_MINOR : SCALE_MAJOR;

  int root    = degOf(S, degIdx);
  int third   = degOf(S, degIdx + 2);
  int fifth   = degOf(S, degIdx + 4);
  int sixth   = degOf(S, degIdx + 5);
  int seventh = degOf(S, degIdx + 6);
  int second  = degOf(S, degIdx + 1);
  int fourth  = degOf(S, degIdx + 3);
  int ninth   = degOf(S, degIdx + 8);

  Chord c;
  c.notes[0] = root; c.notes[1] = third; c.notes[2] = fifth;
  c.count    = 3;
  c.bass     = root - 12;

  int         t3     = third - root;
  const char* suffix = "";
  int         slash  = -128;         

  switch (mod) {
    case MOD_UP:        c.notes[3] = seventh; c.count = 4; break;
    case MOD_UPRIGHT:   c.notes[3] = ninth;   c.count = 4; break;
    case MOD_RIGHT: {  
      int f = (t3 == 3) ? third + 1 : third - 1;
      c.notes[1] = f; t3 = f - root;
      break;
    }
    case MOD_DOWNRIGHT: c.notes[3] = sixth;   c.count = 4; break;
    case MOD_DOWN:      c.notes[1] = second;  suffix = "sus2"; break;
    case MOD_DOWNLEFT:  c.notes[1] = fourth;  suffix = "sus4"; break;
    case MOD_LEFT:      c.bass = third - 12;  slash = third;   break;
    case MOD_UPLEFT:    c.notes[1] = fifth;   c.count = 2; suffix = "5"; break;
    default: break;
  }

  int t5 = fifth - root, t7 = seventh - root;
  const char* q = (t3 == 3 && t5 == 6) ? "dim"
                : (t3 == 3)            ? "m"
                : (t3 == 4 && t5 == 8) ? "aug"
                                       : "";

  char name[8];
  if (suffix[0])              snprintf(name, sizeof(name), "%s", suffix);
  else if (mod == MOD_UP)     snprintf(name, sizeof(name), "%s",
                                       (strcmp(q, "dim") == 0) ? "m7b5"
                                       : (t7 == 11) ? (strcmp(q, "m") == 0 ? "mmaj7" : "maj7")
                                                    : (strcmp(q, "m") == 0 ? "m7" : "7"));
  else if (mod == MOD_UPRIGHT)   snprintf(name, sizeof(name), "%sadd9", q);
  else if (mod == MOD_DOWNRIGHT) snprintf(name, sizeof(name), "%s6", q);
  else                           snprintf(name, sizeof(name), "%s", q);

  int keyRoot = st.getKeyRoot();
  auto nm = [&](int n) { return NOTE_NAME[(((keyRoot + n) % 12) + 12) % 12]; };

  if (slash != -128) snprintf(c.label, sizeof(c.label), "%s%s/%s", nm(root), name, nm(slash));
  else               snprintf(c.label, sizeof(c.label), "%s%s",    nm(root), name);

  return c;
}

static inline int chordMidi(const SynthState& st, int semitone) {
  return 60 + st.getKeyRoot() + semitone + 12 * st.getOctaveOffset();
}

static inline float midiToHz(float m) { return 440.0f * powf(2.0f, (m - 69.0f) / 12.0f); }
