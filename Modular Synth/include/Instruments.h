#pragma once
// Instruments.h -- the instrument table, moved out of AudioEngine.h so that the
// display and the synth state can use it without pulling in the audio code.
// Add an instrument by adding one row: NUM_INSTRUMENTS updates itself.

#include <stdint.h>

enum Wave : uint8_t { W_SINE, W_TRI, W_SAW, W_SQR };

struct Instrument {
    const char* name;   // 8 characters or fewer so it fits the OLED
    Wave  wave;
    float det, a, d, s, r, cut, env, wet, sub;
};

static const Instrument INSTRUMENTS[] = {
    { "Warm Pad", W_SAW,  9,  0.35f,  0.5f,  0.75f, 1.2f,  1100, 900,  0.45f, 0.25f },
    { "E. Piano", W_TRI,  4,  0.005f, 1.1f,  0.15f, 0.5f,  2600, 2200, 0.28f, 0.35f },
    { "Organ",    W_SQR,  2,  0.01f,  0.05f, 1.0f,  0.12f, 1900, 0,    0.18f, 0.30f },
    { "Pluck",    W_SAW,  14, 0.002f, 0.35f, 0.0f,  0.35f, 3200, 2800, 0.35f, 0.20f },
    { "Glass",    W_SINE, 6,  0.01f,  0.9f,  0.2f,  0.9f,  4200, 1500, 0.55f, 0.15f },
    { "Bass",     W_SQR,  3,  0.005f, 0.4f,  0.6f,  0.25f, 800,  600,  0.10f, 0.45f }
};
#define NUM_INSTRUMENTS ((int)(sizeof(INSTRUMENTS) / sizeof(INSTRUMENTS[0])))

static inline const char* instrumentName(int idx) {
    if (idx < 0 || idx >= NUM_INSTRUMENTS) return "?";
    return INSTRUMENTS[idx].name;
}
