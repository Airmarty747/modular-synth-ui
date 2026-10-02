#pragma once
#include <Arduino.h>
#include <driver/i2s.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "SynthState.h"
#include "PadMap.h"

// --- AUDIO CONSTANTS ---
#define SAMPLE_RATE 32000
#define MAX_VOICES 12
#define AUDIO_FRAMES 256
const float TWO_PI_F = 2.0f * PI;

// --- HARDWARE PINS ---
const int PIN_I2S_BCLK = 21;
const int PIN_I2S_LRC  = 6;
const int PIN_I2S_DIN  = 42;
const int PIN_AMP_SD   = 47;

// --- SYNTHESIS STRUCTURES ---
enum Wave : uint8_t { W_SINE, W_TRI, W_SAW, W_SQR };
enum EnvStage : uint8_t { ENV_IDLE, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };
enum EvtType : uint8_t { EV_NOTE_ON, EV_NOTE_OFF };

struct Instrument {
    const char* name;
    Wave  wave;
    float det, a, d, s, r, cut, env, wet, sub;
};

// The 6 Presets from your old code
static const Instrument INSTRUMENTS[6] = {
    { "Warm Pad", W_SAW,  9,  0.35f,  0.5f,  0.75f, 1.2f,  1100, 900,  0.45f, 0.25f },
    { "E. Piano", W_TRI,  4,  0.005f, 1.1f,  0.15f, 0.5f,  2600, 2200, 0.28f, 0.35f },
    { "Organ",    W_SQR,  2,  0.01f,  0.05f, 1.0f,  0.12f, 1900, 0,    0.18f, 0.30f },
    { "Pluck",    W_SAW,  14, 0.002f, 0.35f, 0.0f,  0.35f, 3200, 2800, 0.35f, 0.20f },
    { "Glass",    W_SINE, 6,  0.01f,  0.9f,  0.2f,  0.9f,  4200, 1500, 0.55f, 0.15f },
    { "Bass",     W_SQR,  3,  0.005f, 0.4f,  0.6f,  0.25f, 800,  600,  0.10f, 0.45f }
};

struct AudioEvt {
    EvtType type;
    uint8_t src;  // The Pad ID that triggered this note
    uint8_t inst; // Instrument Index
    uint8_t count;   // Track how many notes are in this event
    int16_t midi[5]; // Hold up to 5 simultaneous notes (e.g., 4-note chord + bass)
};

struct Voice {
    bool active;
    uint8_t src;             
    Wave wave;
    bool isBass;
    uint32_t ph[2], inc[2];  
    float dt[2];             
    uint8_t oscCount;
    uint32_t phSub, incSub;
    float subGain, triZ[2];         
    EnvStage stage;
    float env, peak, sustainLevel;
    float aStep, dStep, rStep;  
    float fCut, fTarget, fCoefBlk; 
    float fG, lp1, lp2;                
};

class AudioEngine {
private:
    QueueHandle_t evtQ;
    Voice voices[MAX_VOICES];
    float globalVolume = 0.5f; // Master Volume

    int getDiatonicMidi(int degree, int rootMidi, bool isMajor) {
    const int majorScale[7] = {0, 2, 4, 5, 7, 9, 11};
    const int minorScale[7] = {0, 2, 3, 5, 7, 8, 10};
    
    int octaves = degree / 7;
    int step = degree % 7;
    const int* scale = isMajor ? majorScale : minorScale;
    
    return rootMidi + (octaves * 12) + scale[step];
    }
    
    // Sine Table for fast oscillator math
    static const uint16_t SINE_BITS = 10;
    static const uint16_t SINE_LEN  = 1 << SINE_BITS;  
    float sineTab[SINE_LEN + 1];

    void sineTabInit() {
        for (uint16_t i = 0; i <= SINE_LEN; i++) sineTab[i] = sinf(TWO_PI_F * i / SINE_LEN);
    }

    // --- MATH HELPERS ---
    static inline float midiToHz(float midi) {
        return 440.0f * powf(2.0f, (midi - 69.0f) / 12.0f);
    }

