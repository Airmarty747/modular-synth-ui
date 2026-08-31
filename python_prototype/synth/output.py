# synth/output.py
import sounddevice as sd
import threading

def play_wave(wave, sample_rate=44100):
    threading.Thread(target=lambda: sd.play(wave, samplerate=sample_rate)).start()

def stop_wave():
    sd.stop()
