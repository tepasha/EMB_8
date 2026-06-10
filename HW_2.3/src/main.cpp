#include <Arduino.h>

// Визначаємо піни для світлодіодів
const int LED1_PIN = 2;
const int LED2_PIN = 3;
const int LED3_PIN = 4;

// Інтервали блимання в мілісекундах
const unsigned long INTERVAL_LED1 = 200;
const unsigned long INTERVAL_LED2 = 500;
const unsigned long INTERVAL_LED3 = 1000;

// Змінні для збереження часу останнього перемикання
unsigned long previousMillisLED1 = 0;
unsigned long previousMillisLED2 = 0;
unsigned long previousMillisLED3 = 0;

// Змінні для збереження поточного стану світлодіодів (LOW = вимкнено, HIGH = увімкнено)
bool stateLED1 = LOW;
bool stateLED2 = LOW;
bool stateLED3 = LOW;

void setup()
{
  // Налаштовуємо піни на вихід
  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  pinMode(LED3_PIN, OUTPUT);
}

void loop()
{
  // Фіксуємо поточний час від моменту старту плати
  unsigned long currentMillis = millis();

  // --- Процес 1: LED1 (200 мс) ---
  if (currentMillis - previousMillisLED1 >= INTERVAL_LED1)
  {
    previousMillisLED1 = currentMillis;
    stateLED1 = !stateLED1;
    digitalWrite(LED1_PIN, stateLED1);
  }

  // --- Процес 2: LED2 (500 мс) ---
  if (currentMillis - previousMillisLED2 >= INTERVAL_LED2)
  {
    previousMillisLED2 = currentMillis;
    stateLED2 = !stateLED2;
    digitalWrite(LED2_PIN, stateLED2);
  }

  // --- Процес 3: LED3 (1000 мс) ---
  if (currentMillis - previousMillisLED3 >= INTERVAL_LED3)
  {
    previousMillisLED3 = currentMillis;
    stateLED3 = !stateLED3;
    digitalWrite(LED3_PIN, stateLED3);
  }
}
