
/*
 * Buttonbox -- a standalone pocket instrument on an ESP32-S3.
 *
 * This is the prototype.html rig moved onto the hardware: same music engine,
 * same seven-notes-always-in-key idea, same brightness ladder, same loop
 * recorder -- but synthesised on the board and played out of the board's own
 * speaker. Nothing to connect to. Power it and play.
 *
 * ---------------------------------------------------------------- wiring ---
 * I2C devices share ONE bus:
 *   SDA -> GPIO 40     SCL -> GPIO 39     VCC -> 3V3      GND -> GND
 *   MPR121 = 0x5A      OLED = 0x3C
 *
 * Audio: ONE I2S bus, shared by both output boards --
 *   BCLK -> GPIO 21  LRC -> GPIO 6   DIN -> GPIO 42  VIN -> 3V3   GND -> GND
 *   MAX98357A amp (speaker):  SD   -> GPIO 47
 *   Give the amp its OWN 3V3 feed, not the rail the I2C boards share -- see
 *   the README. Grounds still common. This matters more than it did for DTMF:
 *   a full mix with drums draws far more current than one sine pair, and a
 *   shared rail sagging on every kick is heard as the whole mix ducking.
 *   PCM5102 DAC (headphones): its SCK MUST go to GND. XSMT is optional.
 *
 * KCX_BT_EMITTER Bluetooth transmitter, fed from the PCM5102's analogue out
 * (the L / G / R holes on its bottom edge -- NEVER the amp's speaker
 * terminals, which are bridge-tied PWM with no ground):
 *   +5V -> 5Vin   PGND -> GND   IN_L -> PCM L   IN_R -> PCM R   AGND -> PCM G
 *   5Vin only outputs USB power because the IN-OUT and USB-OTG solder jumpers
 *   are bridged. So: power from the USB port, ONE cable at a time, and never
 *   feed 5Vin from elsewhere while USB is plugged in.
 *   No code involved -- Output = headphones/both reaches it. Expect ~0.2 s of
 *   Bluetooth latency.
 *
 * KY-023 dual-axis joystick (3.3V ONLY -- 5V would exceed the ADC input):
 *   VRx -> GPIO 1      VRy -> GPIO 2      SW -> GPIO 48 (internal pull-up)
 *
 * Two push buttons, wired GPIO -> button -> GND (internal pull-ups, no
 * resistors):
 *   SHIFT -> GPIO 41     SETTINGS -> GPIO 7
 *
 * ------------------------------------------------------------ the layout ---
 * Everything below is by ELECTRODE NUMBER, not by whatever is printed on the
 * pad, because on this box the two do not line up. The seven notes run low to
 * high across electrodes 8, 5, 9, 6, 10, 7, 11 -- that zigzag is the physical
 * order of the pads as they are actually wired, so playing left to right up
 * the box plays the scale in order.
 *
 *   e8  e5  e9  e6  e10 e7  e11     the seven notes of the key, low to high.
 *                                   Two are "spice" notes -- outside the
 *                                   pentatonic. They clash on purpose, and
 *                                   used well they are the most interesting
 *                                   notes on the board.
 *
 * There is ONE held modifier and ONE menu button, and that is the whole
 * control scheme. The old LOCK button is gone: everything it did now lives on
 * the grid or on the stick, under the same SHIFT you already hold.
 *
 *   SHIFT     (GPIO 41)  hold: second job for the pad or stick you touch
 *   SETTINGS  (GPIO 7)   tap: open the menu. Tap again to close.
 *
 * SETTINGS is a real menu on the screen, not a combo to memorise. The stick
 * drives it -- up/down picks a line, left/right changes it -- and the box goes
 * on playing while it is open, so you hear every change as you make it:
 *
 *     Volume      quiet .. loud
 *     Theme       Sunrise / Lo-fi / Nightdrive / Deep house / Wide open
 *     Tempo       60 .. 150 bpm
 *     Output      speaker / headphones / both
 *
 * Row 1 is five buttons AND a scroll strip. Tap a pad for its job, or drag a
 * finger along the row like a wheel:
 *
 *            tap                SHIFT + tap
 *     e0     darker             previous theme
 *     e1     brighter           next theme
 *     e2     hold: energy       lock energy where it is
 *     e3     record 4 bars      reset everything
 *     e4     stop / start       --
 *
 *     drag along row 1          volume
 *     SHIFT + drag              tempo
 *
 * Note pads:
 *   SHIFT + a note   that note one octave up, only while you hold it
 *   hold a note      pedal the bass on that note; hold it again to let go
 *
 * Holding a note is free real estate: a note pad only ever acted on the press,
 * so nothing else was using the hold. That is why the bass pedal ended up
 * there rather than behind a second button -- it is on the grid, where the
 * note it pedals is.
 *
 * Joystick:
 *   up / down        base octave, -2..+2, and it STAYS where you put it
 *   left / right     darker / brighter, and these repeat if you hold them
 *   SHIFT + up/down  freeze the octave so it cannot drift; again to free it
 *   button tap       arm / cancel the four-bar loop recorder
 *   button hold      reset everything: loops, chords, octave, energy
 *   (in the menu)    up/down picks a line, left/right changes it, tap closes
 *
 * The octave has two controls on purpose: the stick sets where you are and
 * leaves it there, SHIFT reaches up one octave only while you hold it. That is
 * the difference between changing register and leaning on one note.
 *
 * Darker and brighter are the whole point: they do not pick the next chord in
 * the loop, they edit the bar that is playing, permanently. Go round a few
 * times and the progression is yours.
 *
 * Library needed (Tools -> Manage Libraries):
 *   "U8g2" by oliver
 * The MPR121 is driven by raw Wire calls here -- no Adafruit library, because
 * that path returned garbage on this setup while raw reads are rock solid.
 *
 * Board: ESP32S3 Dev Module,  USB CDC On Boot = Enabled,  115200 baud
 */


#include <Wire.h>
#include <U8g2lib.h>
#include <math.h>
#include <ctype.h>




// ============================================================ HARDWARE ======


static const int PIN_SDA = 40;
static const int PIN_SCL = 39;
static const uint8_t MPR_ADDR  = 0x5A;
static const uint8_t OLED_ADDR = 0x3C;   // try 0x3D if the scanner says so


static const uint8_t TOUCH_TH   = 12;    // your margin was ~233 counts, so this
static const uint8_t RELEASE_TH = 6;     // is already wildly comfortable


// I2S pins. 21/6/42 are free on the S3 -- not strapping pins, not USB (19/20),
// not flash/PSRAM (26-37), and clear of the I2C pair on 40/39.
static const int PIN_I2S_BCLK = 21;
static const int PIN_I2S_LRC  = 6;
static const int PIN_I2S_DIN  = 42;


// The onboard addressable LED. setup() sends it an all-zero frame and then
// parks the line -- see the comment there for why both steps are needed.
// This board is a DevKitC-1 v1.1, where that LED is on GPIO 38 -- v1.0 boards
// use 48. The pin has to be free for this to work, so the stick button moved
// off 38 and onto 48, which on a v1.1 board is an ordinary GPIO.
static const int PIN_LED_ONBOARD = 38;


// Both output boards hang off those same three wires -- I2S is a broadcast bus,
// so the amp and the DAC each get their own enable line instead.
static const int PIN_AMP_SD   = 47;   // MAX98357A SD:   LOW = shut down
// PCM5102 XSMT is a solder pad on the back of the module, not a header pin, so
// it is optional here. Leave this at -1 and the DAC is simply always unmuted.
static const int PIN_DAC_XSMT = -1;


// KY-023 joystick. VRx/VRy must land on ADC1 (GPIO 1-10 on the S3) -- ADC2 is
// unusable while WiFi is up. SW is a plain digital input, so it sits on 48.
static const int PIN_JOY_X  = 1;
static const int PIN_JOY_Y  = 2;
static const int PIN_JOY_SW = 48;    // shorts to GND when pressed


// The stick never centres on exactly half scale, so the centre is measured at
// boot instead of assumed. Everything below is relative to that reading.
static const int JOY_DEADZONE = 500;           // of ~2048 counts of travel/side
static const uint16_t JOY_REPEAT_DELAY = 350;  // hold-to-repeat: first repeat
static const uint16_t JOY_REPEAT_RATE  = 150;  // ...and every one after
// A diagonal push never crosses both deadzones on the same poll, so wait this
// long for the direction to stop changing before acting on it.
static const uint16_t JOY_SETTLE_MS = 45;
// Flip these if a push feels backwards -- module orientation decides the sign.
// The module is mounted rotated 180 degrees, which reverses both axes at once.
static const bool JOY_INVERT_X = true;
static const bool JOY_INVERT_Y = true;


static const int PIN_BTN_SHIFT = 41;  // held modifier: second job for every pad
static const int PIN_BTN_SET   = 7;   // tap: open / close the settings menu


// The buttons are debounced with an integrator rather than a timer. Sampled on
// a fixed cadence, each pressed read counts the integrator up and each released
// read counts it down, so it takes BTN_INTEGRATOR * BTN_SAMPLE_MS of *net*
// level to move the debounced state -- 24 ms here.
static const uint16_t BTN_SAMPLE_MS  = 2;
static const uint8_t  BTN_INTEGRATOR = 12;


// false = momentary push buttons: only the press acts, the release does not.
// true  = latching switches (toggle / slide / rocker): every flip is one action.
// Get this wrong and it costs an afternoon -- a toggle stays made, so firing
// only on the press edge gives one action per *two* flips, which looks exactly
// like a push button dropping every other press.
static const bool BTN_LATCHING = false;


// Which level means "pressed".  -1 = detect at boot, 0 = active LOW, 1 = HIGH.
// Forced to 0: a plain button to GND, held high by the internal pull-up.
static const int BTN_ACTIVE_LEVEL = 0;


// 1 = log every raw level change on the button pins. NOT free: each printf
// blocks inside the sample loop, so turning it off changes the sampling cadence
// during contact chatter and with it the debounced behaviour. Off by default
// now that there is music to listen to instead of a serial log to read.
#define BTN_DEBUG 0


static const uint16_t HOLD_MS = 700;   // stick button hold = reset everything


// How long a note pad has to stay down before it pedals the bass on that
// degree. Long enough that no amount of ordinary playing reaches it -- a note
// is a pluck, your finger is off it in well under 200 ms -- and short enough
// that "press and lean on it" is one gesture rather than a wait.
static const uint16_t PEDAL_HOLD_MS = 600;


// The physical layout this box is actually wired in -- three staggered rows,
// which is why the note order zigzags:
//
//   row 1   e0  e1  e2  e3  e4          the control strip
//   row 2     e5  e6  e7                 offset half a pad, like black keys
//   row 3   e8  e9  e10 e11
//
// Reading rows 3 and 2 alternately left to right gives e8 e5 e9 e6 e10 e7 e11,
// and that is the scale, low to high.


// Which scale degree each electrode plays, or -1 if it is a control pad.
// Electrodes 5..11 are the notes; the order is the physical left-to-right,
// bottom-to-top order of the pads on this box, which is NOT the electrode
// order and NOT the phone-keypad order.
//
//   degree  0   1   2   3   4   5   6      (low to high)
//   pad    e8  e5  e9  e6  e10 e7  e11
static const int8_t PAD_DEGREE[12] = {
  -1, -1, -1, -1, -1,        // e0..e4 are controls
   1,                        // e5
   3,                        // e6
   5,                        // e7
   0,                        // e8
   2,                        // e9
   4,                        // e10
   6                         // e11
};
// The same thing the other way round, for the display: which pad plays each
// degree. Keep the two in step if you re-wire the box.
static const uint8_t DEGREE_PAD[7] = { 8, 5, 9, 6, 10, 7, 11 };


// Row 1 is both five buttons and one scroll strip. STRIP lists the electrodes
// in physical left-to-right order, and that order is the only thing the swipe
// needs to know -- if a drag runs backwards, reverse this array and nothing
// else changes.
static const uint8_t STRIP[5]  = { 0, 1, 2, 3, 4 };
static const uint8_t STRIP_N   = 5;


static const uint8_t E_DARK    = 0;
static const uint8_t E_BRIGHT  = 1;
static const uint8_t E_ENERGY  = 2;
static const uint8_t E_REC     = 3;
static const uint8_t E_RUN     = 4;


// (stripIndex() lives down in the TOUCH section: no function may be defined
// above the TYPES block, or the auto-generated prototypes land above it too.)


// A gesture ends once the whole strip has been clear this long. Long enough to
// bridge the gap as a finger crosses between two pads, short enough that two
// deliberate taps never merge into one drag.
static const uint16_t STRIP_GAP_MS = 220;
static const int      BPM_PER_STEP = 4;      // one pad of travel, SHIFT held
static const float    BPM_MIN = 60.0f, BPM_MAX = 150.0f;


// The MPR121 reports how far an electrode has deflected, not just that it
// crossed the threshold, and a firm press deflects further than a brush. That
// is close enough to pressure to drive the accent the web prototype could only
// fake -- a hard press gets the fuller sound and a harmony note underneath.
// Deflection is in the MPR121's own counts, and how many counts a firm press
// makes depends on your pad size and overlay thickness -- on this build a
// brush reads 60-90 and a deliberate press 150+. Set TOUCH_DEBUG to 1 and the
// serial log prints the deflection of every press so you can pick your own
// number; the default is chosen to sit above an ordinary playing touch.
static const bool PRESSURE_ACCENT = true;
static const int  ACCENT_COUNTS   = 130;
#define TOUCH_DEBUG 0


// SSD1309 is SSD1306-compatible enough that this constructor drives it.
// "F_" = full frame buffer (needs ~1 KB RAM, trivial on an S3, no tearing).
U8G2_SSD1309_128X64_NONAME0_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);


// ---------------------------------------------------- MPR121 registers -----
#define R_TOUCH_STATUS 0x00
#define R_FILTERED     0x04
#define R_BASELINE     0x1E
#define R_TOUCH_TH(e)  (0x41 + (e) * 2)
#define R_REL_TH(e)    (0x42 + (e) * 2)
#define R_DEBOUNCE     0x5B
#define R_CONFIG1      0x5C
#define R_CONFIG2      0x5D
#define R_ECR          0x5E
#define R_SOFTRESET    0x80


// ======================================================== AUDIO PLUMBING ====


// 32 kHz, not the 16 k the DTMF sketch used. Hi-hats live at 7-8 kHz and 16 k
// puts Nyquist right on top of them -- the difference between a hat and a dull
// tick. The S3 has the headroom; everything below costs well under half a core.
static const uint32_t SR = 32000;


