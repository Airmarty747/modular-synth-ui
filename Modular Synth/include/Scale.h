#pragma once
// Scale.h -- scales as DATA, in cents above the tonic.
//
// A degree's pitch is  tonic + cents[degree % count] / 100  semitones (+12 per octave).
// Because the table holds cents, any tuning works: 12-tone equal temperament for
// Western scales, just intonation for Indian ones, and later anything loaded
// from a file (Scala .scl files are exactly "a list of cents or ratios").
//
// The pads give degrees 0-6. For scales with a different note count the same
// maths still works (degree 7 on a 5-note scale is simply the next octave).

#include <Arduino.h>
#include <math.h>
#include <strings.h>

enum Tradition : uint8_t { TRAD_WESTERN = 0, TRAD_INDIAN = 1, TRAD_COUNT = 2 };

#define SCALE_MAX_DEGREES 12

struct Scale {
    const char* name;                    // keep to 8 characters or fewer: it must fit the OLED
    Tradition   trad;
    uint8_t     count;                   // number of notes per octave
    float       cents[SCALE_MAX_DEGREES];
};

// Just-intonation reference values used below (cents):
//   komal Re 111.73 (16/15)   Re 203.91 (9/8)    komal Ga 315.64 (6/5)   Ga 386.31 (5/4)
//   Ma 498.04 (4/3)           tivra Ma 590.22 (45/32)   Pa 701.96 (3/2)
//   komal Dha 813.69 (8/5)    Dha 884.36 (5/3)   komal Ni 996.09 (16/9)  Ni 1088.27 (15/8)
static const Scale SCALES[] = {
    // ---- Western: 12-tone equal temperament (the seven church modes) ----
    { "Major",    TRAD_WESTERN, 7, {   0, 200, 400, 500, 700, 900, 1100 } },  // Ionian
    { "Dorian",   TRAD_WESTERN, 7, {   0, 200, 300, 500, 700, 900, 1000 } },
    { "Phrygian", TRAD_WESTERN, 7, {   0, 100, 300, 500, 700, 800, 1000 } },
    { "Lydian",   TRAD_WESTERN, 7, {   0, 200, 400, 600, 700, 900, 1100 } },
    { "Mixolyd",  TRAD_WESTERN, 7, {   0, 200, 400, 500, 700, 900, 1000 } },
    { "Minor",    TRAD_WESTERN, 7, {   0, 200, 300, 500, 700, 800, 1000 } },  // Aeolian
    { "Locrian",  TRAD_WESTERN, 7, {   0, 100, 300, 500, 600, 800, 1000 } },

    // ---- Indian: just intonation, one thaat/raga scale each ----
    { "Bilawal",  TRAD_INDIAN,  7, { 0.00f, 203.91f, 386.31f, 498.04f, 701.96f, 884.36f, 1088.27f } },
    { "Yaman",    TRAD_INDIAN,  7, { 0.00f, 203.91f, 386.31f, 590.22f, 701.96f, 884.36f, 1088.27f } },
    { "Bhairav",  TRAD_INDIAN,  7, { 0.00f, 111.73f, 386.31f, 498.04f, 701.96f, 813.69f, 1088.27f } },
    { "Kafi",     TRAD_INDIAN,  7, { 0.00f, 203.91f, 315.64f, 498.04f, 701.96f, 884.36f,  996.09f } },
    { "Todi",     TRAD_INDIAN,  7, { 0.00f, 111.73f, 315.64f, 590.22f, 701.96f, 813.69f, 1088.27f } },
    { "Marwa",    TRAD_INDIAN,  7, { 0.00f, 111.73f, 386.31f, 590.22f, 701.96f, 884.36f, 1088.27f } },
};
#define NUM_SCALES ((int)(sizeof(SCALES) / sizeof(SCALES[0])))

static inline const char* traditionName(Tradition t) {
    return (t == TRAD_INDIAN) ? "Indian" : "Western";
}

// First scale belonging to a tradition (0 if none, which cannot happen with the table above).
static inline int scaleFirstOf(Tradition t) {
    for (int i = 0; i < NUM_SCALES; i++) if (SCALES[i].trad == t) return i;
    return 0;
}

// Next (dir = +1) or previous (dir = -1) scale of the same tradition as `idx`, wrapping.
static inline int scaleStep(int idx, int dir) {
    Tradition t = SCALES[idx].trad;
    int i = idx;
    for (int n = 0; n < NUM_SCALES; n++) {
        i = (i + (dir >= 0 ? 1 : NUM_SCALES - 1)) % NUM_SCALES;
        if (SCALES[i].trad == t) return i;
    }
    return idx;
}

// Case-insensitive name lookup. Returns -1 if not found. "major" and "minor" both exist.
static inline int scaleFindByName(const char* name) {
    if (!name) return -1;
    for (int i = 0; i < NUM_SCALES; i++) if (strcasecmp(SCALES[i].name, name) == 0) return i;
    return -1;
}

// Pitch of a scale degree as a FLOAT midi note (60.0 = middle C; fractions are microtonal).
// Works for negative degrees too (they fall into lower octaves).
static inline float scaleDegreePitch(const Scale& s, int degree, float tonicMidi) {
    int oct  = degree / (int)s.count;
    int step = degree % (int)s.count;
    if (step < 0) { step += s.count; oct -= 1; }
    return tonicMidi + 12.0f * (float)oct + s.cents[step] / 100.0f;
}
