#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "SynthState.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
// Standard I2C address for OLEDs is usually 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

SynthState synth;

void setup() {
    Serial.begin(115200);

    // Initialize the OLED screen
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println("OLED allocation failed");
        for(;;); // Don't proceed, loop forever
    }

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("PocketChord Booting...");
    display.display();
    delay(1000);
}

void loop() {
    // Example: Retrieve a value from SynthState
    float currentArtic = synth.getArticulation();

    // Clear the screen and draw the new UI
    display.clearDisplay();
    
    display.setCursor(0, 0);
    display.println("--- POCKETCHORD ---");
    
    display.setCursor(0, 20);
    display.print("Artic: ");
    display.println(currentArtic);

    // Push the graphics to the physical screen
    display.display();
    
    // Refresh rate delay (e.g., 10 frames per second)
    delay(100); 
}