// One I2S write is 256 frames = 8 ms. The engine renders it in BLOCK-sized
// chunks so the sequencer gets looked at every 1 ms: fine enough that no note
// start is audibly late, coarse enough that the checking costs nothing.
static const int I2S_FRAMES = 256;
static const int BLOCK      = 32;


// Volume is a level index into this table rather than a plain float, so the
// button steps sound evenly spaced -- loudness is roughly logarithmic, so a
// linear step would be all jump at the bottom and nothing at the top.
static const float VOL_LEVELS[] = { 0.05f, 0.09f, 0.15f, 0.24f,
                                    0.38f, 0.55f, 0.75f, 1.00f };
static const uint8_t VOL_STEPS   = sizeof(VOL_LEVELS) / sizeof(VOL_LEVELS[0]);
static const uint8_t VOL_DEFAULT = 5;


// The balance between what you play and what plays behind you. The web
// prototype had these at 0.19 and 0.17 -- near enough equal, which through a
// laptop reads as "a band" and on this box read as "the bass is eating my
// melody". The notes are the instrument; the bass is accompaniment. These two
// numbers are the single most useful thing in this file to fiddle with.
static const float MELODY_GAIN = 0.38f;    // was 0.19
static const float BASS_GAIN   = 0.10f;    // was 0.17


// Read by the audio task on core 0 while the button changes it on core 1. A
// 32-bit aligned load/store is atomic on the Xtensa, so the worst case is one
// block still using the old level -- 1 ms, inaudible.
uint8_t vol = VOL_DEFAULT;
volatile float gVolume = VOL_LEVELS[VOL_DEFAULT];


// Where the sound goes. Tap the OUT button to cycle.
enum OutMode { OUT_SPEAKER, OUT_PHONES, OUT_BOTH };
OutMode outMode = OUT_SPEAKER;
static const char* OUT_NAME[3] = { "SPK", "HP", "SPK+HP" };


// ================================================================= TYPES ====
// These live up here, above every function, for one specific reason: the
// Arduino IDE auto-generates a prototype for each function in the sketch and
// pastes the lot immediately above the first function definition. A prototype
// that names a struct defined further down is compiled before that struct
// exists -- "'Voice' was not declared in this scope" -- and the error points
// at a line you did not write. The original sketch hit this with one struct
// and left a note about it; the music engine adds six more, so they are all
// collected here instead.


enum OscType { OSC_SINE, OSC_TRI, OSC_SAW, OSC_NOISE };


struct Voice {
  bool     active;
  uint8_t  osc;
  uint32_t startIn;        // samples still to wait before this voice sounds


  float    phase, inc;     // phase is 0..1, inc is cycles per sample
  float    incEnd, incMul; // pitch glide (the kick's drop, mostly)
  uint32_t glideLeft;


  // A topology-preserving (TPT) state-variable filter. Lowpass tap for tones,
  // highpass tap for noise -- one structure, two taps, which is why hats and
  // snares need no filter code of their own.
  //
  // This started out as the textbook Chamberlin SVF and that was a mistake:
  // Chamberlin is only stable while its coefficient stays under about 1, which
  // caps the cutoff at fs/6 -- 5.3 kHz here. The hats are a highpass at 8.2 kHz
  // and the filter simply exploded, NaN inside ten milliseconds, and once one
  // NaN reached the reverb every sample after it was NaN too. The TPT form is
  // unconditionally stable at any cutoff and any Q, so nothing has to be
  // clamped and the hats keep the cutoff they were written with.
  float    ic1, ic2;        // integrator state
  float    ca1, ca2, ca3;   // coefficients, recomputed once per block
  float    ck;              // 1/Q
  bool     useHP;
  float    cutCur, cutMul; // cutoff sweep, stepped once per block
  uint32_t cutLeft;


  // Envelope as exponential segments, exactly like the WebAudio ramps it is
  // copied from: each segment precomputes one per-sample multiplier and then
  // costs a single multiply. seg 0 attack, 1 decay, 2 hold, 3 release.
  float    env, envMul;
  uint8_t  seg;
  uint32_t segLeft;
  float    peak, sus;
  uint32_t decS, holdS, relS;


  float    rev, dly;       // send levels
  uint32_t age;            // for stealing
};


// The generic voice, same parameter list as playTone() in the prototype. Every
// sound in this instrument is one or two of these.
struct ToneSpec {
  float    freq;
  float    delaySec;    // how far ahead of now it should sound
  float    dur;
  uint8_t  type;
  float    gain;
  float    cut;
  float    q;
  float    atk, dec, sus, rel;
  float    detune;      // cents
  float    rev, dly;
  // Pitch envelope: slide from freq to freqEnd over glideSec. 0 = no slide.
  // This is the kick drum -- 130 Hz down to 44 Hz in 110 ms IS the kick -- and
  // nothing else in the instrument uses it.
  float    freqEnd, glideSec;
};


struct LoopEvent { uint8_t step; int8_t deg; bool hard; int8_t oct; bool sparse; };
static const int MAX_EVENTS = 48;
static const int MAX_LAYERS = 6;
struct Loop { LoopEvent ev[MAX_EVENTS]; uint8_t n; };


// --------------------------------------------------------------- the grid --
struct Grid { float lead; int step; int abs; };


struct Button {
  int      pin;
  uint8_t  active;
  uint8_t  integ;
  bool     down;
#if BTN_DEBUG
  bool     dbgSeen;
  bool     dbgRaw;
  uint16_t dbgFlips;
#endif
};


Button btnShift = { PIN_BTN_SHIFT, LOW, 0, false };
Button btnSet   = { PIN_BTN_SET,   LOW, 0, false };


enum CtlType : uint8_t {
  C_MEL, C_DARK, C_BRIGHT, C_OCT_STEP, C_ENERGY,
  C_LOCK, C_REC, C_RESET, C_VIBE, C_RUN,
  C_BPM_STEP,     // a = pads of travel, signed
  C_OCT_LOCK,     // toggle: the stick stops moving the octave
  C_BASS_PEDAL    // a = degree to pedal the bass on, or -1 to release
};
struct CtlMsg { uint8_t type; int8_t a; };


static bool speakerWanted() { return outMode == OUT_SPEAKER || outMode == OUT_BOTH; }
static bool phonesWanted()  { return outMode == OUT_PHONES  || outMode == OUT_BOTH; }


bool     oledOk  = false;
bool     audioOk = false;
bool     joyOk   = false;


// ============================================================ MUSIC DATA ====
// Straight port of the tables in prototype.html. Degrees are scale degrees, not
// semitones, everywhere -- that is what keeps every pad in key no matter what
// the chords underneath are doing.


