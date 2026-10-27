#pragma once
// SynthState.h -- v2
//
// Changes from v1:
//  * Scale is an index into SCALES[] (Scale.h) instead of a String, so there is
//    no heap traffic and any number of scales / tunings can exist.
//  * New "tradition" setting (Western / Indian) that decides which scales the
//    scale controls cycle through.
//  * New note style (gated / fixed length / latched) and strum time.
//  * A revision counter that goes up on every change, so the display can redraw
//    only when something actually changed.
//  * Drop-in compatible with the old InputManager: setScale("major") and
//    setScale("minor") still work (they look the name up in SCALES[]).

#include <Arduino.h>
#include "Scale.h"
#include "Instruments.h"

enum NoteStyle : uint8_t {
    STYLE_GATED = 0,   // chord sounds while the pad is held, then releases
    STYLE_FIXED = 1,   // chord sounds for a fixed time set by articulation, however long the pad is held
    STYLE_LATCH = 2,   // chord keeps sounding after release until the next chord is played
    STYLE_COUNT = 3
};

class SynthState {
private:
    int      keyRoot;        // 0 = C ... 11 = B
    int      scaleIdx;       // index into SCALES[]
    int      octaveOffset;   // -2 .. +2
    int      instrumentIdx;  // 0 .. NUM_INSTRUMENTS-1
    int      bpm;            // 50 .. 200 (drives the sequencer once it exists)
    float    articulation;   // 0.1 .. 1.0; sets the note length in STYLE_FIXED
    uint8_t  noteStyle;
    uint8_t  strumIdx;       // 0 = block chord; higher = slower strum, bottom note to top
    uint32_t revision;

    static int strumMsFor(int idx) {
        switch (idx) { case 1: return 25; case 2: return 50; case 3: return 90; default: return 0; }
    }
    void touch() { revision++; }

public:
    SynthState()
        : keyRoot(0), scaleIdx(0), octaveOffset(0), instrumentIdx(0),
          bpm(100), articulation(1.0f), noteStyle(STYLE_GATED), strumIdx(0), revision(1) {}

    // --- Modifiers ---
    void shiftOctave(int amount) {
        octaveOffset = constrain(octaveOffset + amount, -2, 2);
        touch();
        Serial.printf("SynthState: Octave offset adjusted to %d\n", octaveOffset);
    }

    void setKey(int newKey) {
        keyRoot = ((newKey % 12) + 12) % 12;
        touch();
        Serial.printf("SynthState: Key root set to index %d\n", keyRoot);
    }

    // Pick a scale by index. Also switches the tradition to match that scale.
    void setScaleIndex(int idx) {
        if (idx < 0 || idx >= NUM_SCALES) return;
        scaleIdx = idx;
        touch();
        Serial.printf("SynthState: Scale set to %s (%s)\n", SCALES[scaleIdx].name, traditionName(SCALES[scaleIdx].trad));
    }

    // Old-style call from the previous InputManager: setScale("major") / setScale("minor").
    void setScale(const char* name) {
        int idx = scaleFindByName(name);
        if (idx >= 0) setScaleIndex(idx);
    }

    // Next / previous scale inside the current tradition.
    void cycleScale(int dir) { setScaleIndex(scaleStep(scaleIdx, dir)); }

    // Switch between Western and Indian. Jumps to the first scale of the new tradition.
    void setTradition(Tradition t) {
        if (t == SCALES[scaleIdx].trad) return;
        setScaleIndex(scaleFirstOf(t));
    }
    void toggleTradition() {
        setTradition(SCALES[scaleIdx].trad == TRAD_WESTERN ? TRAD_INDIAN : TRAD_WESTERN);
    }

    void setInstrument(int newIdx) {
        instrumentIdx = constrain(newIdx, 0, NUM_INSTRUMENTS - 1);
        touch();
        Serial.printf("SynthState: Instrument set to %s\n", instrumentName(instrumentIdx));
    }
    void cycleInstrument(int dir) {
        setInstrument((instrumentIdx + (dir >= 0 ? 1 : NUM_INSTRUMENTS - 1)) % NUM_INSTRUMENTS);
    }

    void setBpm(int newBpm) {
        bpm = constrain(newBpm, 50, 200);
        touch();
        Serial.printf("SynthState: BPM set to %d\n", bpm);
    }

    void setArticulation(float val) {
        articulation = constrain(val, 0.1f, 1.0f);
        touch();
        Serial.printf("SynthState: Articulation set to %.2f\n", articulation);
    }
    // Steps of 0.1 (wraps from 1.0 back to 0.1 going up).
    void cycleArticulation(int dir) {
        float v = articulation + (dir >= 0 ? 0.1f : -0.1f);
        if (v > 1.001f) v = 0.1f;
        if (v < 0.099f) v = 1.0f;
        setArticulation(v);
    }

    void setNoteStyle(NoteStyle s) {
        noteStyle = (uint8_t)s;
        touch();
        Serial.printf("SynthState: Note style set to %s\n", getNoteStyleName());
    }
    void cycleNoteStyle() { setNoteStyle((NoteStyle)((noteStyle + 1) % STYLE_COUNT)); }

    void cycleStrum() {
        strumIdx = (strumIdx + 1) % 4;
        touch();
        Serial.printf("SynthState: Strum set to %d ms\n", strumMsFor(strumIdx));
    }

    // --- Accessors ---
    int          getKeyRoot() const { return keyRoot; }
    int          getScaleIndex() const { return scaleIdx; }
    const Scale& getScale() const { return SCALES[scaleIdx]; }
    const char*  getScaleName() const { return SCALES[scaleIdx].name; }   // NOTE: const char*, compare with strcmp
    Tradition    getTradition() const { return SCALES[scaleIdx].trad; }
    int          getOctaveOffset() const { return octaveOffset; }
    int          getInstrumentIndex() const { return instrumentIdx; }
    const char*  getInstrumentName() const { return instrumentName(instrumentIdx); }
    int          getBpm() const { return bpm; }
    float        getArticulation() const { return articulation; }
    NoteStyle    getNoteStyle() const { return (NoteStyle)noteStyle; }
    const char*  getNoteStyleName() const {
        switch (noteStyle) { case STYLE_FIXED: return "Fixed"; case STYLE_LATCH: return "Latch"; default: return "Gated"; }
    }
    int          getStrumMs() const { return strumMsFor(strumIdx); }
    // Note length used by STYLE_FIXED: 190 ms at articulation 0.1, 1000 ms at 1.0.
    int          getGateMs() const { return 100 + (int)(articulation * 900.0f); }
    uint32_t     getRevision() const { return revision; }
};
