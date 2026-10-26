#include <Arduino.h>
#include <driver/i2s.h>

// --- WIRING (ESP32-S3 -> PCM5102 module) ---
//   GPIO 21 -> BCK
//   GPIO 6  -> LCK   (a.k.a. LRCK / WS)
//   GPIO 42 -> DIN
//   GND     -> SCK   (no master clock: the PCM5102 makes its own)
//   3V3     -> VIN
//   GND     -> GND
const int PIN_I2S_BCLK = 21;
const int PIN_I2S_LRC  = 6;
const int PIN_I2S_DIN  = 42;

#define SAMPLE_RATE 32000
#define FRAMES      256
#define VOLUME      0.25f   // fraction of full scale; the PCM5102 line out is hot

// C major arpeggio, then a rest, forever
const float NOTES[] = { 261.63f, 329.63f, 392.00f, 523.25f, 392.00f, 329.63f };
const int   NOTE_COUNT = sizeof(NOTES) / sizeof(NOTES[0]);
const int   NOTE_SAMPLES = SAMPLE_RATE / 4;   // 250 ms per note

int16_t buf[FRAMES * 2];
float   phase = 0.0f;
int     noteIndex = 0;
int     noteSample = 0;

// Each pass of the arpeggio goes to: both ears, left only, right only, repeat
const char* PASS_NAMES[] = { "BOTH", "LEFT only", "RIGHT only" };
int     pass = 0;

void setup() {
    Serial.begin(115200);
    delay(1000);

    // Onboard RGB LED off (GPIO 48 on most S3 DevKitC boards, 38 on some clones)
    neopixelWrite(48, 0, 0, 0);
    neopixelWrite(38, 0, 0, 0);

    i2s_config_t cfg = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate          = SAMPLE_RATE,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = 0,
        .dma_buf_count        = 6,
        .dma_buf_len          = FRAMES,
        .use_apll             = false,
        .tx_desc_auto_clear   = true,
        .fixed_mclk           = 0
    };

    i2s_pin_config_t pins = {
        .mck_io_num   = I2S_PIN_NO_CHANGE,
        .bck_io_num   = PIN_I2S_BCLK,
        .ws_io_num    = PIN_I2S_LRC,
        .data_out_num = PIN_I2S_DIN,
        .data_in_num  = I2S_PIN_NO_CHANGE
    };

    esp_err_t err = i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
    if (err == ESP_OK) err = i2s_set_pin(I2S_NUM_0, &pins);
    Serial.printf("PCM5102 test: I2S %s\n", err == ESP_OK ? "started" : "FAILED");
}

void loop() {
    for (int i = 0; i < FRAMES; i++) {
        // Short fade in/out on each note so there are no clicks
        float env = 1.0f;
        const int fade = 400;
        if (noteSample < fade) env = noteSample / (float)fade;
        else if (noteSample > NOTE_SAMPLES - fade) env = (NOTE_SAMPLES - noteSample) / (float)fade;

        phase += TWO_PI * NOTES[noteIndex] / SAMPLE_RATE;
        if (phase > TWO_PI) phase -= TWO_PI;

        int16_t s = (int16_t)(sinf(phase) * env * VOLUME * 32767.0f);
        buf[i * 2]     = (pass == 2) ? 0 : s;   // left  (the tip of a TS cable)
        buf[i * 2 + 1] = (pass == 1) ? 0 : s;   // right

        if (++noteSample >= NOTE_SAMPLES) {
            noteSample = 0;
            noteIndex = (noteIndex + 1) % NOTE_COUNT;
            if (noteIndex == 0) {
                pass = (pass + 1) % 3;
                Serial.printf("playing: %s\n", PASS_NAMES[pass]);
            }
        }
    }

    size_t written;
    i2s_write(I2S_NUM_0, buf, sizeof(buf), &written, portMAX_DELAY);
}