    float oscSine(uint32_t ph) {
        uint32_t idx  = ph >> (32 - SINE_BITS);
        float frac = (float)(ph & ((1UL << (32 - SINE_BITS)) - 1)) * (1.0f / (float)(1UL << (32 - SINE_BITS)));
        return sineTab[idx] + (sineTab[idx + 1] - sineTab[idx]) * frac;
    }

    float polyBlep(float t, float dt) {
        if (t < dt)        { t /= dt;               return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt;   return t * t + t + t + 1.0f; }
        return 0.0f;
    }

    float oscShape(Wave w, uint32_t ph, float dt) {
        float t = (float)ph * (1.0f / 4294967296.0f);   
        if (w == W_SINE) return oscSine(ph);
        if (w == W_SAW)  return (2.0f * t - 1.0f) - polyBlep(t, dt);
        float v = (t < 0.5f) ? 1.0f : -1.0f;            
        v += polyBlep(t, dt);
        v -= polyBlep((t < 0.5f) ? t + 0.5f : t - 0.5f, dt);
        return v;
    }

    // --- VOICE ALLOCATION & RENDERING ---
    uint32_t incForHz(float hz) {
        return (uint32_t)(hz * (4294967296.0f / (float)SAMPLE_RATE));
    }

    void voiceStart(Voice& v, int midi, const Instrument& I, uint8_t src, bool isBass) {
        v.active = true;
        v.src    = src;
        v.isBass = isBass;
        v.wave     = isBass ? W_TRI : I.wave; // Force bass to triangle
        v.oscCount = isBass ? 1 : 2;

        float hz = midiToHz((float)midi);
        for (uint8_t k = 0; k < v.oscCount; k++) {
            float cents = (k == 0) ? -I.det : I.det;
            float f     = hz * powf(2.0f, cents / 1200.0f);
            v.inc[k] = incForHz(f);
            v.dt[k]  = f / (float)SAMPLE_RATE;
            v.ph[k]  = 0;
        }
        
        v.incSub  = incForHz(hz * 0.5f);
        v.phSub   = 0;
        v.subGain = I.sub * 0.5f; 
        v.triZ[0] = v.triZ[1] = 0.0f;

        v.peak         = 0.16f;    
        v.sustainLevel = v.peak * (I.s > 0.001f ? I.s : 0.001f);
        v.env          = 0.0f;
        v.stage        = ENV_ATTACK;
        v.aStep = v.peak / fmaxf(I.a * SAMPLE_RATE, 1.0f);
        v.dStep = (v.peak - v.sustainLevel) / fmaxf(I.d * SAMPLE_RATE, 1.0f);
        v.rStep = 0.0f;                              

        v.fCut    = I.cut + I.env;
        v.fTarget = fmaxf(120.0f, I.cut);
        float tau = fmaxf((I.a + I.d) * SAMPLE_RATE, 1.0f);
        
        v.fCoefBlk = expf(-3.0f * AUDIO_FRAMES / tau);
        v.fG       = 0.0f;
        v.lp1 = v.lp2 = 0.0f;
    }

    void voiceRelease(Voice& v, float relSec) {
        if (!v.active || v.stage == ENV_RELEASE) return;
        v.stage = ENV_RELEASE;
        v.rStep = v.env / fmaxf(relSec * SAMPLE_RATE, 1.0f);
    }

    Voice* voiceAlloc() {
        for (uint8_t i = 0; i < MAX_VOICES; i++) if (!voices[i].active) return &voices[i];
        Voice* best = nullptr;
        for (uint8_t i = 0; i < MAX_VOICES; i++) {
            if (voices[i].stage != ENV_RELEASE) continue;
            if (!best || voices[i].env < best->env) best = &voices[i];
        }
        if (best) return best;
        for (uint8_t i = 0; i < MAX_VOICES; i++) if (!best || voices[i].env < best->env) best = &voices[i];
        return best;
    }

