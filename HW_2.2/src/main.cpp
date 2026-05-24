#include <Arduino.h>

#define PIN_RELAY_CTRL 11
#define PIN_RELAY_FB 12
#define NUM_MEASUREMENTS 10

volatile uint32_t isrTimestamp = 0;
volatile bool isrFired = false;

void IRAM_ATTR relayISR()
{
  isrTimestamp = micros();
  isrFired = true;
}

void setup()
{
  Serial.begin(115200);
  delay(200);

  pinMode(PIN_RELAY_CTRL, OUTPUT);
  digitalWrite(PIN_RELAY_CTRL, HIGH); // HIGH = вимкнено

  pinMode(PIN_RELAY_FB, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_RELAY_FB), relayISR, CHANGE);

  Serial.println("=== Час спрацювання реле ===");
  Serial.printf("%-4s %-14s %-14s\n", "№", "t_вкл (мкс)", "t_викл (мкс)");
  Serial.println("------------------------------------");
  delay(500);
}

void loop()
{
  static uint8_t count = 0;
  static uint8_t step = 0;
  static uint32_t ctrlUs = 0;
  static uint32_t sumOn = 0;
  static uint32_t sumOff = 0;
  static uint32_t waitMs = 0;
  static bool waiting = false;

  if (waiting && millis() - waitMs < 300)
    return;
  waiting = false;

  if (count >= NUM_MEASUREMENTS)
  {
    Serial.println("------------------------------------");
    Serial.printf("Сер. вкл:  %lu мкс (%.2f мс)\n",
                  sumOn / NUM_MEASUREMENTS,
                  (sumOn / NUM_MEASUREMENTS) / 1000.0f);
    Serial.printf("Сер. викл: %lu мкс (%.2f мс)\n",
                  sumOff / NUM_MEASUREMENTS,
                  (sumOff / NUM_MEASUREMENTS) / 1000.0f);
    detachInterrupt(digitalPinToInterrupt(PIN_RELAY_FB));
    while (true)
      delay(1000);
  }

  if (step == 0)
  {
    isrFired = false;
    ctrlUs = micros();
    digitalWrite(PIN_RELAY_CTRL, LOW); // LOW = увімкнути

    uint32_t t0 = millis();
    while (!isrFired && millis() - t0 < 50)
      ;

    if (isrFired)
    {
      sumOn += (isrTimestamp - ctrlUs);
      Serial.printf("%-4d %-14lu ", count + 1, isrTimestamp - ctrlUs);
    }
    else
    {
      Serial.printf("%-4d %-14s ", count + 1, "TIMEOUT");
    }
    step = 1;
    waiting = true;
    waitMs = millis();
  }
  else
  {
    isrFired = false;
    ctrlUs = micros();
    digitalWrite(PIN_RELAY_CTRL, HIGH); // HIGH = вимкнути

    uint32_t t0 = millis();
    while (!isrFired && millis() - t0 < 50)
      ;

    if (isrFired)
    {
      sumOff += (isrTimestamp - ctrlUs);
      Serial.printf("%-14lu\n", isrTimestamp - ctrlUs);
    }
    else
    {
      Serial.printf("%-14s\n", "TIMEOUT");
    }
    count++;
    step = 0;
    waiting = true;
    waitMs = millis();
  }
}
