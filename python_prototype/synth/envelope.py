# synth/envelope.py
import numpy as np

def apply_adsr(wave, sample_rate, attack=0.01, decay=0.1, sustain_level=0.7, release=0.2):
    total_samples = len(wave)
    attack_samples = int(sample_rate * attack)
    decay_samples = int(sample_rate * decay)
    release_samples = int(sample_rate * release)
    sustain_samples = total_samples - (attack_samples + decay_samples + release_samples)

    # Create envelope curve
    envelope = np.concatenate([
        np.linspace(0, 1, attack_samples),  # Attack
        np.linspace(1, sustain_level, decay_samples),  # Decay
        np.full(sustain_samples, sustain_level),  # Sustain
        np.linspace(sustain_level, 0, release_samples)  # Release
    ])

    # Match envelope length to waveform
    envelope = np.pad(envelope, (0, total_samples - len(envelope)), mode='constant')
    shaped_wave = wave * envelope
    return shaped_wave
