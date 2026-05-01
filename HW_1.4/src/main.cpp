#include <Arduino.h>

// Pin definitions
const char LED1_PIN = 18;
const char LED2_PIN = 19;
const char BUTTON_EXT_PIN = 4;  // External button
const char BUTTON_BOOT_PIN = 0; // Built-in BOOT button

// Mode parameters
unsigned int blinkDelay = 500; // Initial delay (ms)

void setup()
{
  // Configuring outputs for LEDs
  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);

  // Configure inputs for buttons
  pinMode(BUTTON_EXT_PIN, INPUT_PULLUP);
  pinMode(BUTTON_BOOT_PIN, INPUT_PULLUP);

  // Add serial logger
  Serial.begin(115200);
}

void loop()
{
  // 1. Check for external button press (Fast mode)
  if (digitalRead(BUTTON_EXT_PIN) == LOW)
  {
    delay(50); // Debounce
    if (digitalRead(BUTTON_EXT_PIN) == LOW)
    {
      blinkDelay = 100; // Fast
      Serial.println("[EVENT] External Button Pressed: Switching to FAST (100ms)");
    }
  }

  // 2. Check for BOOT button press (Slow mode)
  if (digitalRead(BUTTON_BOOT_PIN) == LOW)
  {
    delay(50); // Debounce
    if (digitalRead(BUTTON_BOOT_PIN) == LOW)
    {
      blinkDelay = 1000; // Slow
      Serial.println("[EVENT] BOOT Button Pressed: Switching to SLOW (1000ms)");
    }
  }

  // Blinking logic
  digitalWrite(LED1_PIN, HIGH);
  digitalWrite(LED2_PIN, HIGH);
  delay(blinkDelay);

  digitalWrite(LED1_PIN, LOW);
  digitalWrite(LED2_PIN, LOW);
  delay(blinkDelay);
}
