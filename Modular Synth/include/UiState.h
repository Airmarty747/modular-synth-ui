#pragma once
// UiState.h -- what the display needs to know about the controls (not part of the music state).

#include <stdint.h>

struct UiState {
    bool shift        = false;   // left bumper held
    bool reharmonize  = false;   // right bumper held: the loop follows the live key / scale / instrument
    bool loopHas      = false;   // a loop has a recorded snapshot
    int  loopKey      = 0;       // key the loop was recorded in (0 = C)
    int  loopScale    = 0;       // index into SCALES[] of the loop's scale
};