static const char* FLAT_NAMES[12]  = {"C","Db","D","Eb","E","F","Gb","G","Ab","A","Bb","B"};
static const char* SHARP_NAMES[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
static const uint8_t SHARP_KEYS[6] = { 7, 2, 9, 4, 11, 6 };   // G D A E B F#


enum ScaleId { SC_MAJOR, SC_MINOR, SC_DORIAN, SC_MIXO, SC_LYDIAN };
static const int8_t SCALES[5][7] = {
  { 0, 2, 4, 5, 7, 9, 11 },   // major
  { 0, 2, 3, 5, 7, 8, 10 },   // minor
  { 0, 2, 3, 5, 7, 9, 10 },   // dorian
  { 0, 2, 4, 5, 7, 9, 10 },   // mixolydian
  { 0, 2, 4, 6, 7, 9, 11 }    // lydian
};
static const char* SCALE_NAME[5] = { "major", "minor", "dorian", "mixo", "lydian" };


// Which scale degrees form the safe pentatonic subset. The two that are NOT in
// here are the spice notes, and they are never snapped or softened.
static const int8_t PENTA[5][5] = {
  { 0, 1, 2, 4, 5 },   // major
  { 0, 2, 3, 4, 6 },   // minor
  { 0, 2, 3, 4, 6 },   // dorian
  { 0, 1, 2, 4, 6 },   // mixolydian
  { 0, 1, 2, 4, 5 }    // lydian
};


// Brightness ranking of the seven chords, brightest first: IV I V ii vi iii vii.
// Darker and brighter walk along THIS, not along the progression.
static const int8_t BRIGHTNESS[7] = { 3, 0, 4, 1, 5, 2, 6 };


enum PadTone  { PAD_WARM, PAD_SOFT, PAD_SAW };
enum LeadTone { LEAD_PLUCK, LEAD_BELL };


struct Vibe {
  const char* name;
  uint8_t  root;
  uint8_t  scale;
  uint8_t  bpm;
  int8_t   prog[4];
  float    swing;
  bool     jazzy;
  uint8_t  pad;
  uint8_t  lead;
};
static const Vibe VIBES[] = {
  { "Sunrise",   2, SC_MAJOR,  102, { 0, 4, 5, 3 }, 0.06f, false, PAD_WARM, LEAD_PLUCK },
  { "Lo-fi",     9, SC_DORIAN,  78, { 0, 3, 0, 6 }, 0.16f, true,  PAD_SOFT, LEAD_BELL  },
  { "Nightdrive",7, SC_MINOR,  118, { 0, 5, 2, 6 }, 0.02f, false, PAD_SAW,  LEAD_PLUCK },
  { "Deep house",0, SC_DORIAN, 124, { 0, 3, 0, 4 }, 0.04f, true,  PAD_SOFT, LEAD_PLUCK },
  { "Wide open", 5, SC_LYDIAN,  86, { 0, 3, 5, 4 }, 0.05f, true,  PAD_WARM, LEAD_BELL  }
};
static const uint8_t VIBE_COUNT = sizeof(VIBES) / sizeof(VIBES[0]);


// ============================================================ SONG STATE ====
// Everything in here is owned by the audio task on core 0. Core 1 only ever
// posts control events into ctrlQ and reads these for the display -- which is
// why there is not a mutex anywhere in this file. A half-updated chord name on
// one OLED frame is not worth a lock in the audio path.


struct Song {
  uint8_t  root;
  uint8_t  scaleId;
  float    bpm;
  float    swing;
  bool     jazzy;
  int8_t   prog[4];
  int8_t   baseProg[4];      // what reset puts back
  uint8_t  progIdx;
  int8_t   degree;
  int8_t   pendingDegree;    // -1 = none
  float    energy, energyTarget;
  bool     energyLock;
  int8_t   octShift;         // -2..+2, set by the stick and left there
  bool     octLocked;        // LOCK + stick: the stick stops changing it
  int8_t   bassPedal;        // LOCK + a note: bass holds this degree. -1 = off
  uint8_t  padTone, leadTone;
  bool     running;
  uint8_t  vibeIdx;
};
Song S;


// The pad's last voicing, so the next chord can move to the nearest inversion
// instead of jumping. 4 notes max -- triad plus the seventh or the ninth.
int8_t padVoicing[4];
bool   padVoiced = false;


static const int8_t* scaleTbl() { return SCALES[S.scaleId]; }


// degree -> semitone offset from the root, across octaves. Negative degrees
// work too, which is what the harmony-a-third-below needs.
static int degSemi(int d) {
  const int8_t* sc = scaleTbl();
  int n = 7;
  int idx = ((d % n) + n) % n;
  int oct = (int)floorf((float)d / n);
  return sc[idx] + 12 * oct;
}


// Chord tones as semitones from the root. The seventh (or the added ninth) is
// what stops this sounding like a school recorder ensemble.
static int chordTones(int deg, int* out) {
  int t0 = degSemi(deg), t1 = degSemi(deg + 2), t2 = degSemi(deg + 4);
  out[0] = t0; out[1] = t1; out[2] = t2;
  int third = t1 - t0, fifth = t2 - t0;
  bool isMinor = (third == 3), isDim = (fifth == 6);
  if (isDim) return 3;                          // leave diminished bare
  if (S.jazzy) {
    if (isMinor)          out[3] = t0 + 10;     // m7
    else if (deg % 7 == 4) out[3] = t0 + 10;    // dominant 7 on V
    else                   out[3] = t0 + 11;    // maj7
  } else {
    if (isMinor)          out[3] = t0 + 10;     // m7
    else if (deg % 7 == 4) out[3] = t0 + 10;    // dom7
    else                   out[3] = t0 + 14;    // add9
  }
  return 4;
}


static const char* noteName(int pc) {
  pc = ((pc % 12) + 12) % 12;
  for (int i = 0; i < 6; i++) if (SHARP_KEYS[i] == S.root) return SHARP_NAMES[pc];
  return FLAT_NAMES[pc];
}


static void chordLabel(int deg, char* buf, size_t n) {
  int t0 = degSemi(deg), t1 = degSemi(deg + 2), t2 = degSemi(deg + 4);
  int third = t1 - t0, fifth = t2 - t0;
  int pc = (S.root + t0) % 12;
  if (fifth == 6) { snprintf(buf, n, "%sdim", noteName(pc)); return; }
  const char* q = "";
  if (S.jazzy)          q = (third == 3) ? "m7" : ((deg % 7 == 4) ? "7" : "maj7");
  else if (third == 3)  q = "m7";
  else if (deg % 7 == 4) q = "7";
  snprintf(buf, n, "%s%s", noteName(pc), q);
}


static const char* ROMAN[7] = { "I", "II", "III", "IV", "V", "VI", "VII" };
static void romanLabel(int deg, char* buf, size_t n) {
  int t0 = degSemi(deg), t1 = degSemi(deg + 2), t2 = degSemi(deg + 4);
  bool minor = (t1 - t0) == 3, dim = (t2 - t0) == 6;
  const char* r = ROMAN[((deg % 7) + 7) % 7];
  if (!minor) { snprintf(buf, n, "%s", r); return; }
  char low[5];
  size_t i = 0;
  for (; r[i] && i < sizeof(low) - 1; i++) low[i] = (char)tolower((unsigned char)r[i]);
  low[i] = 0;
  snprintf(buf, n, "%s%s", low, dim ? "o" : "");
}


static bool isSpice(int deg) {
  const int8_t* p = PENTA[S.scaleId];
  for (int i = 0; i < 5; i++) if (p[i] == deg) return false;
  return true;
}


// ------------------------------------------------------- voice leading -----
// Nearest MIDI note with the given pitch class. This is the whole trick behind
// the chords never lurching: each voice moves to the closest note that spells
// the new chord, so a change is two voices shifting a semitone, not four
// voices jumping an octave.
static int nearestPitch(int pc, int anchor) {
  int p = anchor - ((((anchor - pc) % 12) + 12) % 12);
  return (anchor - p) > 6 ? p + 12 : p;
}


static int voiceChord(int deg, int8_t* out) {
  int tones[4];
  int n = chordTones(deg, tones);
  for (int i = 0; i < n; i++) {
    int pc = ((S.root + tones[i]) % 12 + 12) % 12;
    int anchor = (padVoiced && i < 4) ? padVoicing[i] : 66 + i * 4;
    int p = nearestPitch(pc, anchor);
    while (p < 52) p += 12;
    while (p > 82) p -= 12;
    out[i] = (int8_t)p;
  }
  for (int i = 1; i < n; i++) {          // insertion sort, n <= 4
    int8_t v = out[i], j = i - 1;
    while (j >= 0 && out[j] > v) { out[j + 1] = out[j]; j--; }
    out[j + 1] = v;
  }
  return n;
}


static int bassNote(int deg) {
  int pc = ((S.root + degSemi(deg)) % 12 + 12) % 12;
  int p = nearestPitch(pc, 43);
  if (p < 33) p += 12;
  return p;
}


static inline float mtof(float m) { return 440.0f * powf(2.0f, (m - 69.0f) / 12.0f); }


// nudge an exposed note onto the nearest tone of the chord underneath, but
// never by more than a whole tone -- past that it stops being the note you
// pressed and starts being the instrument overruling you.
static int snapToChord(int note, int deg) {
  int tones[4];
  int n = chordTones(deg, tones);
  int best = note, bestD = 99;
  for (int i = 0; i < n; i++) {
    int pc = ((S.root + tones[i]) % 12 + 12) % 12;
    int cand = nearestPitch(pc, note);
    int d = abs(cand - note);
    if (d < bestD) { bestD = d; best = cand; }
  }
  return bestD <= 2 ? best : note;
}


// ============================================================ SYNTH =========
// The web version got a fresh oscillator, filter and gain node per note and
// threw them away afterwards. Here there is a fixed pool, and the quietest is
// stolen when they run out. 48 rather than the 28 this started with: dense
// playing is around sixteen notes a second, each spawning two or three voices
// with half a second of tail, on top of a four-note pad, bass, drums and up to
// six recorded loop layers. 28 ran dry in under a second of fast playing and
// the stealing was audible. At 48 the pool is ~6 KB and never empties in
// normal use.
// The Voice and ToneSpec structs are up in TYPES; see the note there.


static const int NVOICE = 48;
Voice voices[NVOICE];
uint32_t voiceAge = 1;


// A 1024-point sine, linearly interpolated. Cheaper than sinf() per sample and
// the interpolation error sits ~80 dB down, well under the noise floor of a
// 3 dollar amp.
static const int SINE_N = 1024;
float sineTbl[SINE_N + 1];


static uint32_t rngState = 0x1234567u;
static inline float frand() {          // -1..1 white noise, xorshift
  rngState ^= rngState << 13;
  rngState ^= rngState >> 17;
  rngState ^= rngState << 5;
  return (float)(int32_t)rngState * (1.0f / 2147483648.0f);
}
static inline float urand() { return (frand() + 1.0f) * 0.5f; }   // 0..1


// Exponential ramp multiplier: what to multiply by, each sample, to get from
// `from` to `to` in `n` samples. Guarding both ends away from zero is what
// keeps this from producing inf or nan -- the same reason the WebAudio version
// ramps to .0001 and never to 0.
static inline float rampMul(float from, float to, uint32_t n) {
  if (n == 0) return 1.0f;
  if (from < 1e-6f) from = 1e-6f;
  if (to   < 1e-6f) to   = 1e-6f;
  return expf(logf(to / from) / (float)n);
}


static inline uint32_t secToSamp(float s) {
  if (s < 0) s = 0;
  return (uint32_t)(s * SR);
}


// Recompute the filter coefficients. Called once per block rather than per
// sample: tanf is the expensive part, and a cutoff sweep stepping every 1 ms
// is not something you can hear as steps.
static inline void voiceCoef(Voice& v) {
  float fc = v.cutCur;
  if (fc > SR * 0.45f) fc = SR * 0.45f;    // just short of Nyquist, where tan blows up
  if (fc < 20.0f)      fc = 20.0f;
  float g = tanf(3.14159265f * fc / SR);   // prewarped, so the cutoff is where it says
  v.ca1 = 1.0f / (1.0f + g * (g + v.ck));
  v.ca2 = g * v.ca1;
  v.ca3 = g * v.ca2;
}


// Steal the QUIETEST voice, not the oldest. Oldest is the obvious choice and
// the wrong one: the oldest voice is usually the pad, which is the one thing
// holding the harmony up, and cutting it dead mid-bar is a click plus a hole.
// The quietest voice is almost always one already deep in its release, where
// truncating it is inaudible. This was the other half of the graininess when
// playing fast -- dense playing spawns three voices per note and ran the pool
// dry in under a second.
static Voice* allocVoice() {
  Voice* best = nullptr;
  float  bestEnv = 1e9f;
  for (int i = 0; i < NVOICE; i++) {
    if (!voices[i].active) return &voices[i];
    float e = voices[i].env;
    if (voices[i].seg == 3) e *= 0.25f;      // already dying: prefer it
    if (e < bestEnv) { bestEnv = e; best = &voices[i]; }
  }
  return best;
}






static void playTone(const ToneSpec& t) {
  Voice* v = allocVoice();
  if (!v) return;


  v->active  = true;
  v->age     = voiceAge++;
  v->osc     = t.type;
  v->startIn = secToSamp(t.delaySec);
  v->phase   = 0.0f;


  float f = t.freq * powf(2.0f, t.detune / 1200.0f);
  v->inc = f / SR;
  if (t.freqEnd > 0.0f && t.glideSec > 0.0f) {
    v->incEnd    = t.freqEnd / SR;
    v->glideLeft = secToSamp(t.glideSec);
    v->incMul    = rampMul(v->inc, v->incEnd, v->glideLeft);
  } else {
    v->incEnd = v->inc; v->glideLeft = 0; v->incMul = 1.0f;
  }


  // Filter sweep: open bright and close down to `cut` over the decay. That
  // downward sweep is most of what makes a pluck sound plucked.
  v->useHP = false;
  v->ic1 = v->ic2 = 0.0f;
  float q  = t.q < 0.05f ? 0.05f : t.q;
  v->ck    = 1.0f / q;
  float cutHi = fminf(t.cut * 2.2f, 15000.0f);
  float cutLo = fmaxf(t.cut, 200.0f);
  v->cutCur  = cutHi;
  v->cutLeft = secToSamp(fmaxf(t.dec, 0.08f)) / BLOCK;
  v->cutMul  = rampMul(cutHi, cutLo, v->cutLeft ? v->cutLeft : 1);
  voiceCoef(*v);


  v->peak = t.gain;
  v->sus  = t.gain * t.sus;
  v->env  = 1e-4f;
  v->seg  = 0;
  uint32_t atkS = secToSamp(t.atk);
  if (atkS < 8) atkS = 8;                   // never instant: 0.25 ms of fade
  v->segLeft = atkS;
  v->envMul  = rampMul(v->env, v->peak, atkS);
  v->decS    = secToSamp(t.dec);
  uint32_t durS = secToSamp(t.dur);
  v->holdS   = (durS > atkS + v->decS) ? durS - atkS - v->decS : 0;
  v->relS    = secToSamp(t.rel);


  v->rev = t.rev;
  v->dly = t.dly;
}


// A noise burst: the whole drum kit above the kick. Highpass instead of
// lowpass, one exponential decay, no sustain.
static void playNoise(float delaySec, float dur, float cut, float gain, float rev) {
  Voice* v = allocVoice();
  if (!v) return;


  v->active  = true;
  v->age     = voiceAge++;
  v->osc     = OSC_NOISE;
  v->startIn = secToSamp(delaySec);
  v->phase   = 0.0f;
  v->inc = v->incEnd = 0.0f; v->incMul = 1.0f; v->glideLeft = 0;


  v->useHP  = true;
  v->ic1 = v->ic2 = 0.0f;
  v->ck     = 1.4f;             // gently damped: a hat wants no resonant peak
  v->cutCur = cut;
  v->cutLeft = 0; v->cutMul = 1.0f;
  voiceCoef(*v);


  v->peak = gain; v->sus = gain;
  v->env  = gain;
  v->seg  = 3;                              // straight into the release
  v->segLeft = secToSamp(dur);
  v->envMul  = rampMul(gain, 1e-4f, v->segLeft);
  v->decS = v->holdS = v->relS = 0;


  v->rev = rev; v->dly = 0.0f;
}


// ============================================================ EFFECTS =======
// A Schroeder reverb -- four combs into two allpasses -- and one dotted-eighth
// delay. Both are mono: at this speaker size stereo width is a rounding error,
// and mono halves the memory and the arithmetic.


static const int COMB_N[4] = { 810, 927, 1032, 1130 };   // freeverb, scaled to 32k
static const int AP_N[2]   = { 403, 320 };
float combBuf0[810], combBuf1[927], combBuf2[1032], combBuf3[1130];
float apBuf0[403], apBuf1[320];
float* combBuf[4] = { combBuf0, combBuf1, combBuf2, combBuf3 };
float* apBuf[2]   = { apBuf0, apBuf1 };
int    combIdx[4] = { 0, 0, 0, 0 };
int    apIdx[2]   = { 0, 0 };
float  combLp[4]  = { 0, 0, 0, 0 };
static const float COMB_FB   = 0.84f;
static const float COMB_DAMP = 0.20f;
float revHpPrevIn = 0, revHpPrevOut = 0;
static const float REV_HP_A = 0.947f;      // ~280 Hz highpass at 32 kHz


// Longest dotted eighth is at the slowest tempo we allow: 60 bpm -> 0.75 s.
// int16 rather than float halves 115 KB to 58 KB, and a feedback delay is the
// one place quantisation noise genuinely does not matter -- it is already
// being fed back through a lowpass at 2.6 kHz.
static const int DLY_MAX = (int)(SR * 0.78f);
int16_t dlyBuf[DLY_MAX];
int     dlyIdx = 0;
int     dlyLen = SR / 2;
float   dlyLp  = 0;
static const float DLY_FB  = 0.34f;
static const float DLY_OUT = 0.38f;
static const float DLY_LP_A = 0.40f;       // ~2.6 kHz one-pole


static void setDelayTime() {
  int n = (int)((60.0f / S.bpm) * 0.75f * SR);
  if (n < 64) n = 64;
  if (n > DLY_MAX - 1) n = DLY_MAX - 1;
  dlyLen = n;
}


// ---- speaker protection ----------------------------------------------------
// The kick sweeps down to 44 Hz, which is right for headphones and physically
// impossible for a small speaker. Measured on this mix, content below 100 Hz
// reaches 0.56 of full scale -- over half the cone's travel spent producing a
// frequency the driver cannot radiate. It does not come out as a note; it comes
// out as the cone hitting its limits, which is exactly why it gets worse the
// louder you go. It also eats the headroom everything else needs.
//
// So the speaker path gets a 2-pole highpass and the headphone path does not.
// The kick still reads as a kick: its harmonics at 88, 132, 176 Hz all survive,
// and the ear reconstructs a fundamental it never actually hears.
//
// Set SPEAKER_HPF_HZ to 0 to switch this off entirely. Raise it towards 150 if
// the box still struggles, lower it towards 80 if the kick sounds thin. Both
// output boards share one I2S stream, so in SPK+HP mode the headphones get the
// filtered signal too -- switch to HP alone for the full-range mix.
static const float SPEAKER_HPF_HZ = 110.0f;
static const float HPF_Q          = 0.707f;    // Butterworth: flat, no bump


float hpA1 = 0, hpA2 = 0, hpA3 = 0;
static const float HPF_K = 1.0f / HPF_Q;
float hpIc1 = 0, hpIc2 = 0;
// Written by applyOutput() on core 1, read by the audio task on core 0. A bool
// is a single aligned byte, so the worst case is one block on the old setting.
volatile bool hpfOn = false;


static void hpfInit() {
  float g = tanf(3.14159265f * SPEAKER_HPF_HZ / SR);
  hpA1 = 1.0f / (1.0f + g * (g + HPF_K));
  hpA2 = g * hpA1;
  hpA3 = g * hpA2;
}


// Same TPT structure as the voice filter, highpass tap. Unconditionally
// stable, so it cannot be the thing that breaks at some odd cutoff.
static inline float hpfTick(float x) {
  float v3 = x - hpIc2;
  float v1 = hpA1 * hpIc1 + hpA2 * v3;
  float v2 = hpIc2 + hpA2 * hpIc1 + hpA3 * v3;
  hpIc1 = 2.0f * v1 - hpIc1;
  hpIc2 = 2.0f * v2 - hpIc2;
  return x - HPF_K * v1 - v2;
}


static void fxClear() {
  memset(combBuf0, 0, sizeof(combBuf0)); memset(combBuf1, 0, sizeof(combBuf1));
  memset(combBuf2, 0, sizeof(combBuf2)); memset(combBuf3, 0, sizeof(combBuf3));
  memset(apBuf0, 0, sizeof(apBuf0));     memset(apBuf1, 0, sizeof(apBuf1));
  memset(dlyBuf, 0, sizeof(dlyBuf));
  for (int i = 0; i < 4; i++) { combIdx[i] = 0; combLp[i] = 0; }
  apIdx[0] = apIdx[1] = 0;
  revHpPrevIn = revHpPrevOut = dlyLp = 0;
}


static inline float reverbTick(float in) {
  // Highpass the send. Without this the pad's low notes pile up in the tail
  // and the whole mix turns to mud within a couple of bars.
  float hp = REV_HP_A * (revHpPrevOut + in - revHpPrevIn);
  revHpPrevIn = in; revHpPrevOut = hp;


  float acc = 0.0f;
  for (int c = 0; c < 4; c++) {
    float y = combBuf[c][combIdx[c]];
    acc += y;
    combLp[c] = y * (1.0f - COMB_DAMP) + combLp[c] * COMB_DAMP;
    combBuf[c][combIdx[c]] = hp + combLp[c] * COMB_FB;
    if (++combIdx[c] >= COMB_N[c]) combIdx[c] = 0;
  }
  acc *= 0.25f;


  for (int a = 0; a < 2; a++) {
    float y = apBuf[a][apIdx[a]];
    float out = y - acc;
    apBuf[a][apIdx[a]] = acc + y * 0.5f;
    if (++apIdx[a] >= AP_N[a]) apIdx[a] = 0;
    acc = out;
  }
  return acc;
}


// Returns the delay's output, which the caller also feeds back into the reverb
// -- the repeats getting wetter as they fade is what the web version's
// dlyOut -> revIn patch was doing.
static inline float delayTick(float in) {
  int rd = dlyIdx - dlyLen;
  if (rd < 0) rd += DLY_MAX;
  float y = dlyBuf[rd] * (1.0f / 32768.0f);


  dlyLp = y * (1.0f - DLY_LP_A) + dlyLp * DLY_LP_A;
  float w = in + dlyLp * DLY_FB;
  if (w >  1.0f) w =  1.0f;
  if (w < -1.0f) w = -1.0f;
  dlyBuf[dlyIdx] = (int16_t)(w * 32767.0f);
  if (++dlyIdx >= DLY_MAX) dlyIdx = 0;


  return y * DLY_OUT;
}


// ---- master bus ----
// This is where the graininess lived, and it was self-inflicted. The first
// version compressed hard at -12 dB, then applied 1.35x of makeup gain that
// the WebAudio original never had, then HARD-clamped at +/-1.2 before a cubic
// waveshaper. Measured on the bench that left the mix sitting at 0.867 against
// a 0.883 ceiling -- permanently jammed into a hard corner, so every kick and
// every note was being shaved flat. That is what "grainy on some beats and
// worse at full volume" sounds like.
//
// The chain now has real headroom and never reaches a corner at all:
//   trim -> gentle compressor -> soft limiter with a smooth knee -> volume
// The compressor sits high and shallow, so it only leans on genuine peaks
// instead of riding the whole mix, and the limiter below it is C1-continuous
// -- no discontinuity in the waveform OR in its slope, which is the part the
// ear actually hears as grit.
static const float COMP_THR   = 0.50f;                 // -6 dB, up from -12
static const float COMP_SLOPE = 1.0f / 3.0f - 1.0f;    // ratio 3, down from 4
float compEnv = 0.0f;
static const float COMP_ATK = 0.9922f;   // exp(-1/(0.004*SR))
static const float COMP_REL = 0.99983f;  // exp(-1/(0.180*SR))


// Counts samples the limiter had to act on. Zero here while the speaker still
// sounds grainy means the graininess is NOT the mix -- see the load meter and
// the note about the amp's supply in the header.
volatile uint32_t gLimited = 0;


// Linear below the knee, asymptotic to 1.0 above it, and the slope is exactly
// 1 at the knee so there is no kink where the two meet. Nothing can ever leave
// this function outside +/-1, so the DAC cannot be overrun no matter how many
// voices pile up.
static const float LIM_KNEE = 0.75f;
static inline float softLimit(float x) {
  float a = fabsf(x);
  if (a <= LIM_KNEE) return x;
  gLimited = gLimited + 1;
  float over = a - LIM_KNEE;
  float room = 1.0f - LIM_KNEE;
  float y = LIM_KNEE + room * (over / (over + room));
  return x < 0.0f ? -y : y;
}


static inline float masterTick(float x) {
  x *= 0.85f;


  float a = fabsf(x);
  float c = (a > compEnv) ? COMP_ATK : COMP_REL;
  compEnv = a + (compEnv - a) * c;
  if (compEnv > COMP_THR) x *= powf(compEnv / COMP_THR, COMP_SLOPE);


  // After the compressor, so the compressor still reacts to the real kick --
  // it should duck for a hit the speaker cannot reproduce, because that hit is
  // still real energy. Before the volume and the limiter, so what the limiter
  // sees is what the speaker actually gets.
  if (hpfOn) x = hpfTick(x);


  // Volume BEFORE the limiter, not after. The old order limited at full scale
  // and then turned the result down, so the distortion was baked in at every
  // volume setting and only got louder. This way turning down genuinely backs
  // the signal away from the limiter.
  return softLimit(x * gVolume);
}


// ======================================================== VOICE RENDERING ===


static inline float oscSample(Voice& v) {
  float p = v.phase;
  v.phase += v.inc;
  if (v.phase >= 1.0f) v.phase -= 1.0f;


  switch (v.osc) {
    case OSC_SINE: {
      float x = p * SINE_N;
      int   i = (int)x;
      float fr = x - i;
      return sineTbl[i] + (sineTbl[i + 1] - sineTbl[i]) * fr;
    }
    case OSC_TRI:
      return 4.0f * fabsf(p - 0.5f) - 1.0f;
    case OSC_SAW: {
      // PolyBLEP on the one discontinuity. Naive saws at these pitches fold
      // audibly against a 32 kHz Nyquist; this costs four lines and removes
      // most of it, which matters because the bass and one pad tone are saws.
      float s = 2.0f * p - 1.0f;
      float dt = v.inc;
      if (dt > 1e-9f) {
        if (p < dt)            { float t = p / dt;        s -= t + t - t * t - 1.0f; }
        else if (p > 1.0f - dt){ float t = (p - 1.0f)/dt; s -= t * t + t + t + 1.0f; }
      }
      return s;
    }
    default:
      return frand();
  }
}


static void renderVoices(float* dry, float* revS, float* dlyS, int n) {
  for (int vi = 0; vi < NVOICE; vi++) {
    Voice& v = voices[vi];
    if (!v.active) continue;


    int i = 0;
    if (v.startIn) {                       // still waiting for its grid line
      if (v.startIn >= (uint32_t)n) { v.startIn -= n; continue; }
      i = (int)v.startIn;
      v.startIn = 0;
    }


    // One cutoff step per block, not per sample -- see voiceCoef().
    if (v.cutLeft) { v.cutCur *= v.cutMul; v.cutLeft--; voiceCoef(v); }


    for (; i < n; i++) {
      // --- envelope -------------------------------------------------------
      v.env *= v.envMul;
      if (v.segLeft) v.segLeft--;
      while (v.segLeft == 0 && v.seg < 3) {
        v.seg++;
        if (v.seg == 1)      { v.segLeft = v.decS;  v.envMul = rampMul(v.env, v.sus, v.decS); }
        else if (v.seg == 2) { v.segLeft = v.holdS; v.envMul = 1.0f; }
        else                 { v.segLeft = v.relS;  v.envMul = rampMul(v.env, 1e-4f, v.relS); }
        if (v.segLeft == 0 && v.seg == 3) break;
      }
      if (v.seg == 3 && v.segLeft == 0) { v.active = false; break; }


      // --- pitch glide ----------------------------------------------------
      if (v.glideLeft) { v.inc *= v.incMul; v.glideLeft--; }


      // --- oscillator + filter -------------------------------------------
      float in = oscSample(v);
      float v3 = in - v.ic2;
      float v1 = v.ca1 * v.ic1 + v.ca2 * v3;
      float v2 = v.ic2 + v.ca2 * v.ic1 + v.ca3 * v3;
      v.ic1 = 2.0f * v1 - v.ic1;
      v.ic2 = 2.0f * v2 - v.ic2;
      float s = (v.useHP ? (in - v.ck * v1 - v2) : v2) * v.env;


      dry[i] += s;
      if (v.rev > 0.0f) revS[i] += s * v.rev;
      if (v.dly > 0.0f) dlyS[i] += s * v.dly;
    }
  }
}


// ========================================================== SEQUENCER =======


uint64_t samplePos      = 0;     // free-running sample clock, the only timebase
uint64_t nextStepSample = 0;
int      curStep = 0;            // 0..15 inside the bar
int      absStep = 0;            // 0..63 inside the four-bar loop
static const int LOOP_STEPS = 64;


static inline uint32_t stepSamples() { return (uint32_t)((60.0f / S.bpm) / 4.0f * SR); }
static inline float swingOffset(int i) { return (i & 1) ? S.swing * ((60.0f / S.bpm) / 4.0f) : 0.0f; }
static inline float jitterSec() { return (urand() - 0.5f) * 0.008f; }


// --------------------------------------------------------- loop recorder ---




Loop    loops[MAX_LAYERS];
uint8_t loopCount = 0;
Loop    recBuf;
bool    recArmed = false, recording = false;
uint8_t recBars  = 0;


// ------------------------------------------------------------- density -----
// The instrument watches how fast you are playing and changes its own rules.
// Sparse notes are treated as statements -- snapped to a chord tone, landed on
// a 1/8 line, left to ring. Dense notes are treated as texture -- short plucks
// on the 1/16 grid with the full pentatonic. Hysteresis on both ends so it
// does not flap on the boundary.
static const int HITLOG_N = 12;
uint64_t hitLog[HITLOG_N];
uint8_t  hitLogN = 0;
bool     sparse  = true;


static bool noteMode() {
  uint64_t cutoff = (samplePos > (uint64_t)(1.8f * SR)) ? samplePos - (uint64_t)(1.8f * SR) : 0;
  uint8_t w = 0;
  for (uint8_t i = 0; i < hitLogN; i++) if (hitLog[i] > cutoff) hitLog[w++] = hitLog[i];
  hitLogN = w;
  if (hitLogN >= 5)      sparse = false;
  else if (hitLogN <= 2) sparse = true;
  return sparse;
}
static void hitLogPush() {
  if (hitLogN >= HITLOG_N) {
    for (uint8_t i = 1; i < HITLOG_N; i++) hitLog[i - 1] = hitLog[i];
    hitLogN = HITLOG_N - 1;
  }
  hitLog[hitLogN++] = samplePos;
}


// ------------------------------------------------------------- the sounds --
static void kickHit(float lead, float v) {
  ToneSpec t = {};
  t.freq = 130.0f; t.delaySec = lead; t.dur = 0.02f; t.type = OSC_SINE;
  t.gain = 0.9f * v; t.cut = 3000.0f; t.q = 0.7f;
  t.atk = 0.002f; t.dec = 0.11f; t.sus = 0.35f; t.rel = 0.24f;
  t.rev = 0.02f;
  t.freqEnd = 44.0f; t.glideSec = 0.11f;   // the drop that makes it a kick
  playTone(t);
}


static void snareHit(float lead, float v) {
  playNoise(lead, 0.17f, 1500.0f, 0.34f * v, 0.22f);
  ToneSpec t = {};
  t.freq = 210.0f; t.delaySec = lead; t.dur = 0.01f; t.type = OSC_TRI;
  t.gain = 0.22f * v; t.cut = 1800.0f; t.q = 0.7f;
  t.atk = 0.001f; t.dec = 0.04f; t.sus = 0.3f; t.rel = 0.09f;
  playTone(t);
}
static inline void hatHit(float lead, float v)  { playNoise(lead, 0.035f, 8200.0f, 0.11f * v, 0.05f); }
static inline void ohatHit(float lead, float v) { playNoise(lead, 0.19f,  7200.0f, 0.09f * v, 0.14f); }


static bool inList(const int8_t* list, int n, int s) {
  for (int i = 0; i < n; i++) if (list[i] == s) return true;
  return false;
}


// Four energy levels, and every pattern in the kit is indexed by it. Holding
// pad 0 walks up this; the transitions are glided, so it thickens rather than
// switching.
static void drumPattern(int step, float lead) {
  int e = (int)lroundf(S.energy);
  if (e < 0) e = 0; if (e > 3) e = 3;
  static const int8_t K0[] = {0,8}, K1[] = {0,8}, K2[] = {0,6,8,14}, K3[] = {0,3,6,8,11,14};
  const int8_t* K[4]  = { K0, K1, K2, K3 };
  const int     KN[4] = { 2, 2, 4, 6 };
  if (inList(K[e], KN[e], step)) kickHit(lead, step == 0 ? 1.0f : 0.86f);


  if (e >= 1 && (step == 4 || step == 12)) snareHit(lead, e >= 2 ? 1.0f : 0.7f);
  if (e == 0 && step == 12)                snareHit(lead, 0.4f);


  static const int8_t H0[] = {4,12}, H1[] = {0,4,8,12}, H2[] = {0,2,4,6,8,10,12,14};
  const int8_t* H[4]  = { H0, H1, H2, H2 };
  const int     HN[4] = { 2, 4, 8, 8 };
  if (inList(H[e], HN[e], step)) hatHit(lead, (step % 4 == 0) ? 0.9f : 0.55f);
  if (e >= 3 && (step == 3 || step == 11)) hatHit(lead, 0.4f);
  if (e >= 2 && step == 14)                ohatHit(lead, 0.8f);
}


static void bassPattern(int step, int deg, float lead) {
  int e = (int)lroundf(S.energy);
  if (e < 0) e = 0; if (e > 3) e = 3;
  static const int8_t B0[] = {0}, B1[] = {0,8}, B2[] = {0,3,8,11}, B3[] = {0,2,4,6,8,10,12,14};
  const int8_t* B[4]  = { B0, B1, B2, B3 };
  const int     BN[4] = { 1, 2, 4, 8 };
  if (!inList(B[e], BN[e], step)) return;


  int n = bassNote(deg);
  if (e >= 2 && (step == 11 || step == 6 || step == 14)) n += 12;
  ToneSpec t = {};
  t.freq = mtof((float)n); t.delaySec = lead; t.dur = (e >= 3) ? 0.1f : 0.3f;
  t.type = OSC_SAW; t.gain = BASS_GAIN; t.cut = 240.0f + S.energy * 90.0f; t.q = 6.0f;
  t.atk = 0.006f; t.dec = 0.1f; t.sus = 0.7f; t.rel = 0.12f; t.rev = 0.04f;
  playTone(t);
}


static void playPad(int deg, float lead) {
  int8_t v[4];
  int n = voiceChord(deg, v);
  for (int i = 0; i < n; i++) padVoicing[i] = v[i];
  if (n < 4) for (int i = n; i < 4; i++) padVoicing[i] = v[n - 1];
  padVoiced = true;


  bool isSoft = (S.padTone == PAD_SOFT);
  float barLen = (60.0f / S.bpm) * 4.0f;
  for (int i = 0; i < n; i++) {
    ToneSpec t = {};
    t.freq = mtof((float)v[i]);
    t.delaySec = lead + i * 0.012f;     // a tiny spread, so it strums
    t.dur  = barLen * 0.92f;
    t.type = (S.padTone == PAD_SAW) ? OSC_SAW : (isSoft ? OSC_SINE : OSC_TRI);
    t.gain = (isSoft ? 0.075f : 0.06f) * (i == 0 ? 1.1f : 0.9f);
    t.cut  = 700.0f + S.energy * 420.0f; t.q = 0.8f;
    t.atk  = isSoft ? 0.5f : 0.18f; t.dec = 0.5f; t.sus = 0.8f; t.rel = 0.9f;
    t.detune = (i & 1) ? 6.0f : -6.0f;
    t.rev = 0.5f;
    playTone(t);
  }
}


// ---------------------------------------------------------------- melody ---
volatile uint32_t gMelFlash[7] = {0,0,0,0,0,0,0};   // millis, read by the OLED


// Low notes lose twice over: the ear is far less sensitive down there (the
// bottom of the equal-loudness curves), and the speaker highpass takes the
// fundamental away entirely below ~110 Hz. Without this, dropping two octaves
// on the stick made notes almost vanish. Roughly +6 dB per octave below middle
// C, capped so it cannot run away.
static inline float lowNoteBoost(int note) {
  float b = 1.0f + 0.60f * (60.0f - (float)note) / 12.0f;
  if (b < 1.0f) b = 1.0f;
  if (b > 2.6f) b = 2.6f;
  return b;
}


static void playMelody(int deg, float lead, bool hard, int oct, bool isSparse) {
  // Seven pads, strictly ascending: no octave wrapping, so the row reads low
  // to high the way it is printed.
  int note = 60 + S.root + degSemi(deg) + 12 * oct;
  while (note > 96) note -= 12;
  while (note < 40) note += 12;


  bool spice = isSpice(deg);
  // Exposed notes land on a chord tone -- but the spice notes keep their bite,
  // because those two are the whole reason the row is not boring.
  if (isSparse && !spice) note = snapToChord(note, S.degree);


  float vel = (hard ? 1.0f : ((isSparse ? 0.82f : 0.62f) + urand() * 0.12f)) * (spice ? 0.88f : 1.0f);
  bool bell = (S.leadTone == LEAD_BELL);
  float dur = isSparse ? (bell ? 0.95f : 0.62f) : (bell ? 0.5f : 0.22f);
  float boost = lowNoteBoost(note);


  ToneSpec t = {};
  t.freq = mtof((float)note); t.delaySec = lead; t.dur = dur;
  t.type = bell ? OSC_SINE : OSC_TRI;
  t.gain = MELODY_GAIN * vel * boost;
  t.cut  = (bell ? 2600.0f : 1800.0f) + vel * 2600.0f; t.q = 2.0f;
  t.atk  = isSparse ? 0.012f : 0.004f;
  t.dec  = isSparse ? 0.32f  : (bell ? 0.5f : 0.13f);
  t.sus  = isSparse ? 0.62f  : (bell ? 0.3f : 0.35f);
  t.rel  = isSparse ? 1.1f   : (bell ? 0.9f : 0.4f);
  t.rev  = isSparse ? 0.5f   : 0.34f;
  t.dly  = (isSparse ? 0.45f : 0.3f) + S.energy * 0.05f;
  playTone(t);


  if (!bell) {                          // a saw underneath, for body
    ToneSpec u = {};
    u.freq = mtof((float)note); u.delaySec = lead + 0.004f;
    u.dur = isSparse ? 0.5f : 0.18f; u.type = OSC_SAW;
    // Twice as much saw as before, and it matters most low down: the saw is
    // all harmonics, so it is the part of a low note that survives both the
    // speaker highpass and the ear.
    u.gain = (isSparse ? 0.10f : 0.14f) * vel * boost;
    u.cut = 1500.0f + vel * 2200.0f; u.q = 3.0f;
    u.atk = 0.003f; u.dec = 0.1f; u.sus = isSparse ? 0.35f : 0.2f;
    u.rel = isSparse ? 0.8f : 0.3f; u.rev = 0.2f;
    playTone(u);
  }


  if (hard) {
    // Harmony a diatonic third below -- inside the key, so it is always
    // consonant no matter which pad you hit.
    int n2 = 60 + S.root + degSemi(deg - 2) + 12 * oct;
    while (n2 > note - 2) n2 -= 12;
    while (n2 < 40) n2 += 12;
    ToneSpec h = {};
    h.freq = mtof((float)n2); h.delaySec = lead + 0.006f; h.dur = 0.2f;
    h.type = OSC_TRI; h.gain = 0.2f * boost; h.cut = 2400.0f; h.q = 1.5f;
    h.atk = 0.004f; h.dec = 0.12f; h.sus = 0.3f; h.rel = 0.35f;
    h.rev = 0.3f; h.dly = 0.2f;
    playTone(h);
  }


  if (deg >= 0 && deg < 7) gMelFlash[deg] = millis();
}






// Where a live press should actually land. coarse = snap to 1/8 rather than
// 1/16, so an exposed note lands on a strong beat.
static Grid nextGrid(bool coarse) {
  Grid g;
  if (!S.running) { g.lead = 0.0f; g.step = curStep; g.abs = absStep; return g; }
  uint64_t t = samplePos + secToSamp(0.02f);
  uint64_t time = nextStepSample;
  int s = curStep, a = absStep;
  uint32_t sd = stepSamples();
  int guard = 0;
  while ((time < t || (coarse && (s & 1))) && guard++ < 64) {
    time += sd; s = (s + 1) % 16; a = (a + 1) % LOOP_STEPS;
  }
  float lead = (time > samplePos) ? (float)(time - samplePos) / SR : 0.0f;
  g.lead = lead + swingOffset(s) + jitterSec();
  if (g.lead < 0) g.lead = 0;
  g.step = s; g.abs = a;
  return g;
}


// ---------------------------------------------------------- the bar clock --
static void scheduleStep(int step, float lead, int aStep) {
  // Swing pushes every off-beat 16th late by a fraction of a step, and a few
  // ms of jitter on top keeps a machine-exact grid from sounding like one.
  // The pad is deliberately NOT given either -- see below.
  float played = lead + swingOffset(step) + jitterSec();


  if (step == 0) {
    // Chord advance on the bar. A pending degree -- something darker/brighter
    // put there -- wins over the progression, and does not consume a slot.
    if (S.pendingDegree >= 0) { S.degree = S.pendingDegree; S.pendingDegree = -1; }
    else {
      S.progIdx = (S.progIdx + 1) % 4;
      S.degree  = S.prog[S.progIdx];
    }
    // The pad lands on the bar line itself. Swing and jitter are for the
    // things that are supposed to feel played; a chord arriving 20 ms late
    // just sounds like the box is struggling.
    playPad(S.degree, lead);


    if (recArmed) { recArmed = false; recording = true; recBuf.n = 0; recBars = 0; }
    else if (recording) {
      recBars++;
      if (recBars >= 4) {
        if (recBuf.n && loopCount < MAX_LAYERS) loops[loopCount++] = recBuf;
        recording = false;
      }
    }
  }


  drumPattern(step, played);
  // A pedal point: the bass stops following the chords and holds one root
  // while everything above it keeps moving. It is the oldest trick there is
  // for making four chords sound like they are going somewhere.
  bassPattern(step, S.bassPedal >= 0 ? S.bassPedal : S.degree, played);


  for (uint8_t l = 0; l < loopCount; l++)
    for (uint8_t i = 0; i < loops[l].n; i++)
      if (loops[l].ev[i].step == aStep) {
        const LoopEvent& e = loops[l].ev[i];
        playMelody(e.deg, played, e.hard, e.oct, e.sparse);
      }


  // Energy glides toward its target rather than jumping -- one 16th at a time,
  // which is roughly a bar to go from nothing to full.
  S.energy += (S.energyTarget - S.energy) * 0.06f;
}


// ============================================================== ACTIONS =====


static void hitMelody(int deg, bool hard, int octBump) {
  hitLogPush();
  bool sp = noteMode();
  Grid g = nextGrid(sp);
  // A note landing on beat 1 or 3 gets the accent for free, the way a player
  // leans on the downbeat. A firm press gets it too -- see PRESSURE_ACCENT.
  bool accent = hard || (g.step % 8 == 0);
  // The stick's base octave plus whatever SHIFT is adding right now. Recorded
  // as the total, so a loop plays back at the register you played it in even
  // after you move the stick.
  int oct = S.octShift + octBump;
  playMelody(deg, g.lead, accent, oct, sp);
  if (recording && recBuf.n < MAX_EVENTS) {
    LoopEvent e = { (uint8_t)g.abs, (int8_t)deg, accent, (int8_t)oct, sp };
    recBuf.ev[recBuf.n++] = e;
  }
}


static int shiftTarget(int dir) {
  int cur = (S.pendingDegree >= 0) ? S.pendingDegree : S.degree;
  int i = -1;
  for (int k = 0; k < 7; k++) if (BRIGHTNESS[k] == cur) { i = k; break; }
  int j = (i == -1 ? 1 : i) - dir;
  if (j < 0) j = 0;
  if (j > 6) j = 6;
  return BRIGHTNESS[j];
}


// +1 brighter, -1 darker. The change is written back into the progression, so
// the loop keeps it on every pass from here on -- you are editing the song,
// not nudging one bar.
static void shift(int dir) {
  int target = shiftTarget(dir);
  S.prog[S.progIdx] = (int8_t)target;
  S.pendingDegree   = (int8_t)target;
  S.degree          = (int8_t)target;
  if (S.running) { Grid g = nextGrid(true); playPad(target, g.lead); }
}


static void clearLoops() {
  loopCount = 0; recording = false; recArmed = false; recBars = 0; recBuf.n = 0;
}


static void applyVibe(uint8_t idx) {
  const Vibe& v = VIBES[idx % VIBE_COUNT];
  S.vibeIdx = idx % VIBE_COUNT;
  S.root = v.root; S.scaleId = v.scale; S.bpm = v.bpm; S.swing = v.swing;
  S.jazzy = v.jazzy;
  for (int i = 0; i < 4; i++) { S.prog[i] = v.prog[i]; S.baseProg[i] = v.prog[i]; }
  S.progIdx = 3; S.degree = v.prog[0]; S.pendingDegree = -1;
  S.padTone = v.pad; S.leadTone = v.lead;
  S.octShift = 0; S.energyLock = false; S.energyTarget = 0;
  S.octLocked = false; S.bassPedal = -1;
  padVoiced = false;
  setDelayTime();
}


static void resetAll() {
  clearLoops();
  S.octShift = 0; S.octLocked = false; S.bassPedal = -1;
  S.energyLock = false; S.energyTarget = 0; S.energy = 0;
  for (int i = 0; i < 4; i++) S.prog[i] = S.baseProg[i];
  S.progIdx = 0; S.degree = S.prog[0]; S.pendingDegree = -1;
  padVoiced = false;
  hitLogN = 0; sparse = true;
}


// ========================================================= CONTROL QUEUE ====
// Core 1 reads the hardware and posts one of these; core 0 is the only thing
// that ever touches the song state. That is the whole concurrency design --
// no mutexes anywhere, because there is only one writer.




QueueHandle_t ctlQ = nullptr;


static void post(uint8_t type, int8_t a) {
  if (!ctlQ) return;
  CtlMsg m = { type, a };
  xQueueSend(ctlQ, &m, 0);
}


static void handleCtl(const CtlMsg& m) {
  switch (m.type) {
    // Notes play whether or not the transport is running -- with the track
    // stopped the box is just a seven-note instrument, which is a perfectly
    // good thing to hand someone.
    //
    // The argument packs three things: the degree in the low bits, 0x40 for
    // "shift was held, play it an octave up", 0x80 for an accent.
    case C_MEL:
      hitMelody(m.a & 0x07, (m.a & 0x80) != 0, (m.a & 0x40) ? 1 : 0);
      break;
    case C_DARK:   shift(-1); break;
    case C_BRIGHT: shift(+1); break;
    // The stick steps the base octave and leaves it there. Clamped rather
    // than wrapped: wrapping from the top back to the bottom mid-phrase is
    // never what you meant, and playMelody would fold the note back anyway.
    case C_OCT_STEP: {
      if (S.octLocked) break;               // SHIFT + stick froze it
      int n = S.octShift + m.a;
      if (n < -2) n = -2;
      if (n >  2) n =  2;
      S.octShift = (int8_t)n;
      break;
    }
    case C_OCT_LOCK: S.octLocked = !S.octLocked; break;


    // Holding a note pedals the bass on that degree. Holding it again lets go
    // -- one gesture to set and the identical gesture to clear, which is the
    // only kind of toggle anyone remembers.
    case C_BASS_PEDAL:
      S.bassPedal = (S.bassPedal == m.a) ? -1 : m.a;
      break;


    // Tempo, from a SHIFT + drag along row 1 or the Tempo line in the menu.
    // The delay is a dotted eighth of the tempo, so it has to be retimed or
    // the echoes stop lining up.
    case C_BPM_STEP: {
      float b = S.bpm + m.a * BPM_PER_STEP;
      if (b < BPM_MIN) b = BPM_MIN;
      if (b > BPM_MAX) b = BPM_MAX;
      S.bpm = b;
      setDelayTime();
      break;
    }
    case C_ENERGY: if (!S.energyLock) S.energyTarget = m.a ? 3.0f : 0.0f; break;
    // Locking pins energy where it is; unlocking hands it straight back to the
    // pad, so letting go means it starts falling immediately.
    case C_LOCK:
      S.energyLock = !S.energyLock;
      S.energyTarget = S.energyLock ? S.energy : 0.0f;
      break;
    case C_REC:
      if (recording || recArmed) { recording = false; recArmed = false; recBuf.n = 0; }
      else recArmed = true;
      break;
    case C_RESET:  resetAll(); break;
    case C_VIBE:
      clearLoops();
      applyVibe((S.vibeIdx + (m.a >= 0 ? 1 : VIBE_COUNT - 1)) % VIBE_COUNT);
      break;
    case C_RUN:
      S.running = !S.running;
      if (S.running) {
        curStep = 0; absStep = 0;
        nextStepSample = samplePos + 64;   // first step ~2 ms out, so the pad
        S.progIdx = 3;                     // lands together with the drums
        S.pendingDegree = -1;
      }
      break;
  }
}


// ============================================================ I2S LAYER =====
// Arduino-ESP32 3.x replaced driver/i2s.h with the ESP_I2S wrapper. Both paths
// are here so the sketch compiles on whichever core is installed.
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  #include <ESP_I2S.h>
  static I2SClass i2s;


  static bool audioBegin() {
    i2s.setPins(PIN_I2S_BCLK, PIN_I2S_LRC, PIN_I2S_DIN);
    return i2s.begin(I2S_MODE_STD, SR, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  }
  static void audioWrite(int16_t* buf, size_t frames) {
    i2s.write((uint8_t*)buf, frames * 2 * sizeof(int16_t));
  }
#else
  #include <driver/i2s.h>


  static bool audioBegin() {
    i2s_config_t cfg = {
      .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
      .sample_rate          = SR,
      .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags     = 0,
      .dma_buf_count        = 6,
      .dma_buf_len          = 256,
      .use_apll             = false,
      .tx_desc_auto_clear   = true,
      .fixed_mclk           = 0
    };
    if (i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL) != ESP_OK) return false;
    i2s_pin_config_t pins = {
      .mck_io_num   = I2S_PIN_NO_CHANGE,
      .bck_io_num   = PIN_I2S_BCLK,
      .ws_io_num    = PIN_I2S_LRC,
      .data_out_num = PIN_I2S_DIN,
      .data_in_num  = I2S_PIN_NO_CHANGE
    };
    return i2s_set_pin(I2S_NUM_0, &pins) == ESP_OK;
  }
  static void audioWrite(int16_t* buf, size_t frames) {
    size_t written;
    i2s_write(I2S_NUM_0, buf, frames * 2 * sizeof(int16_t), &written, portMAX_DELAY);
  }
#endif


// Counts I2S buffer underruns. Written by the audio task on core 0, reported
// from loop() on core 1 -- a printf inside the task would add exactly the
// jitter it is trying to measure.
volatile uint32_t gUnderruns = 0;
volatile int      gStep      = 0;    // the step currently sounding, for the OLED


// How much of the available time the synth actually spends rendering, 0..100.
// This exists to answer one question when the speaker sounds rough: is it the
// software or the hardware? If this is comfortably under 100 and the underrun
// count is not moving, the audio pipeline is keeping up and whatever you are
// hearing is analogue -- the amp's supply, the grounding, or the speaker being
// asked for bass it cannot make. Look there instead of at this file.
volatile uint32_t gLoadPct  = 0;
volatile uint32_t gVoicePk  = 0;     // most voices in use since the last report


// ============================================================ AUDIO TASK ====
// Everything musical happens here, on core 0, driven by nothing but the sample
// clock. Core 1 does the I2C, the pads and the screen, and cannot stall it.


static void audioTask(void*) {
  static int16_t out[I2S_FRAMES * 2];
  float dry[BLOCK], revS[BLOCK], dlyS[BLOCK];


  for (;;) {
    uint32_t renderStart = micros();
    int o = 0;
    for (int b = 0; b < I2S_FRAMES / BLOCK; b++) {
      // Drained per block, not per buffer. Draining once every 8 ms would put
      // a whole buffer of avoidable latency between the pad and the note; per
      // block it is 1 ms, and an empty queue check costs nothing.
      CtlMsg m;
      while (xQueueReceive(ctlQ, &m, 0) == pdTRUE) handleCtl(m);


      // ---- sequencer: fire any grid line that falls inside this block ------
      if (S.running) {
        uint32_t sd = stepSamples();
        int guard = 0;
        while (nextStepSample < samplePos + BLOCK && guard++ < 8) {
          float lead = (nextStepSample > samplePos)
                     ? (float)(nextStepSample - samplePos) / SR : 0.0f;
          gStep = curStep;
          scheduleStep(curStep, lead, absStep);
          nextStepSample += sd;
          curStep = (curStep + 1) % 16;
          absStep = (absStep + 1) % LOOP_STEPS;
        }
      }


      memset(dry,  0, sizeof(dry));
      memset(revS, 0, sizeof(revS));
      memset(dlyS, 0, sizeof(dlyS));
      renderVoices(dry, revS, dlyS, BLOCK);


      for (int i = 0; i < BLOCK; i++) {
        float d = delayTick(dlyS[i]);
        float r = reverbTick(revS[i] + d);     // repeats get wetter as they fade
        float s = masterTick(dry[i] + d + r * 0.9f);
        int32_t q = (int32_t)(s * 32767.0f);
        if (q >  32767) q =  32767;
        if (q < -32768) q = -32768;
        out[o++] = (int16_t)q;                 // the MAX98357A sums L+R, so the
        out[o++] = (int16_t)q;                 // same sample goes in both slots
      }
      samplePos += BLOCK;
    }


    // How long that buffer took to build, against how long it will take to
    // play. Anything approaching 100% means the synth cannot keep up and the
    // DMA is about to starve -- turn NVOICE down if you ever see it.
    uint32_t budget = 1000000UL * I2S_FRAMES / SR;      // 8000 us at 32 kHz
    uint32_t spent  = micros() - renderStart;
    gLoadPct = (spent * 100) / budget;
    uint32_t nv = 0;
    for (int i = 0; i < NVOICE; i++) if (voices[i].active) nv++;
    if (nv > gVoicePk) gVoicePk = nv;


    // Time the write, because it doubles as an underrun detector. A healthy
    // pipeline always has the DMA busy, so this blocks for roughly one
    // buffer's worth of time. If it returns nearly instantly, the DMA had
    // already run dry -- which is what grainy audio sounds like when the cause
    // is software rather than power.
    uint32_t t0 = micros();
    audioWrite(out, I2S_FRAMES);
    uint32_t dt = micros() - t0;
    if (dt < budget / 4) gUnderruns = gUnderruns + 1;
  }
}


// ============================================================= AMP GATE =====
// A powered MAX98357A hisses even on digital silence -- that is the amp's own
// noise floor. The task feeds silence non-stop to keep the DMA alive, so the
// amp is gated on the transport instead: on while the track runs, and dropped
// a beat and a half after it stops so the reverb tail is not cut off.
static const uint16_t AMP_TAIL_MS = 1500;
uint32_t ampOffAt = 0;
bool     ampOn    = false;


static void ampSet(bool on) {
  if (on == ampOn) return;
  ampOn = on;
  digitalWrite(PIN_AMP_SD, (on && speakerWanted()) ? HIGH : LOW);
}


static void ampService() {
  if (S.running) { ampOffAt = 0; ampSet(speakerWanted()); return; }
  if (ampOn && ampOffAt == 0) ampOffAt = millis() + AMP_TAIL_MS;
  if (ampOn && (int32_t)(millis() - ampOffAt) >= 0) { ampSet(false); ampOffAt = 0; }
}


// The little speaker starts to crackle well before the volume table runs out --
// the amp and driver clip, not the mix. So while the speaker is an output, the
// top of the volume range is capped at this fraction of full scale. The menu
// still shows 1..8, the top step is just quieter. Raise it if you want more
// level and can live with the crackle; lower it if it still scratches.
static const float SPEAKER_VOL_CEIL = 1.00f;


static void applyVolume() {
  float v = VOL_LEVELS[vol];
  if (speakerWanted()) v *= SPEAKER_VOL_CEIL;
  gVolume = v;
}


static void applyOutput() {
  applyVolume();
  if (!speakerWanted()) { digitalWrite(PIN_AMP_SD, LOW); ampOn = false; }
  else if (ampOn)       digitalWrite(PIN_AMP_SD, HIGH);
  if (PIN_DAC_XSMT >= 0) digitalWrite(PIN_DAC_XSMT, phonesWanted() ? HIGH : LOW);


  // Protect the speaker whenever it is one of the outputs. Headphones alone
  // get the full-range mix -- a PCM5102 into a pair of cans has no trouble
  // with 44 Hz, and that is where the kick is supposed to be felt.
  hpfOn = (SPEAKER_HPF_HZ > 0.0f) && speakerWanted();
}


// ============================================================ I2C / MPR121 ==
bool mprWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPR_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}


