#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Include your custom engine files
#include "SynthState.h"
#include "InputManager.h"
#include "Looper.h" 

// --- OLED Screen Settings ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
// 0x3C is the standard I2C address for most 128x64 OLEDs
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// --- Global Engine Objects ---
SynthState synth;
InputManager input;
Looper looper; // Assuming you have a basic Looper class, otherwise comment this out!

// --- UI Drawing Function ---
void updateOLED() {
    display.clearDisplay();
    display.setTextSize(1);

    // 1. Draw Header
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("--- POCKETCHORD ---");
    display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

    // 2. Draw Menu Item 0: Key Root
    // If selected, invert colors (Black text on White background)
    if (input.selectedMenuItem == 0) display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    else display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
    display.setCursor(0, 15);
    display.printf("Root Key: %d  ", synth.getKeyRoot());

    // 3. Draw Menu Item 1: Scale
    if (input.selectedMenuItem == 1) display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    else display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
    display.setCursor(0, 25);
    display.printf("Scale: %s  ", synth.getScaleName().c_str());

    // 4. Draw Menu Item 2: Articulation
    if (input.selectedMenuItem == 2) display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    else display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
    display.setCursor(0, 35);
    display.printf("Artic: %.1f  ", synth.getArticulation());

    // 5. Draw Footer (Always white on black)
    display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
    display.drawLine(0, 50, 128, 50, SSD1306_WHITE);
    display.setCursor(0, 54);
    display.printf("Octave Offset: %d", synth.getOctaveOffset());

    // Push the buffer to the physical screen
    display.display();
}

// --- Boot Sequence ---
void setup() {
    Serial.begin(115200);
    delay(500);

    // Initialize OLED Display
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println("OLED allocation failed! Check wiring (SDA/SCL).");
        for(;;); // Halt execution if screen fails
    }
    
    // Initialize Hardware Matrix and Joystick
    input.begin();

    Serial.println("PocketChord Hardware Booted Successfully!");
}

// --- The Master Engine ---
void loop() {
    // 1. Read the physical world (this directly triggers synth notes!)
    input.scanHardware(synth, looper);

    // 2. Refresh the physical display to match the current state
    updateOLED();

    // 3. Short delay for stability
    delay(10); 
}