    float voiceRender(Voice& v) {
        switch (v.stage) {
            case ENV_ATTACK:
                v.env += v.aStep;
                if (v.env >= v.peak) { v.env = v.peak; v.stage = ENV_DECAY; }
                break;
            case ENV_DECAY:
                v.env -= v.dStep;
                if (v.env <= v.sustainLevel) { v.env = v.sustainLevel; v.stage = ENV_SUSTAIN; }
                break;
            case ENV_RELEASE:
                v.env -= v.rStep;
                if (v.env <= 0.0f) { v.env = 0.0f; v.active = false; return 0.0f; }
                break;
            default: break;
        }

        float s = 0.0f;
        for (uint8_t k = 0; k < v.oscCount; k++) {
            v.ph[k] += v.inc[k];
            float o = oscShape(v.wave, v.ph[k], v.dt[k]);
            if (v.wave == W_TRI) {                    
                v.triZ[k] += 4.0f * v.dt[k] * o;
                v.triZ[k] *= 0.9995f;                  
                o = v.triZ[k];
            }
            s += o;
        }
        if (v.oscCount == 2) s *= 0.5f;

        if (v.subGain > 0.0f) {
            v.phSub += v.incSub;
            s += oscSine(v.phSub) * v.subGain;
        }

        v.lp1 += (s     - v.lp1) * v.fG;
        v.lp2 += (v.lp1 - v.lp2) * v.fG;

        return v.lp2 * v.env;
    }

    // --- FREERTOS AUDIO TASK ---
    static void audioTaskWrapper(void* arg) {
        static_cast<AudioEngine*>(arg)->audioTask();
    }

    void audioTask() {
        const float CLIP_KNEE = 0.70f;
        const float CLIP_SPAN = 1.0f - CLIP_KNEE;
        const float CLIP_INV_SPAN = 1.0f / CLIP_SPAN;
        static int16_t buf[AUDIO_FRAMES * 2]; 

        for (;;) {
            AudioEvt e;
            // Pull events from the queue
            while (xQueueReceive(evtQ, &e, 0) == pdTRUE) {
                const Instrument& I = INSTRUMENTS[e.inst % 6];
                if (e.type == EV_NOTE_OFF) {
                    for (uint8_t i = 0; i < MAX_VOICES; i++) {
                        if (voices[i].active && voices[i].src == e.src) voiceRelease(voices[i], I.r);
                    }
                } else {
                    // Spawn a new voice for every note in the chord array
                    for (uint8_t n = 0; n < e.count; n++) {
                        Voice* v = voiceAlloc();
                        // The last note in the array (e.count - 1) is flagged as the bass note
                        if (v) voiceStart(*v, e.midi[n], I, e.src, (n == e.count - 1));
                    }
                }
            }

            // Update filters for active voices
            for (uint8_t i = 0; i < MAX_VOICES; i++) {
                if (voices[i].active) {
                    voices[i].fCut += (voices[i].fTarget - voices[i].fCut) * (1.0f - voices[i].fCoefBlk);
                    float g = 1.0f - expf(-TWO_PI_F * voices[i].fCut / (float)SAMPLE_RATE);
                    voices[i].fG = (g > 0.95f) ? 0.95f : g;
                }
            }

            // Render audio buffer
            for (int i = 0; i < AUDIO_FRAMES; i++) {
                float mix = 0.0f;
                for (uint8_t vI = 0; vI < MAX_VOICES; vI++) {
                    if (voices[vI].active) mix += voiceRender(voices[vI]);
                }
                mix *= globalVolume;

                // Soft Clipping (Distortion prevention)
                float a = fabsf(mix);
                if (a > CLIP_KNEE) {
                    float u = (a - CLIP_KNEE) * CLIP_INV_SPAN;
                    a = CLIP_KNEE + CLIP_SPAN * (u / (1.0f + u));
                    mix = (mix < 0.0f) ? -a : a;
                }

                int16_t o = (int16_t)(mix * 32000.0f);
                buf[i * 2]     = o;                
                buf[i * 2 + 1] = o;                
            }

            // Push buffer to amplifier DMA
            size_t written;
            i2s_write(I2S_NUM_0, buf, AUDIO_FRAMES * 2 * sizeof(int16_t), &written, portMAX_DELAY);
        }
    }

public:
    AudioEngine() {
        for (int i = 0; i < MAX_VOICES; i++) voices[i].active = false;
    }

