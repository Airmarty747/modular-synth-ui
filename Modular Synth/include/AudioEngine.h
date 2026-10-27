#pragma once
// AudioEngine.h -- v2
//
// Changes from v1:
//  * Pitch is a float midi note (fractions = microtones), built from the Scale tables.
//  * Voice pool is 32 (4 loop layers + live play); change with -DMAX_VOICES=n.
//  * When the pool is full, a WHOLE chord is faded out over ~5 ms to make room and
//    the new chord waits (up to one audio block, ~4 ms) for the space. v1 stole
//    single voices, which pulled notes out of other chords and clicked.
//  * Release time is stored per voice at note-on, so changing instrument while a
//    pad is held no longer changes how that note releases.
//  * Strum: chord notes start bottom to top, `strumMs` apart.
//  * Note styles: gated (finger), fixed length, latched (see SynthState.h).
//  * Instruments with sustain 0 (Pluck) free their voice when the decay ends.
//  * Smaller audio block (128 frames) and DMA (4 x 128) = about 16 ms of buffering
//    instead of 48 ms, and the audio task runs above the main loop's priority.
//  * CPU report: call printStats() every couple of seconds and read the serial monitor.
//  * Public API is unchanged, so the existing InputManager / main loop still compile:
//      playPad(pad, synth), playPad(pad, synth, src), stopNote(src, synth), bootChime().

#include <Arduino.h>
#include <driver/i2s.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "SynthState.h"
#include "Instruments.h"
#include "Scale.h"
#include "PadMap.h"

// --- AUDIO CONSTANTS ---
#define SAMPLE_RATE 32000
#ifndef MAX_VOICES
#define MAX_VOICES 32
#endif
#ifndef AUDIO_FRAMES
#define AUDIO_FRAMES 128        // frames rendered per block; block time = 4 ms at 32 kHz
#endif
#ifndef DMA_BUF_COUNT
#define DMA_BUF_COUNT 4         // DMA buffers of AUDIO_FRAMES each
#endif
#ifndef LITE_LOOP_VOICES
#define LITE_LOOP_VOICES 0      // set to 1 to render looped chords with one oscillator and no sub (cheaper)
#endif
#define LOOP_VOICE_OFFSET 12    // looper playback uses sources 12-23 so it never cuts off live pads 0-11
#define PEND_MAX 4              // chords that may wait for voices at once
const float TWO_PI_F = 2.0f * PI;

// --- HARDWARE PINS ---
const int PIN_I2S_BCLK = 21;
const int PIN_I2S_LRC  = 6;
const int PIN_I2S_DIN  = 42;
const int PIN_AMP_SD   = 47;

enum EnvStage : uint8_t { ENV_IDLE, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };
enum EvtType  : uint8_t { EV_NOTE_ON, EV_NOTE_OFF };

#define EVF_BASS_FIRST 0x01     // pitch[0] is the bass note (renders as a triangle, one oscillator)
#define EVF_LITE       0x02     // render with the cheap voice

struct AudioEvt {
    EvtType  type;
    uint8_t  src;        // which pad (or looper pad + 12) owns these voices
    uint8_t  inst;
    uint8_t  count;      // notes in this event (max 5)
    uint8_t  flags;
    uint16_t strumMs;    // delay between successive notes, bottom to top
    uint16_t gateMs;     // 0 = hold until note-off; otherwise release automatically after this long
    float    pitch[5];   // float midi notes, listed bottom to top
};

struct Voice {
    bool active, fast, autoFree, isBass;
    uint8_t src;
    Wave wave;
    uint8_t oscCount;
    int32_t delay;       // samples left before this voice starts sounding (strum)
    int32_t gateLeft;    // samples left before auto-release (0 = none)
    float relSec;        // release time, copied from the instrument at note-on
    uint32_t ph[2], inc[2];
    float dt[2];
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
    QueueHandle_t evtQ = nullptr;
    Voice voices[MAX_VOICES];
    float globalVolume = 0.5f;

