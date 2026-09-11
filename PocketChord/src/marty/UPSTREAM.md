# These are copies — do not edit them here

Every `.h` in this directory is a **byte-for-byte copy of `Modular Synth/include/`**
in this same repository, taken at commit **`3cb1acd`**.

**The canonical copy is `Modular Synth/include/`. Edit there, never here.**

## The pin is deliberate — expect drift to be reported

`3cb1acd` is not the newest commit. It is the newest one this sketch has been
**flashed onto real hardware and heard working with**. Two files have changed
upstream since:

| file | changed since `3cb1acd` |
|---|---|
| `SynthState.h` | `noteOn()` / `noteOff()` added, articulation default, 16-button interval tables |
| `InputManager.h` | `scanHardware()` signature, OLED menu fields |

So `./sync-upstream.sh` will report exactly those two as **drifted**. That is
correct, not a fault.

The sketch builds clean against the newer versions — that was checked — but it
was never run on a board afterwards, and "compiles" is not "works". Whoever has
the hardware should refresh and confirm:

```
./sync-upstream.sh            # show what has drifted
./sync-upstream.sh --write    # take the newer files
```

then build, flash, and play a few chords. If it behaves, update the commit at
the top of this file and the pin moves forward. Nothing in the sketch calls
`noteOn`/`noteOff` or `InputManager`, so it is expected to be uneventful.

## Why there are two copies at all

The Arduino IDE will only compile sources that live inside the sketch folder or
its `src/` subfolder. It cannot reach up and across into `Modular Synth/include/`,
and a relative include that tried would break on the space in the folder name.

Keeping copies here is what makes the sketch **download-and-build**: open
`PocketChord.ino`, press Upload, done. The cost is this duplication, and
`sync-upstream.sh` exists to make the drift visible rather than silent.

## What the sketch uses

| file | used? |
|---|---|
| `SynthState.h` | yes — the authoritative key / scale / octave / instrument / bpm |
| `Looper.h` | yes — records what gets played |
| `IStorage.h`, `DummyMemory.h` | yes — the storage seam |
| `ShareManager.h` | yes — `begin()` is called; inert while its `Serial1` lines are commented out |
| `page.h` | yes — served verbatim as the browser GUI |
| `InputManager.h` | **no** — see below |

### Why InputManager.h is copied but never included

It cannot run on this particular board. Nothing is wrong with it — the two just
assume completely different hardware:

- rows `{18,19,20,21}` — **21 is this board's I2S bit clock**, and 19/20 are the
  USB D−/D+ pins
- cols `{22,23,14,6}` — **6 is this board's I2S word clock**, and **GPIO 22 and 23
  do not exist on an ESP32-S3** at all; the numbering jumps 21 → 26
- joystick `VRx=2, VRy=3` — GPIO 2 is this board's VRy
- it scans a 4×4 matrix of 16 keys; this board has 12 MPR121 capacitive pads

`padService()` in `PocketChord.ino` replaces it. It reports the same button IDs
in the same WebSocket format and drives the same `SynthState` and `Looper`
methods, so nothing downstream can tell which one is running. The file is copied
anyway so this directory stays an honest mirror.