// endTransmission(false) = repeated START. A STOP here makes the MPR121 reset
// its address pointer to 0x00 and every read silently returns touch status.
int mprRead16(uint8_t reg) {
  Wire.beginTransmission(MPR_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return -1;
  if (Wire.requestFrom((int)MPR_ADDR, 2) != 2) return -1;
  uint8_t lo = Wire.read();
  uint8_t hi = Wire.read();
  return lo | (hi << 8);
}


int mprRead8(uint8_t reg) {
  Wire.beginTransmission(MPR_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return -1;
  if (Wire.requestFrom((int)MPR_ADDR, 1) != 1) return -1;
  return Wire.read();
}


// How hard the pad was pressed, in the MPR121's own counts: how far the
// filtered reading has been pulled below the tracked baseline. The baseline
// register holds the top 8 bits of a 10-bit value, hence the shift.
static int padDeflection(uint8_t e) {
  int filt = mprRead16(R_FILTERED + e * 2);
  int base = mprRead8(R_BASELINE + e);
  if (filt < 0 || base < 0) return 0;
  int d = (base << 2) - filt;
  return d < 0 ? 0 : d;
}


bool mprInit() {
  if (!mprWrite(R_SOFTRESET, 0x63)) return false;
  delay(10);
  mprWrite(R_ECR, 0x00);                    // stop mode -- required to configure


  mprWrite(0x2B, 0x01); mprWrite(0x2C, 0x01);   // baseline filter, rising
  mprWrite(0x2D, 0x0E); mprWrite(0x2E, 0x00);
  mprWrite(0x2F, 0x01); mprWrite(0x30, 0x05);   // falling
  mprWrite(0x31, 0x01); mprWrite(0x32, 0x00);
  mprWrite(0x33, 0x00); mprWrite(0x34, 0x00); mprWrite(0x35, 0x00);  // touched


  for (uint8_t e = 0; e < 12; e++) {
    mprWrite(R_TOUCH_TH(e), TOUCH_TH);
    mprWrite(R_REL_TH(e),   RELEASE_TH);
  }


  mprWrite(R_DEBOUNCE, 0x00);
  mprWrite(R_CONFIG1,  0x10);               // 16 uA charge current
  mprWrite(R_CONFIG2,  0x20);               // 0.5 us charge, 4 samples, 1 ms
  mprWrite(R_ECR,      0x8F);               // baseline tracking on + 12 electrodes
  delay(50);                                // let it self-calibrate


  return true;
}


// ============================================================== BUTTONS =====
// One struct per push button so both behave identically: the integrator count
// plus the debounced level. The struct itself is up in TYPES, along with the
// two instances -- see the note there for why.


// The integrator has to cross its whole range to change the state, so bounce
// cannot flip it: a burst of chatter drives the count part way and the reads
// going the other way pull it straight back. That is the property two earlier
// attempts were missing -- both asked for a *clean run* of one level, and a
// chattering contact never gives you one.
static bool btnPoll(Button& b) {
  bool raw = (digitalRead(b.pin) == HIGH);
  bool on  = (raw == (b.active == HIGH));


  if (on) { if (b.integ < BTN_INTEGRATOR) b.integ++; }
  else    { if (b.integ > 0)              b.integ--; }


#if BTN_DEBUG
  if (!b.dbgSeen || raw != b.dbgRaw) {
    b.dbgSeen = true; b.dbgRaw = raw; b.dbgFlips++;
    Serial.printf("  btn%2d raw %s  t=%lu\n", b.pin, raw ? "HIGH" : "LOW ",
                  (unsigned long)millis());
  }
#endif


  if (!b.down && b.integ >= BTN_INTEGRATOR) {
    b.down = true;
#if BTN_DEBUG
    Serial.printf("btn%2d press    (%u raw flips)\n", b.pin, b.dbgFlips); b.dbgFlips = 0;
#endif
    return true;
  }
  if (b.down && b.integ == 0) {
    b.down = false;
#if BTN_DEBUG
    Serial.printf("btn%2d release  (%u raw flips)\n", b.pin, b.dbgFlips); b.dbgFlips = 0;
#endif
    if (BTN_LATCHING) return true;
  }
  return false;
}


// Work out which level means "pressed" and leave the pin in the matching mode.
// DON'T HOLD A BUTTON AT POWER-UP -- like the touch pads and the joystick
// centre, this is measured once and a held button reads as the resting state.
static uint8_t btnDetect(int pin, const char** how) {
  if (BTN_ACTIVE_LEVEL >= 0) {
    pinMode(pin, BTN_ACTIVE_LEVEL ? INPUT_PULLDOWN : INPUT_PULLUP);
    *how = "forced";
    return BTN_ACTIVE_LEVEL ? HIGH : LOW;
  }
  pinMode(pin, INPUT_PULLUP);  delay(2);
  bool highOnPullup = (digitalRead(pin) == HIGH);
  pinMode(pin, INPUT_PULLDOWN); delay(2);
  bool highOnPulldown = (digitalRead(pin) == HIGH);
  Serial.printf("  GPIO %d: pu=%d pd=%d\n", pin, highOnPullup, highOnPulldown);


  if (highOnPulldown) { pinMode(pin, INPUT_PULLUP); *how = "ext pull-up";   return LOW; }
  if (!highOnPullup)  {                             *how = "ext pull-down"; return HIGH; }
  pinMode(pin, INPUT_PULLUP);                       *how = "bare switch";   return LOW;
}


// Volume moved off the button and onto the strip, so it steps by a signed
// amount now and clamps at both ends instead of wrapping. Wrapping was fine
// for a button you tap round in a circle; on a wheel, sliding up past the top
// and landing back at silent would be alarming.
static void stepVolume(int d) {
  int v = (int)vol + d;
  if (v < 0) v = 0;
  if (v > VOL_STEPS - 1) v = VOL_STEPS - 1;
  if (v == (int)vol) return;
  vol = (uint8_t)v;
  applyVolume();
  Serial.printf("volume -> %u/%u (%.2f)\n", vol + 1, VOL_STEPS, VOL_LEVELS[vol]);
}


// Output is a menu line now rather than a hidden combo, so it steps in
// whichever direction you pushed the stick instead of only cycling forwards.
static void stepOutput(int d) {
  outMode = (OutMode)(((int)outMode + d + 3) % 3);
  applyOutput();
  Serial.printf("output -> %s\n", OUT_NAME[outMode]);
}


// ============================================================== SETTINGS ====
// The four things you set once and then leave alone, on a screen that names
// them. Every one of these was already reachable as a held combo, and a combo
// is something you either know or you don't -- there was no way to find one
// out from the box itself. A menu costs one button and makes the whole set
// discoverable, which is worth more than the button it took.
//
// The box keeps playing while the menu is open. That is deliberate: volume,
// tempo and theme are all judged by ear, and a menu that silences the thing
// you are adjusting is a menu you have to close before you can tell.


enum SetItem : uint8_t { SET_VOL, SET_THEME, SET_TEMPO, SET_OUT, SET_COUNT };
static const char* SET_LABEL[SET_COUNT] = { "Volume", "Theme", "Tempo", "Output" };


bool    setOpen = false;
uint8_t setItem = 0;


static void setToggle() {
  setOpen = !setOpen;
  Serial.println(setOpen ? "settings: open" : "settings: closed");
}


// Left / right on the highlighted line. Volume and output are core-1 state and
// change here; theme and tempo belong to the audio task, so they go through
// the queue like every other control does.
static void setAdjust(int d) {
  switch (setItem) {
    case SET_VOL:    stepVolume(d);               break;
    case SET_THEME:  post(C_VIBE,      (int8_t)d); break;
    case SET_TEMPO:  post(C_BPM_STEP,  (int8_t)d); break;
    case SET_OUT:    stepOutput(d);               break;
    default: break;
  }
}


static void setMove(int d) {
  setItem = (uint8_t)((setItem + d + SET_COUNT) % SET_COUNT);
}


static void btnInit() {
  const char* sHow;
  const char* mHow;
  btnShift.active = btnDetect(PIN_BTN_SHIFT, &sHow);
  btnSet.active   = btnDetect(PIN_BTN_SET,   &mHow);


  if (digitalRead(PIN_BTN_SHIFT) == btnShift.active) { btnShift.integ = BTN_INTEGRATOR; btnShift.down = true; }
  if (digitalRead(PIN_BTN_SET)   == btnSet.active)   { btnSet.integ   = BTN_INTEGRATOR; btnSet.down   = true; }


  Serial.printf("Buttons: SHIFT(GPIO %d) active %s (%s), SETTINGS(GPIO %d) active %s (%s)\n",
                PIN_BTN_SHIFT, btnShift.active == HIGH ? "HIGH" : "LOW", sHow,
                PIN_BTN_SET,   btnSet.active   == HIGH ? "HIGH" : "LOW", mHow);
}


// One held modifier and one menu button. SHIFT fires nothing of its own --
// btnPoll keeps its debounced level up to date and the pads, the stick and the
// screen all just read it. SETTINGS is the opposite: it is only ever an edge,
// and the edge toggles the menu.
//
// That split is the point of the redesign. A modifier multiplies what the pads
// already do, so it is worth holding; the things you set and leave alone don't
// want a modifier at all, they want to be listed somewhere you can read them.
bool shiftHeld = false;      // read by padPress, joyService and draw


static void btnService() {
  // Fixed cadence, not "every time loop() comes round". loop() spins in
  // microseconds, so sampling every pass would let one bounce fill the
  // integrator and the debounce would be worthless.
  static uint32_t nextSample = 0;
  uint32_t now = millis();
  if ((int32_t)(now - nextSample) < 0) return;
  nextSample = now + BTN_SAMPLE_MS;


  btnPoll(btnShift);
  shiftHeld = btnShift.down;


  if (btnPoll(btnSet)) setToggle();
}


// ============================================================= JOYSTICK =====
int joyCenterX = 2048, joyCenterY = 2048;


// Centre is sampled at boot, so DON'T hold the stick while it powers up -- the
// offset would be baked in and that direction would sit permanently deflected.
static void joyInit() {
  pinMode(PIN_JOY_SW, INPUT_PULLUP);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);


  long sx = 0, sy = 0;
  for (int i = 0; i < 16; i++) {
    sx += analogRead(PIN_JOY_X);
    sy += analogRead(PIN_JOY_Y);
    delay(4);
  }
  joyCenterX = sx / 16;
  joyCenterY = sy / 16;


  joyOk = (joyCenterX > 200 && joyCenterX < 3900 &&
           joyCenterY > 200 && joyCenterY < 3900);
  Serial.printf("Joystick centre: x=%d y=%d  %s\n", joyCenterX, joyCenterY,
                joyOk ? "ok" : "-- out of range, check VRx/VRy wiring");
}


static int8_t joyAxis(int raw, int center, bool invert) {
  int d = raw - center;
  if (invert) d = -d;
  if (d >  JOY_DEADZONE) return  1;
  if (d < -JOY_DEADZONE) return -1;
  return 0;
}


static void joyService() {
  if (!joyOk) return;


  static uint32_t lastPoll = 0;
  uint32_t now = millis();
  if (now - lastPoll < 20) return;
  lastPoll = now;


  int8_t nx = joyAxis(analogRead(PIN_JOY_X), joyCenterX, JOY_INVERT_X);
  int8_t ny = joyAxis(analogRead(PIN_JOY_Y), joyCenterY, JOY_INVERT_Y);


  // A reading only becomes a direction once it has held still for
  // JOY_SETTLE_MS. Without it a diagonal fires as a sideways step and *then* a
  // diagonal one -- two actions where you meant one.
  static int8_t   dirX = 0, dirY = 0, pendX = 0, pendY = 0;
  static uint32_t settleAt = 0, repeatAt = 0;


  if (nx != pendX || ny != pendY) { pendX = nx; pendY = ny; settleAt = now + JOY_SETTLE_MS; }


  bool settled = (pendX == dirX && pendY == dirY);
  bool step = false, isRepeat = false;
  if (!settled && (int32_t)(now - settleAt) >= 0) {
    dirX = pendX; dirY = pendY;
    step = (dirX || dirY);
    repeatAt = now + JOY_REPEAT_DELAY;
  } else if (settled && dirX && (int32_t)(now - repeatAt) >= 0) {
    step = true; isRepeat = true;                 // only X repeats -- see below
    repeatAt = now + JOY_REPEAT_RATE;
  }


  if (step) {
    // With the menu open the stick belongs to the menu and nothing else: left
    // and right change the highlighted line, up and down pick a different one.
    // Chord and octave keep their gestures for when the menu is shut, so
    // nothing here has to be unlearned -- the stick simply points at whatever
    // is on the screen.
    if (setOpen) {
      if (dirX)                    setAdjust(dirX > 0 ? +1 : -1);
      else if (!isRepeat && dirY)  setMove(dirY < 0 ? -1 : +1);
    }
    else if (dirX) post(dirX > 0 ? C_BRIGHT : C_DARK, 0);
    // Up and down step the octave, and deliberately do NOT repeat: the range
    // is only four steps wide, so holding the stick up would hit the ceiling
    // instantly and the repeat would do nothing but feel broken. dirY is
    // negative for up, which is why the sign flips here.
    // SHIFT + a nudge freezes the octave where it is rather than moving it, so
    // the gesture to pin it is the same one you use to set it.
    else if (!isRepeat && dirY) {
      if (shiftHeld) post(C_OCT_LOCK, 0);
      else           post(C_OCT_STEP, dirY < 0 ? +1 : -1);
    }
  }


  // Button: tap = arm the recorder, hold = reset everything. Reset is behind
  // the hold on purpose -- it throws away every loop you have stacked. In the
  // menu both are just "close", so a stick press always gets you back to
  // playing whichever way you press it.
  bool down = (digitalRead(PIN_JOY_SW) == LOW);
  static bool     wasDown = false, holdDone = false;
  static uint32_t downAt = 0;


  if (down && !wasDown) { downAt = now; holdDone = false; }
  else if (down && !holdDone && now - downAt >= HOLD_MS) {
    holdDone = true;
    // Reset while the menu is open would be a nasty surprise -- you are half a
    // second into a hold you meant as a click to get out. Closing is the only
    // thing the stick button does in the menu.
    if (setOpen) setOpen = false;
    else         post(C_RESET, 0);
  } else if (!down && wasDown && !holdDone && now - downAt >= 25) {
    if (setOpen) setOpen = false;
    else         post(C_REC, 0);
  }
  wasDown = down;
}


// ================================================================ DISPLAY ===
// 128x64 is not much, so the screen shows only what changes while you play:
// the chord you are on, the four bars with the current one lit, where the bar
// is, how much energy is built, and which of the seven notes just sounded.
// Everything static -- the pad legend -- is printed on the box, not the OLED.


uint16_t lastTouched = 0;


// Whether the press currently down on the energy pad was a SHIFT-modified one.
// Without this, shift-tapping it and then lifting your finger would send the
// plain release and hand energy straight back to the pad -- the lock would
// appear to last exactly as long as you kept touching the pad.
bool energyPressLocked = false;


// The value column for the settings screen. Volume gets a bar as well as a
// number, because "5/8" means nothing next to something you can see filling.
static void setValue(uint8_t item, char* out, size_t n) {
  switch (item) {
    case SET_VOL:   snprintf(out, n, "%u/%u", vol + 1, VOL_STEPS);        break;
    case SET_THEME: snprintf(out, n, "%s", VIBES[S.vibeIdx].name);        break;
    case SET_TEMPO: snprintf(out, n, "%d bpm", (int)(S.bpm + 0.5f));      break;
    case SET_OUT:   snprintf(out, n, "%s", OUT_NAME[outMode]);            break;
    default:        out[0] = 0;                                          break;
  }
}


// Four lines, the highlighted one inverted, arrows on it so the stick gesture
// is written on the screen rather than remembered. The footer says how to get
// out, because the one thing worse than a hidden control is a menu you cannot
// tell how to leave.
static void drawSettings() {
  char val[20];


  oled.clearBuffer();


  oled.setFont(u8g2_font_4x6_tr);
  oled.drawStr(0, 6, "SETTINGS");
  oled.drawStr(128 - oled.getStrWidth("stick picks"), 6, "stick picks");
  oled.drawHLine(0, 8, 128);


  for (uint8_t i = 0; i < SET_COUNT; i++) {
    int y   = 11 + i * 11;          // row top
    int ty  = y + 7;                // text baseline
    bool cur = (i == setItem);


    if (cur) oled.drawBox(0, y, 128, 10);
    oled.setDrawColor(cur ? 0 : 1);


    oled.drawStr(3, ty, SET_LABEL[i]);
    setValue(i, val, sizeof(val));


    if (i == SET_VOL) {
      // Bar first, number after it, both right-aligned as one group.
      int bw = 26;
      int bx = 128 - 4 - bw;
      oled.drawFrame(bx, y + 2, bw, 6);
      int fill = (vol + 1) * (bw - 4) / VOL_STEPS;
      if (fill > 0) oled.drawBox(bx + 2, y + 4, fill, 2);
      oled.drawStr(bx - 4 - oled.getStrWidth(val), ty, val);
    } else {
      oled.drawStr(128 - 4 - oled.getStrWidth(val), ty, val);
    }


    // The arrows only appear on the line you are on, so there is never a
    // question about which one left/right is about to change.
    if (cur) {
      oled.drawStr(44, ty, "<");
      oled.drawStr(51, ty, ">");
    }
    oled.setDrawColor(1);
  }


  oled.drawHLine(0, 57, 128);
  oled.drawStr(0, 63, "SET or stick tap = back");


  oled.sendBuffer();
}


static void draw() {
  if (!oledOk) return;
  if (setOpen) { drawSettings(); return; }


  char buf[24], buf2[16];
  oled.clearBuffer();


  // ---- chord, big, top left ------------------------------------------------
  chordLabel(S.degree, buf, sizeof(buf));
  oled.setFont(u8g2_font_helvB12_tr);
  oled.drawStr(0, 12, buf);


  // ---- roman numeral + key, small, top right -------------------------------
  oled.setFont(u8g2_font_4x6_tr);
  romanLabel(S.degree, buf2, sizeof(buf2));
  oled.drawStr(128 - oled.getStrWidth(buf2), 5, buf2);
  // Tempo goes here because SHIFT + drag can change it, and a control with no
  // readout is a control you cannot use deliberately.
  snprintf(buf, sizeof(buf), "%s %s %d", noteName(S.root), SCALE_NAME[S.scaleId],
           (int)(S.bpm + 0.5f));
  oled.drawStr(128 - oled.getStrWidth(buf), 12, buf);


  oled.drawHLine(0, 14, 128);


  // ---- the four bars -------------------------------------------------------
  for (int i = 0; i < 4; i++) {
    int x = i * 32;
    bool cur = (i == S.progIdx);
    if (cur) oled.drawBox(x, 16, 30, 10);
    else     oled.drawFrame(x, 16, 30, 10);
    chordLabel(S.prog[i], buf, sizeof(buf));
    oled.setDrawColor(cur ? 0 : 1);
    int tw = oled.getStrWidth(buf);
    oled.drawStr(x + (30 - tw) / 2, 24, buf);
    oled.setDrawColor(1);
  }


  // ---- where the bar is ----------------------------------------------------
  int st = gStep;
  for (int i = 0; i < 16; i++) {
    int x = i * 8 + 3;
    int h = (i % 4 == 0) ? 5 : 3;
    if (i == st) oled.drawBox(x - 1, 28, 3, 5);
    else         oled.drawVLine(x, 28 + (5 - h), h);
  }


  // ---- energy, and what the recorder is doing ------------------------------
  oled.drawFrame(0, 35, 58, 7);
  int w = (int)(S.energy / 3.0f * 54.0f);
  if (w > 54) w = 54;
  if (w > 0) oled.drawBox(2, 37, w, 3);
  if (S.energyLock) oled.drawStr(60, 41, "LK");


  if      (recArmed)  snprintf(buf, sizeof(buf), "ARMED");
  else if (recording) snprintf(buf, sizeof(buf), "REC %u/4", recBars + 1);
  else                snprintf(buf, sizeof(buf), "%u lay", loopCount);
  oled.drawStr(74, 41, buf);


  const char* dens = sparse ? "spr" : "dns";
  oled.drawStr(128 - oled.getStrWidth(dens), 41, dens);


  // ---- the modifier line ---------------------------------------------------
  // While a modifier is held this line stops reporting state and starts
  // telling you what the pads will do instead. That is the difference between
  // a box you have to memorise and a box you can work out while holding it --
  // and it costs nothing, because the line is only status the rest of the time.
  if (shiftHeld) {
    oled.drawStr(0, 48, "SHIFT thm-thm+ lock rset stk=frz");
  } else {
    oled.drawStr(0, 48, VIBES[S.vibeIdx].name);
    // The stick's base octave, signed, with a dot after it while it is frozen.
    if (S.octShift || S.octLocked) {
      snprintf(buf, sizeof(buf), "%+d%s", S.octShift, S.octLocked ? "*" : "");
      oled.drawStr(48, 48, buf);
    }
    // Which degree the bass is pedalling on, if any.
    if (S.bassPedal >= 0) {
      snprintf(buf, sizeof(buf), "P%d", S.bassPedal + 1);
      oled.drawStr(66, 48, buf);
    }
    if (!S.running) oled.drawStr(80, 48, "STOP");


    oled.drawStr(106 - oled.getStrWidth(OUT_NAME[outMode]), 48, OUT_NAME[outMode]);
    oled.drawFrame(108, 42, 20, 6);
    oled.drawBox(110, 43, (vol + 1) * 16 / VOL_STEPS, 4);
  }


  // ---- the seven notes -----------------------------------------------------
  // Lit while the pad is held, and flashed when a recorded loop plays that
  // note back at you -- so a stacked loop is something you can watch as well
  // as hear. The spice notes get a dotted top edge, same as the dashed violet
  // pads on the web version.
  uint32_t now = millis();
  for (int d = 0; d < 7; d++) {
    int x = d * 18 + 1, y = 50, cw = 16, ch = 13;
    // Which physical pad plays this degree -- NOT the degree number. The two
    // stopped matching when the notes moved to electrodes 8,5,9,6,10,7,11.
    bool held  = (lastTouched >> DEGREE_PAD[d]) & 1;
    bool flash = (now - gMelFlash[d]) < 160;
    if (held || flash) oled.drawBox(x, y, cw, ch);
    else               oled.drawFrame(x, y, cw, ch);


    oled.setDrawColor((held || flash) ? 0 : 1);
    if (isSpice(d)) for (int px = x + 2; px < x + cw - 2; px += 2) oled.drawPixel(px, y + 2);
    int note = (60 + S.root + degSemi(d)) % 12;
    const char* nm = noteName(note);
    oled.drawStr(x + (cw - oled.getStrWidth(nm)) / 2, y + 11, nm);
    oled.setDrawColor(1);
  }


  oled.sendBuffer();
}


// ================================================================ TOUCH =====
// Row 1 has to be two things at once: five buttons and one scroll wheel. The
// whole difficulty is telling a tap from a drag without making taps feel late.
//
// The rule: a touch is a TAP until a second strip pad joins in. So the four
// discrete pads (darker, brighter, record, stop) fire on RELEASE and only if
// no drag happened -- you cannot know a touch was a tap until it ends, and
// waiting until release costs nothing on controls that are quantised to the
// bar anyway. Energy is the exception: it fires on PRESS so "hold to build"
// still feels like holding, and a drag that starts on it simply cancels it.
// Energy glides at 6% per 16th, so the ~50 ms of ramp that a drag picks up on
// the way past is far too small to hear.


// Where in the strip a given electrode sits, or -1 if it is not on the strip.
static int8_t stripIndex(uint8_t e) {
  for (uint8_t i = 0; i < STRIP_N; i++) if (STRIP[i] == e) return (int8_t)i;
  return -1;
}


// Where the drag is right now, and where it started.
int8_t   stripLastIdx = -1;      // strip position of the most recent touch
uint8_t  stripDownPad = 0;       // the pad a potential tap is waiting on
bool     stripDownValid = false; // ...and whether that tap is still possible
bool     stripDragging  = false; // a drag has begun; suppress the tap
uint32_t stripLastMs = 0;


// A drag step: one pad of travel. Positive is towards the high end of STRIP.
static void stripScroll(int delta) {
  if (shiftHeld) post(C_BPM_STEP, (int8_t)delta);
  else           stepVolume(delta);
}


// The note pad a finger is currently resting on, and how long it has been
// there. Only one is tracked: pedalling is a deliberate one-finger gesture,
// and a chord of held notes should stay a chord, not a race to see which pad
// claims the bass.
uint8_t  holdPad   = 0xFF;
uint32_t holdSince = 0;
bool     holdFired = false;


static void padPress(uint8_t e) {
  int8_t deg = PAD_DEGREE[e];


  // ---- note pads -----------------------------------------------------------
  if (deg >= 0) {
    // A plain press starts the pedal timer. SHIFT does not, because SHIFT +
    // note is already the octave-up gesture and leaning on it is exactly how
    // that one is played -- the two would fight over the same finger.
    if (!shiftHeld) { holdPad = e; holdSince = millis(); holdFired = false; }


    bool hard = false;
    if (PRESSURE_ACCENT) {
      int d = padDeflection(e);
      hard = (d >= ACCENT_COUNTS);
#if TOUCH_DEBUG
      Serial.printf("e%-2u deg %d  deflection %d %s\n", e, deg, d,
                    hard ? "ACCENT" : "");
#endif
    }
    post(C_MEL, (int8_t)(deg | (shiftHeld ? 0x40 : 0) | (hard ? 0x80 : 0)));
    return;
  }


  // ---- the strip -----------------------------------------------------------
  int8_t idx = stripIndex(e);
  if (idx < 0) return;
  uint32_t now = millis();


  // A new gesture, or a continuation of the one still in progress?
  bool continues = (stripLastIdx >= 0) && (now - stripLastMs <= STRIP_GAP_MS);
  if (continues && idx != stripLastIdx) {
    if (!stripDragging) {
      stripDragging = true;
      // The touch that started this turned out to be part of a drag after all.
      // Cancel it: the tap is abandoned, and if it had started energy building
      // that gets handed back too.
      if (stripDownValid && stripDownPad == E_ENERGY && !energyPressLocked)
        post(C_ENERGY, 0);
      stripDownValid = false;
    }
    stripScroll(idx - stripLastIdx);
  } else {
    // Either a fresh gesture, or the same pad pressed again inside the window
    // -- which is what two quick taps on one pad look like. Both start a tap.
    // Only an in-progress drag suppresses it; without that check, tapping the
    // same pad twice in a row silently dropped the second one.
    if (!continues) stripDragging = false;
    if (!stripDragging) {
      stripDownPad   = e;
      stripDownValid = true;
      // Energy is the one control that acts immediately, because holding IS
      // the gesture.
      if (e == E_ENERGY) {
        energyPressLocked = shiftHeld;
        if (shiftHeld) post(C_LOCK, 0);
        else           post(C_ENERGY, 1);
      }
    }
  }


  stripLastIdx = idx;
  stripLastMs  = now;
}


// Called from loop() while a note pad is still down. The pedal fires here
// rather than on release, so it lands under your finger while it is still on
// the pad -- you hear the bass arrive and know the gesture took.
static void noteHoldService() {
  if (holdPad == 0xFF || holdFired) return;
  if (millis() - holdSince < PEDAL_HOLD_MS) return;
  holdFired = true;
  post(C_BASS_PEDAL, PAD_DEGREE[holdPad]);
}


static void padRelease(uint8_t e) {
  if (e == holdPad) holdPad = 0xFF;          // the pedal timer goes with it


  int8_t idx = stripIndex(e);
  if (idx < 0) return;                       // note pads have nothing else
  stripLastMs = millis();                    // keep the gesture window alive


  if (e == E_ENERGY && !stripDragging && !energyPressLocked) post(C_ENERGY, 0);


  // The tap fires here, and only if this touch never became a drag.
  if (!stripDragging && stripDownValid && stripDownPad == e) {
    switch (e) {
      case E_DARK:   if (shiftHeld) post(C_VIBE, -1); else post(C_DARK, 0);   break;
      case E_BRIGHT: if (shiftHeld) post(C_VIBE, +1); else post(C_BRIGHT, 0); break;
      case E_REC:    if (shiftHeld) post(C_RESET, 0); else post(C_REC, 0);    break;
      // SHIFT + e4 does nothing on purpose. Output moved to the settings
      // menu, and an unused modifier slot is better than a second, hidden way
      // to change something the menu already lists.
      case E_RUN:    if (!shiftHeld) post(C_RUN, 0);                           break;
      default: break;                        // E_ENERGY already acted on press
    }
  }
  stripDownValid = false;
}


// Called from loop(): once the whole strip has been clear for STRIP_GAP_MS the
// gesture is over, so the next touch starts a fresh one rather than being read
// as more drag.
static void stripService(uint16_t touched) {
  if (stripLastIdx < 0) return;
  for (uint8_t i = 0; i < STRIP_N; i++) if ((touched >> STRIP[i]) & 1) return;
  if (millis() - stripLastMs > STRIP_GAP_MS) {
    stripLastIdx  = -1;
    stripDragging = false;
  }
}


// ================================================================ SETUP =====
void setup() {
  // Never wait on the USB serial. Powered from the native USB port (which the
  // 5Vin jumper mod now requires) with a PC on the other end but no monitor
  // open, the host counts as "connected", so every print blocked until the
  // TX timeout -- the screen took ~30 s to come up and stopped following the
  // buttons. With a zero timeout, unread log text is simply dropped.
  Serial.setTxTimeoutMs(0);
  Serial.begin(115200);
  delay(400);
  Serial.println("\n=== Buttonbox ===");


  // Kill the onboard WS2812, in that order: send it an all-zero frame FIRST,
  // then park the line.
  //
  // Holding the pin LOW on its own does not work. LOW is the WS2812's reset
  // level -- it stops new data arriving, but the chip goes on displaying
  // whatever it last latched. A frame clocked in by noise during the float
  // window before setup() runs therefore survives being "parked" indefinitely.
  // The only thing that clears it is actually sending R=G=B=0.
  if (PIN_LED_ONBOARD >= 0) {
    neopixelWrite(PIN_LED_ONBOARD, 0, 0, 0);
    pinMode(PIN_LED_ONBOARD, OUTPUT);
    digitalWrite(PIN_LED_ONBOARD, LOW);
  }


  Wire.setPins(PIN_SDA, PIN_SCL);
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);        // 400 kHz: MPR121 is fine with it, OLED loves it


  if (mprInit()) Serial.println("MPR121 ready.");
  else           Serial.println("MPR121 NOT responding -- run I2C_Scanner.");


  oled.setI2CAddress(OLED_ADDR << 1);   // U8g2 wants the 8-bit form
  oledOk = oled.begin();
  Serial.println(oledOk ? "OLED ready." : "OLED NOT responding -- check address.");


  // Hold both outputs muted across I2S start-up -- that is when the pop happens.
  pinMode(PIN_AMP_SD, OUTPUT); digitalWrite(PIN_AMP_SD, LOW);
  if (PIN_DAC_XSMT >= 0) { pinMode(PIN_DAC_XSMT, OUTPUT); digitalWrite(PIN_DAC_XSMT, LOW); }


  for (int i = 0; i <= SINE_N; i++) sineTbl[i] = sinf(2.0f * 3.14159265f * i / SINE_N);
  for (int i = 0; i < NVOICE; i++) voices[i].active = false;
  fxClear();
  hpfInit();


  memset(&S, 0, sizeof(S));
  applyVibe(0);
  S.energy = 0; S.pendingDegree = -1;
  S.running = true;
  S.progIdx = 3;                      // so the first bar to land is prog[0]


  ctlQ    = xQueueCreate(24, sizeof(CtlMsg));
  audioOk = (ctlQ != nullptr) && audioBegin();
  if (audioOk) {
    // Priority 10, not 1. At 1 it tied with every ordinary task on core 0 --
    // including the USB CDC plumbing Serial output goes through -- so a burst
    // of logging could hold the CPU long enough for the I2S DMA to run dry.
    xTaskCreatePinnedToCore(audioTask, "audio", 8192, NULL, 10, NULL, 0);
    delay(20);                        // let the task feed a block of silence
    applyOutput();
    ampSet(true);
    Serial.printf("Audio ready at %lu Hz. Output: %s\n", (unsigned long)SR, OUT_NAME[outMode]);
  } else {
    Serial.println("Audio init FAILED -- check the I2S pins.");
    S.running = false;
  }


  joyInit();
  btnInit();


  Serial.println("Notes: e8 e5 e9 e6 e10 e7 e11, low to high.");
  Serial.println("SHIFT (GPIO 41) is held. SETTINGS (GPIO 7) is tapped: volume, theme, tempo, output.");
  Serial.println("Row 1 taps:  e0 darker  e1 brighter  e2 energy(hold)  e3 record  e4 stop/start");
  Serial.println("  + SHIFT:   prev theme next theme   lock energy      reset     --");
  Serial.println("Drag along row 1 = volume.  SHIFT + drag = tempo.");
  Serial.println("SHIFT + note = octave up while held.  HOLD a note = pedal the bass there.");
  Serial.println("Stick: up/down octave (SHIFT+ = freeze it), L/R chord, tap record, hold reset.");
  Serial.println("In the menu the stick picks and changes lines; tap it or SETTINGS to leave.");
  draw();
}