    void begin() {
        sineTabInit();
        
        // Wake up amplifier
        pinMode(PIN_AMP_SD, OUTPUT);
        digitalWrite(PIN_AMP_SD, HIGH);

        // I2S Configuration
        i2s_config_t cfg = {
            .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
            .sample_rate          = SAMPLE_RATE,
            .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
            .channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT,
            .communication_format = I2S_COMM_FORMAT_STAND_I2S,
            .intr_alloc_flags     = 0,
            .dma_buf_count        = 6,
            .dma_buf_len          = 256,
            .use_apll             = false,
            .tx_desc_auto_clear   = true,
            .fixed_mclk           = 0
        };
        
        i2s_pin_config_t pins = {
            .mck_io_num   = I2S_PIN_NO_CHANGE,
            .bck_io_num   = PIN_I2S_BCLK,
            .ws_io_num    = PIN_I2S_LRC,
            .data_out_num = PIN_I2S_DIN,
            .data_in_num  = I2S_PIN_NO_CHANGE
        };

        i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
        i2s_set_pin(I2S_NUM_0, &pins);

        // Create the FreeRTOS Event Queue
        evtQ = xQueueCreate(32, sizeof(AudioEvt));

        // Launch the Audio Math Engine onto a background processor core
        xTaskCreatePinnedToCore(audioTaskWrapper, "AudioTask", 8192, this, 1, NULL, 1);
        
        Serial.println("AudioEngine: FreeRTOS Polyphonic Synth Booted!");

        // // --- HARDWARE DIAGNOSTIC BEEP ---
        // Serial.println("Playing diagnostic beep...");
        
        // // Spawn a fake event to force the speaker to play Middle C
        // AudioEvt beep = {};
        // beep.type = EV_NOTE_ON;
        // beep.src = 99; // Fake pad ID
        // beep.inst = 1; // E. Piano
        // beep.count = 1;
        // beep.midi[0] = 60; // Middle C
        
        // if (evtQ) {
        //     xQueueSend(evtQ, &beep, 0);
        // }
    }

    // Pass the Pad ID (0-11) and it will map it to a MIDI note based on your SynthState
    void playPad(uint8_t padId, const SynthState& synth) {
        // Check PadMap to ensure this is actually a chord button
        if (PADS[padId].role != ROLE_CHORD) return;

        int rootMidi = 60 + synth.getKeyRoot() + (synth.getOctaveOffset() * 12);
        bool isMajor = (synth.getScaleName() == "major");
        
        int degree = PADS[padId].arg; // e.g., 0 = I chord, 4 = V chord

        AudioEvt e = {};
        e.type = EV_NOTE_ON;
        e.src  = padId;
        e.inst = synth.getInstrumentIndex();
        e.count = 4; // 3-note triad + 1 bass note
        
        // 1. Root Note of the chord
        e.midi[0] = getDiatonicMidi(degree, rootMidi, isMajor);     
        // 2. The 3rd (Two scale degrees up)
        e.midi[1] = getDiatonicMidi(degree + 2, rootMidi, isMajor); 
        // 3. The 5th (Four scale degrees up)
        e.midi[2] = getDiatonicMidi(degree + 4, rootMidi, isMajor); 
        // 4. Bass Note (Root dropped exactly one octave)
        e.midi[3] = getDiatonicMidi(degree, rootMidi - 12, isMajor); 

        if (evtQ) xQueueSend(evtQ, &e, 0);
    }

    void stopNote(uint8_t padId, const SynthState& synth) {
        AudioEvt e = {};
        e.type = EV_NOTE_OFF;
        e.src  = padId;
        e.inst = synth.getInstrumentIndex();
        if (evtQ) xQueueSend(evtQ, &e, 0);
    }
};