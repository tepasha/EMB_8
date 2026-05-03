#include <Arduino.h>

const char BUTTON_LEFT = 15;
const char BUTTON_RIGHT = 15;

volatile uint32_t counter_left = 0;
volatile uint32_t counter_right = 0;

uint32_t last_reported_left_count = 0;
uint32_t last_reported_right_count = 0;

uint32_t debounce = 0;

void IRAM_ATTR reaction_left()
{
  if (millis() - debounce > 100)
  {
    debounce = millis() - 50;
    counter_left++;
  }
}

void IRAM_ATTR reaction_right()
{
  if (millis() - debounce > 100)
  {
    debounce = millis() - 50;
    counter_right++;
  }
}

void setup()
{
  Serial.begin(115200);

  pinMode(BUTTON_LEFT, INPUT_PULLUP);
  pinMode(BUTTON_RIGHT, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(BUTTON_LEFT), reaction_left, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_RIGHT), reaction_right, FALLING);
}

void loop()
{
  if (counter_left != last_reported_left_count)
  {
    last_reported_left_count = counter_left;
    Serial.print("Total LEFT pulses detected by MCU: ");
    Serial.println(last_reported_left_count);
  }

  if (counter_right != last_reported_right_count)
  {
    last_reported_right_count = counter_right;
    Serial.print("Total RIGHT pulses detected by MCU: ");
    Serial.println(last_reported_right_count);
  }
}
