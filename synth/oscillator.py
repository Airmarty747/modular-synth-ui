# synth/oscillator.py

import numpy as np

def generate_wave(freq, duration, sample_rate=44100, waveform="sine"):
    t = np.linspace(0, duration, int(sample_rate * duration), endpoint=False)

    if waveform == "sine":
        return np.sin(2 * np.pi * freq * t)
    elif waveform == "square":
        return np.sign(np.sin(2 * np.pi * freq * t))
    elif waveform == "sawtooth":
        return 2 * (t * freq - np.floor(0.5 + t * freq))
    else:
        raise ValueError(f"Unsupported waveform: {waveform}")

def generate_chord_wave(frequencies, duration, sample_rate=44100, waveform="sine"):
    wave = sum(generate_wave(freq, duration, sample_rate, waveform) for freq in frequencies)
    wave /= len(frequencies)
    return wave

