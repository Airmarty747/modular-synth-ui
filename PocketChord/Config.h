#pragma once

static const int     PIN_SDA   = 40;
static const int     PIN_SCL   = 39;
static const uint8_t MPR_ADDR  = 0x5A;
static const uint8_t OLED_ADDR = 0x3C;   

static const uint8_t TOUCH_TH   = 12;
static const uint8_t RELEASE_TH = 6;

static const int PIN_I2S_BCLK = 21;
static const int PIN_I2S_LRC  = 6;
static const int PIN_I2S_DIN  = 42;

static const int PIN_AMP_SD   = 47;  
static const int PIN_DAC_XSMT = -1;  
static const int PIN_LED_ONBOARD = 38;

static const int PIN_JOY_X  = 1;
static const int PIN_JOY_Y  = 2;
static const int PIN_JOY_SW = 48;

static const int      JOY_DEADZONE     = 500;
static const uint16_t JOY_SETTLE_MS    = 45;   
static const bool     JOY_INVERT_X     = true; 
static const bool     JOY_INVERT_Y     = true;
static const int PIN_BTN_VOL = 41;   
static const int PIN_BTN_OUT = 7;    

static const uint16_t BTN_SAMPLE_MS  = 2;
static const uint8_t  BTN_INTEGRATOR = 12;
static const bool     BTN_LATCHING   = false;
static const int      BTN_ACTIVE_LEVEL = 0;  
#define BTN_DEBUG 0                          

static const uint16_t HOLD_MS     = 700;
static const uint16_t AMP_TAIL_MS = 400;

static const uint32_t SAMPLE_RATE = 32000;

static const float VOL_LEVELS[] = { 0.04f, 0.07f, 0.12f, 0.20f,
                                    0.35f, 0.50f, 0.68f, 0.90f };
static const uint8_t VOL_STEPS   = sizeof(VOL_LEVELS) / sizeof(VOL_LEVELS[0]);
static const uint8_t VOL_DEFAULT = 4;

static const uint8_t MAX_VOICES = 12;

static const char* AP_SSID = "PocketChord";
static const char* AP_PASS = "chordchord";
