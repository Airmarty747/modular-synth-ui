#pragma once
#include <Arduino.h>

class SynthState {
private:
    int keyRoot;       // 0 = C, 1 = C#, ..., 11 = B
    String scaleName;  // "major" or "minor"
    int octaveOffset;  // Range: -2 to +2
    int instrumentIdx; // Range: 0 to 5 (maps to your instruments array)
    int bpm;           // Range: 50 to 200
    float articulation; // Ranges from 0.1 (ultra-staccato) to 1.0 (full legato)

    // Musical maps for translating 16 buttons into 2 octaves of a scale
    const int majorIntervals[16] = {
        0, 2, 4, 5, 7, 9, 11, 12,        // Octave 1
        14, 16, 17, 19, 21, 23, 24, 26   // Octave 2
    };
    
    const int minorIntervals[16] = {
        0, 2, 3, 5, 7, 8, 10, 12,        // Octave 1
        14, 15, 17, 19, 20, 22, 24, 26   // Octave 2
    };

public:
    SynthState() {
        keyRoot = 0;          // Default to C
        scaleName = "major";  // Default to Major scale
        octaveOffset = 0;     // Default center octave
        instrumentIdx = 0;    // Default to Warm Pad
        bpm = 100;            // Default tempo
        articulation = 0.8f;  // Default articulation
    }

    // --- Audio Triggers (Called by InputManager) ---

    void noteOn(int buttonId) {
        if (buttonId < 0 || buttonId > 15) return;

        // Base MIDI note (60 = Middle C). Add keyRoot and octave modifier.
        int baseMidi = 60 + keyRoot + (octaveOffset * 12);
        
        // Add the scale interval based on the current scale choice
        int interval = (scaleName == "minor") ? minorIntervals[buttonId] : majorIntervals[buttonId];
        int finalMidiNote = baseMidi + interval;
        
        Serial.printf("SynthState: Note ON -> MIDI %d (Button %d)\n", finalMidiNote, buttonId);
        
        // TODO: Send finalMidiNote to audio generator
    }

    void noteOff(int buttonId) {
        if (buttonId < 0 || buttonId > 15) return;

        int baseMidi = 60 + keyRoot + (octaveOffset * 12);
        int interval = (scaleName == "minor") ? minorIntervals[buttonId] : majorIntervals[buttonId];
        int finalMidiNote = baseMidi + interval;
        
        Serial.printf("SynthState: Note OFF -> MIDI %d (Button %d)\n", finalMidiNote, buttonId);
        
        // TODO: Send note off command to audio generator
    }

    // --- State Modifiers ---

    void shiftOctave(int amount) {
        octaveOffset = constrain(octaveOffset + amount, -2, 2);
        Serial.printf("SynthState: Octave offset adjusted to %d\n", octaveOffset);
    }

    void setKey(int newKey) {
        keyRoot = ((newKey % 12) + 12) % 12; // Wrap safely within 0-11
        Serial.printf("SynthState: Key root set to index %d\n", keyRoot);
    }

    void setScale(String newScale) {
        if (newScale == "major" || newScale == "minor") {
            scaleName = newScale;
            Serial.println("SynthState: Scale updated to " + scaleName);
        }
    }

    void setInstrument(int newIdx) {
        instrumentIdx = constrain(newIdx, 0, 5);
        Serial.printf("SynthState: Instrument index set to %d\n", instrumentIdx);
    }

    void setBpm(int newBpm) {
        bpm = constrain(newBpm, 50, 200);
        Serial.printf("SynthState: BPM set to %d\n", bpm);
    }
    
    void setArticulation(float val) {
        articulation = constrain(val, 0.1f, 1.0f);
        Serial.printf("SynthState: Articulation set to %.2f\n", articulation);
    }

    // --- State Accessors (Getters) ---
    int getKeyRoot() const { return keyRoot; }
    String getScaleName() const { return scaleName; }
    int getOctaveOffset() const { return octaveOffset; }
    int getInstrumentIndex() const { return instrumentIdx; }
    int getBpm() const { return bpm; }
    float getArticulation() const { return articulation; }
};
