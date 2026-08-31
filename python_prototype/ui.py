# ui.py
import tkinter as tk

def launch_ui(on_button_press, on_waveform_change, on_button_release):
    root = tk.Tk()
    root.title("HiChord Synth UI")

    chords = [
        ["Cmaj", "Dmin", "Emin"],
        ["Fmaj", "Gmaj", "Amin"],
        ["Bdim", "C7", "D7"]
    ]

    key_map = {
        "1": "Cmaj",
        "2": "Dmin",
        "3": "Emin",
        "4": "Fmaj",
        "5": "Gmaj",
        "6": "Amin",
        "7": "Bdim",
        "8": "C7",
        "9": "D7"
    }

    waveform_var = tk.StringVar(value="sine")
    waveform_order = ["sine", "square", "sawtooth"]

    def toggle_waveform(direction):
        current = waveform_var.get()
        idx = waveform_order.index(current)
        if direction == "right":
            idx = (idx + 1) % len(waveform_order)
        elif direction == "left":
            idx = (idx - 1) % len(waveform_order)
        new_wave = waveform_order[idx]
        waveform_var.set(new_wave)
        on_waveform_change(new_wave)
        waveform_btn.config(text=f"Waveform: {new_wave}")

    waveform_btn = tk.Button(root, text="Waveform: sine", command=lambda: toggle_waveform("right"))
    waveform_btn.grid(row=3, column=0, columnspan=3, pady=10)

    # Create chord buttons
    for row_index, row in enumerate(chords):
        for col_index, chord in enumerate(row):
            btn = tk.Button(root, text=chord, width=10, height=3,
                            command=lambda c=chord: on_button_press(c))
            btn.grid(row=row_index, column=col_index, padx=5, pady=5)

    # Handle key presses
    pressed_keys = set()

    def key_down(event):
        key = event.char
        if key in key_map:
            chord = key_map[key]
            if chord not in pressed_keys:
                pressed_keys.add(chord)
                on_button_press(chord)

    def key_up(event):
        key = event.char
        if key in key_map:
            chord = key_map[key]
            pressed_keys.discard(chord)
            on_button_release(chord)

    def arrow_handler(event):
        if event.keysym == "Right":
            toggle_waveform("right")
        elif event.keysym == "Left":
            toggle_waveform("left")

    root.bind("<KeyPress>", key_down)
    root.bind("<KeyRelease>", key_up)
    root.bind("<Left>", arrow_handler)
    root.bind("<Right>", arrow_handler)

    root.mainloop()