// ================================================================= LOOP =====
void loop() {
  ampService();
  joyService();
  btnService();


  // Report I2S underruns at most once a second, and only when the count moved.
  // Silence here while the speaker sounds grainy is the useful result: it means
  // the audio pipeline never starved, so the graininess is analogue -- supply,
  // grounding or the amp itself -- and no amount of code will move it.
  //
  // The load, the voice high-water mark and the limiter count go out with it,
  // once every two seconds, because together they say WHICH kind of grainy you
  // have:
  //   underruns climbing        -> the synth is not keeping up. Lower NVOICE.
  //   load high, no underruns   -> close to the edge; it will break under load.
  //   limited% more than a few  -> the mix is hitting the ceiling. Turn down.
  //   all of them quiet         -> it is the analogue side. The amp's supply is
  //                                the first thing to check, then the speaker:
  //                                a small driver asked for a 44 Hz kick just
  //                                makes a rattle, and no code fixes that.
  {
    static uint32_t lastUnder = 0, lastLim = 0, lastReport = 0;
    if (millis() - lastReport > 2000) {
      lastReport = millis();
      uint32_t u = gUnderruns, l = gLimited;
      Serial.printf("load %u%%  voices peak %u/%u  underruns %u (+%u)  "
                    "limited +%u\n",
                    (unsigned)gLoadPct, (unsigned)gVoicePk, NVOICE,
                    u, u - lastUnder, l - lastLim);
      lastUnder = u; lastLim = l; gVoicePk = 0;
    }
  }


  // ---- touch pads, on their own 20 ms cadence ------------------------------
  static uint32_t lastPoll = 0;
  uint32_t now = millis();
  if (now - lastPoll >= 20) {
    lastPoll = now;
    int raw = mprRead16(R_TOUCH_STATUS);
    if (raw >= 0) {
      uint16_t t = raw & 0x0FFF;
      if (t != lastTouched) {
        for (uint8_t e = 0; e < 12; e++) {
          bool is  = (t >> e) & 1;
          bool was = (lastTouched >> e) & 1;
          if (is && !was)      padPress(e);
          else if (!is && was) padRelease(e);
        }
        lastTouched = t;
      }
      stripService(t);
    }
    noteHoldService();
  }


  // ---- screen, ~14 fps -----------------------------------------------------
  // A full 1 KB frame over 400 kHz I2C is about 25 ms of blocking, so this is
  // deliberately slower than the pad poll: the pads have to stay responsive,
  // and nothing on screen moves faster than a 16th note anyway.
  static uint32_t lastDraw = 0;
  if (now - lastDraw >= 70) { lastDraw = now; draw(); }
}





