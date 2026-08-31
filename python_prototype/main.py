from synth.chords import CHORDS
from synth.oscillator import generate_chord_wave
from synth.envelope import apply_adsr
from synth.output import play_wave
from synth.output import stop_wave
from ui import launch_ui

def handle_button_release(chord_name):
    stop_wave()

current_waveform = "sine"

# In main.py
precomputed = {}

# def handle_chord_press(chord_name):
#     global precomputed
#     if chord_name not in precomputed:
#         freqs = CHORDS.get(chord_name)
#         if freqs:
#             wave = generate_chord_wave(freqs, duration=1.5, waveform=current_waveform)
#             shaped_wave = apply_adsr(wave, sample_rate=44100,
#                                      attack=0.05, decay=0.1, sustain_level=0.6, release=0.3)
#             precomputed[chord_name] = shaped_wave
#     play_wave(precomputed[chord_name])
# 7
#
# def handle_waveform_change(new_waveform):
#     global current_waveform
#     current_waveform = new_waveform
#     print(f"Waveform changed to: {new_waveform}")
#
# launch_ui(handle_chord_press, handle_waveform_change)
# main.py
shared_state = {"waveform": "sine"}

# def handle_chord_press(chord_name):
#     freqs = CHORDS.get(chord_name)
#     if freqs:
#         wave = generate_chord_wave(freqs, duration=1.5, waveform=shared_state["waveform"])
#         shaped_wave = apply_adsr(wave, sample_rate=44100,
#                                  attack=0.05, decay=0.1, sustain_level=0.6, release=0.3)
#         play_wave(shaped_wave)
def handle_chord_press(chord_name):
    try:
        freqs = CHORDS.get(chord_name)
        if freqs:
            wave = generate_chord_wave(freqs, duration=1.5, waveform=shared_state["waveform"])
            shaped_wave = apply_adsr(wave, sample_rate=44100,
                                     attack=0.05, decay=0.1, sustain_level=0.6, release=0.3)
            play_wave(shaped_wave)
        else:
            print(f"Chord not found: {chord_name}")
    except Exception as e:
        print(f"Error playing chord {chord_name}: {e}")

def handle_waveform_change(new_waveform):
    shared_state["waveform"] = new_waveform
    print(f"Waveform changed to: {new_waveform}")

launch_ui(handle_chord_press, handle_waveform_change, handle_button_release)
