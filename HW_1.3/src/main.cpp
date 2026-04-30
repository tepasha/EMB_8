#include <Arduino.h>

// Assign pins for the LEDs
const char RED_LED = 12;
const char BLUE_LED = 14;
const char GREEN_LED = 10;

// Speed settings
const char BLINK_DELAY = 80;   // Duration of a single flash
const char PAUSE_BETWEEN = 50; // Pause between double flashes
const char SWITCH_DELAY = 150; // Pause when changing colors

void blink_LED(char pin_led, char blink_delay, char pause_between);

void setup()
{
  // Set pins to output
  pinMode(RED_LED, OUTPUT);
  pinMode(BLUE_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
}

void loop()
{
  // --- RED FLASHES (2 times) ---
  blink_LED(RED_LED, BLINK_DELAY, PAUSE_BETWEEN);
  delay(SWITCH_DELAY); // Short pause before changing color
  // --- BLUE FLASHES (2 times) ---
  blink_LED(BLUE_LED, BLINK_DELAY, PAUSE_BETWEEN);
  delay(SWITCH_DELAY);
  // --- GREEN FLASHES (2 times) ---
  blink_LED(GREEN_LED, BLINK_DELAY, PAUSE_BETWEEN);
  delay(SWITCH_DELAY); // Pause before starting a new cycle
}

void blink_LED(char pin_led, char blink_delay, char pause_between)
{
  digitalWrite(pin_led, HIGH);
  delay(blink_delay);
  digitalWrite(pin_led, LOW);
  delay(pause_between);
  digitalWrite(pin_led, HIGH);
  delay(blink_delay);
  digitalWrite(pin_led, LOW);
}