    // Waiting room for chords that arrived while the pool was full (audio task only).
    AudioEvt pend[PEND_MAX];
    int pendN = 0;

    // Main-loop side state for latch mode (playPad / stopNote are only called from the main loop).
    uint32_t latchMask = 0;

    // CPU / health counters, written by the audio task, read by printStats().
    volatile uint32_t statBlocks = 0, statSumUs = 0, statMaxUs = 0, statOver = 0;
    volatile uint32_t statPeakVoices = 0, statStolen = 0, statDropped = 0, statQueueFull = 0;

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
        uint32_t idx = ph >> (32 - SINE_BITS);
        float frac = (float)(ph & ((1UL << (32 - SINE_BITS)) - 1)) * (1.0f / (float)(1UL << (32 - SINE_BITS)));
        return sineTab[idx] + (sineTab[idx + 1] - sineTab[idx]) * frac;
    }

    float polyBlep(float t, float dt) {
        if (t < dt)        { t /= dt;             return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
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

    uint32_t incForHz(float hz) {
        return (uint32_t)(hz * (4294967296.0f / (float)SAMPLE_RATE));
    }

    // --- VOICES ---
    void voiceStart(Voice& v, float pitch, const Instrument& I, uint8_t src, bool isBass, bool lite,
                    int32_t delaySamples, int32_t gateSamples) {
        v.active   = true;
        v.fast     = false;
        v.src      = src;
        v.isBass   = isBass;
        v.delay    = delaySamples;
        v.gateLeft = gateSamples;
        v.relSec   = I.r;
        v.autoFree = (I.s <= 0.001f);
        v.wave     = isBass ? W_TRI : I.wave;
        v.oscCount = (isBass || lite) ? 1 : 2;

        float hz = midiToHz(pitch);
        for (uint8_t k = 0; k < v.oscCount; k++) {
            float cents = (k == 0) ? -I.det : I.det;
            float f = hz * powf(2.0f, cents / 1200.0f);
            v.inc[k] = incForHz(f);
            v.dt[k]  = f / (float)SAMPLE_RATE;
            v.ph[k]  = 0;
        }

        v.incSub  = incForHz(hz * 0.5f);
        v.phSub   = 0;
        v.subGain = lite ? 0.0f : I.sub * 0.5f;
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
        if (v.delay > 0) { v.active = false; return; }       // strummed note that never started
        v.stage = ENV_RELEASE;
        v.rStep = v.env / fmaxf(relSec * SAMPLE_RATE, 1.0f);
    }

    // ~5 ms fade so that a stolen voice does not click.
    void voiceFastFade(Voice& v) {
        if (!v.active) return;
        if (v.delay > 0) { v.active = false; return; }
        float step = fmaxf(v.env / (0.005f * SAMPLE_RATE), 1e-6f);
        if (v.stage == ENV_RELEASE) step = fmaxf(step, v.rStep);
        v.stage = ENV_RELEASE;
        v.rStep = step;
        v.fast  = true;
    }

    float voiceRender(Voice& v) {
        if (v.delay > 0) { v.delay--; return 0.0f; }
        if (v.gateLeft > 0 && --v.gateLeft == 0) voiceRelease(v, v.relSec);

        switch (v.stage) {
            case ENV_ATTACK:
                v.env += v.aStep;
                if (v.env >= v.peak) { v.env = v.peak; v.stage = ENV_DECAY; }
                break;
            case ENV_DECAY:
                v.env -= v.dStep;
                if (v.env <= v.sustainLevel) {
                    v.env = v.sustainLevel;
                    v.stage = ENV_SUSTAIN;
                    if (v.autoFree) { v.active = false; return 0.0f; }   // e.g. Pluck: decayed to nothing
                }
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

    // --- CHORD ALLOCATION (audio task only) ---

    // Fade out whole chords, quietest first, until enough voices are free or fading.
    void stealFor(int needed) {
        int freeN = 0, fading = 0;
        for (int i = 0; i < MAX_VOICES; i++) {
            if (!voices[i].active) freeN++;
            else if (voices[i].fast) fading++;
        }
        int deficit = needed - freeN - fading;
        while (deficit > 0) {
            int bestSrc = -1, bestN = 0;
            float bestSum = 1e9f;
            for (int i = 0; i < MAX_VOICES; i++) {
                if (!voices[i].active || voices[i].fast) continue;
                bool seen = false;
                for (int j = 0; j < i; j++)
                    if (voices[j].active && !voices[j].fast && voices[j].src == voices[i].src) { seen = true; break; }
                if (seen) continue;
                float sum = 0.0f; int n = 0;
                for (int k = 0; k < MAX_VOICES; k++)
                    if (voices[k].active && !voices[k].fast && voices[k].src == voices[i].src) { sum += voices[k].env; n++; }
                if (sum < bestSum) { bestSum = sum; bestSrc = voices[i].src; bestN = n; }
            }
            if (bestSrc < 0) break;
            for (int k = 0; k < MAX_VOICES; k++)
                if (voices[k].active && !voices[k].fast && voices[k].src == bestSrc) voiceFastFade(voices[k]);
            statStolen = statStolen + 1;
            deficit -= bestN;
        }
    }

    // Start a chord if there is room for all of it. Otherwise make room and report false.
    bool tryStart(const AudioEvt& e) {
        Voice* freeV[MAX_VOICES];
        int nFree = 0;
        for (int i = 0; i < MAX_VOICES; i++) if (!voices[i].active) freeV[nFree++] = &voices[i];
        if (nFree < e.count) { stealFor(e.count); return false; }

        const Instrument& I = INSTRUMENTS[e.inst % NUM_INSTRUMENTS];
        bool lite = (e.flags & EVF_LITE) != 0;
        int32_t strumSamples = (int32_t)e.strumMs * (SAMPLE_RATE / 1000);
        int32_t gateSamples  = (int32_t)e.gateMs  * (SAMPLE_RATE / 1000);
        for (uint8_t n = 0; n < e.count; n++) {
            bool isBass = (n == 0) && (e.flags & EVF_BASS_FIRST);
            int32_t d = (int32_t)n * strumSamples;
            // gateLeft only counts down once the voice is sounding (after its strum delay).
            voiceStart(*freeV[n], e.pitch[n], I, e.src, isBass, lite, d, gateSamples);
        }
        return true;
    }

    void handleEvent(const AudioEvt& e) {
        if (e.type == EV_NOTE_OFF) {
            for (int i = 0; i < MAX_VOICES; i++)
                if (voices[i].active && voices[i].src == e.src) voiceRelease(voices[i], voices[i].relSec);
            for (int i = 0; i < pendN; ) {                    // a chord still waiting for room is cancelled
                if (pend[i].src == e.src) { for (int j = i; j < pendN - 1; j++) pend[j] = pend[j + 1]; pendN--; }
                else i++;
            }
            return;
        }
        if (pendN == 0 && tryStart(e)) return;
        if (pendN < PEND_MAX) pend[pendN++] = e;
        else statDropped = statDropped + 1;
    }

    void servicePending() {
        while (pendN > 0) {
            if (!tryStart(pend[0])) break;
            for (int j = 0; j < pendN - 1; j++) pend[j] = pend[j + 1];
            pendN--;
        }
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
        const uint32_t BUDGET_US = (uint32_t)((uint64_t)AUDIO_FRAMES * 1000000ULL / SAMPLE_RATE);

        for (;;) {
            AudioEvt e;
            while (xQueueReceive(evtQ, &e, 0) == pdTRUE) handleEvent(e);
            servicePending();

            uint32_t t0 = micros();

            uint32_t nActive = 0;
            for (int i = 0; i < MAX_VOICES; i++) {
                if (!voices[i].active) continue;
                nActive++;
                voices[i].fCut += (voices[i].fTarget - voices[i].fCut) * (1.0f - voices[i].fCoefBlk);
                float g = 1.0f - expf(-TWO_PI_F * voices[i].fCut / (float)SAMPLE_RATE);
                voices[i].fG = (g > 0.95f) ? 0.95f : g;
            }

            for (int i = 0; i < AUDIO_FRAMES; i++) {
                float mix = 0.0f;
                for (int vI = 0; vI < MAX_VOICES; vI++)
                    if (voices[vI].active) mix += voiceRender(voices[vI]);
                mix *= globalVolume;

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

            uint32_t dt = micros() - t0;
            statBlocks = statBlocks + 1;
            statSumUs  = statSumUs + dt;
            if (dt > statMaxUs) statMaxUs = dt;
            if (dt > BUDGET_US) statOver = statOver + 1;
            if (nActive > statPeakVoices) statPeakVoices = nActive;

            size_t written;
            i2s_write(I2S_NUM_0, buf, AUDIO_FRAMES * 2 * sizeof(int16_t), &written, portMAX_DELAY);
        }
    }

    // --- MAIN-LOOP SIDE HELPERS ---
    void sendOff(uint8_t src) {
        AudioEvt e = {};
        e.type = EV_NOTE_OFF;
        e.src  = src;
        // A dropped note-off would leave a note stuck, so this one waits a few ms for room.
        if (evtQ && xQueueSend(evtQ, &e, pdMS_TO_TICKS(5)) != pdTRUE) statQueueFull = statQueueFull + 1;
    }

    void releaseLatched() {
        for (uint8_t s = 0; s < 32; s++)
            if (latchMask & (1u << s)) sendOff(s);
        latchMask = 0;
    }

public:
    AudioEngine() {
        for (int i = 0; i < MAX_VOICES; i++) voices[i].active = false;
    }

    void begin() {
        sineTabInit();

        pinMode(PIN_AMP_SD, OUTPUT);
        digitalWrite(PIN_AMP_SD, HIGH);

        i2s_config_t cfg = {
            .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
            .sample_rate          = SAMPLE_RATE,
            .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
            .channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT,
            .communication_format = I2S_COMM_FORMAT_STAND_I2S,
            .intr_alloc_flags     = 0,
            .dma_buf_count        = DMA_BUF_COUNT,
            .dma_buf_len          = AUDIO_FRAMES,
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

        evtQ = xQueueCreate(64, sizeof(AudioEvt));

        // Priority 5 on core 1: above the Arduino loop task (priority 1), so a slow screen
        // update can never delay a buffer. The task sleeps inside i2s_write, so loop() still runs.
        xTaskCreatePinnedToCore(audioTaskWrapper, "AudioTask", 8192, this, 5, NULL, 1);

        Serial.println("AudioEngine v2: polyphonic synth booted.");
    }

    // Four notes on the way up so you can hear at power-on that the amp and I2S are alive.
    // Uses explicit notes, not the pad map, so silence here means the speaker chain only.
    void bootChime() {
        const float notes[4] = { 60, 64, 67, 72 };
        const uint8_t src0 = 90;

        for (uint8_t i = 0; i < 4; i++) {
            AudioEvt e = {};
            e.type     = EV_NOTE_ON;
            e.src      = src0 + i;
            e.inst     = 1;
            e.count    = 1;
            e.pitch[0] = notes[i];
            if (evtQ) xQueueSend(evtQ, &e, 0);
            delay(170);
        }
        delay(260);
        for (uint8_t i = 0; i < 4; i++) sendOff(src0 + i);
    }

    // Play the chord on a pad, tagging the voices with the pad id.
    void playPad(uint8_t padId, const SynthState& synth) { playPad(padId, synth, padId); }

    // Same, but tags the voices with `src` so stopNote(src) releases only them.
    void playPad(uint8_t padId, const SynthState& synth, uint8_t src) {
        if (padId >= 12 || PADS[padId].role != ROLE_CHORD) return;

        NoteStyle style = synth.getNoteStyle();
        // Latch belongs to LIVE pads only (src 0-11). Looped chords (src 12+) always follow their
        // own note-on/note-off, so loop playback can neither release a latched live chord nor
        // be latched itself.
        bool isLive = (src < LOOP_VOICE_OFFSET);
        if (isLive) {
            if (style == STYLE_LATCH) { releaseLatched(); latchMask |= (1u << src); }
            else if (latchMask)       { releaseLatched(); }
        }

        const Scale& sc = synth.getScale();
        float tonic = 60.0f + (float)synth.getKeyRoot() + (float)(synth.getOctaveOffset() * 12);
        int degree  = PADS[padId].arg;             // 0 = I chord ... 6 = vii chord

        AudioEvt e = {};
        e.type    = EV_NOTE_ON;
        e.src     = src;
        e.inst    = (uint8_t)synth.getInstrumentIndex();
        e.count   = 4;
        e.flags   = EVF_BASS_FIRST;
#if LITE_LOOP_VOICES
        if (src >= LOOP_VOICE_OFFSET && src < 32) e.flags |= EVF_LITE;
#endif
        e.strumMs = (uint16_t)synth.getStrumMs();
        e.gateMs  = (style == STYLE_FIXED) ? (uint16_t)synth.getGateMs() : 0;

        // Listed bottom to top: bass (root, one octave down), root, third, fifth.
        e.pitch[0] = scaleDegreePitch(sc, degree,     tonic) - 12.0f;
        e.pitch[1] = scaleDegreePitch(sc, degree,     tonic);
        e.pitch[2] = scaleDegreePitch(sc, degree + 2, tonic);
        e.pitch[3] = scaleDegreePitch(sc, degree + 4, tonic);

        if (evtQ && xQueueSend(evtQ, &e, 0) != pdTRUE) statQueueFull = statQueueFull + 1;
    }

    // Releases every voice started with this source. In latch mode a pad release is ignored:
    // the chord is released by the next chord instead.
    void stopNote(uint8_t src) {
        if (src < LOOP_VOICE_OFFSET && (latchMask & (1u << src))) return;
        sendOff(src);
    }
    void stopNote(uint8_t src, const SynthState& /*synth*/) { stopNote(src); }   // v1-compatible signature

    // --- Diagnostics ---
    void setVolume(float v) { globalVolume = constrain(v, 0.0f, 1.0f); }

    // Print and reset the audio health counters. Call from loop() every couple of seconds.
    void printStats() {
        uint32_t blocks = statBlocks;
        if (blocks == 0) return;
        const float budget = (float)AUDIO_FRAMES * 1000000.0f / (float)SAMPLE_RATE;
        float avgPct = 100.0f * ((float)statSumUs / (float)blocks) / budget;
        float maxPct = 100.0f * (float)statMaxUs / budget;
        Serial.printf("Audio: CPU avg %.0f%% max %.0f%% | peak voices %u/%d | over-budget blocks %u | stolen %u dropped %u queue-full %u\n",
                      avgPct, maxPct, (unsigned)statPeakVoices, MAX_VOICES, (unsigned)statOver,
                      (unsigned)statStolen, (unsigned)statDropped, (unsigned)statQueueFull);
        statBlocks = 0; statSumUs = 0; statMaxUs = 0; statOver = 0; statPeakVoices = 0;
        statStolen = 0; statDropped = 0; statQueueFull = 0;
    }

    // Debug helpers (used by the host tests; harmless on hardware).
    int activeVoices() const {
        int n = 0;
        for (int i = 0; i < MAX_VOICES; i++) if (voices[i].active) n++;
        return n;
    }
    // Voices owned by `src`. With soundingOnly, strummed voices that have not started yet are skipped.
    int voicesForSrc(uint8_t src, bool soundingOnly = false) const {
        int n = 0;
        for (int i = 0; i < MAX_VOICES; i++)
            if (voices[i].active && voices[i].src == src && (!soundingOnly || voices[i].delay <= 0)) n++;
        return n;
    }
};
