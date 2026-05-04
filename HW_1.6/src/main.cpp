#include <Arduino.h>

#define LDR_PIN 4          // GPIO 4
#define V_REF 3.3          // Опорна напруга
#define ADC_MAX_12BIT 4095 // Максимальне значення для 12 біт

void setup()
{
  Serial.begin(9600);

  // Налаштування АЦП (Опційне завдання)
  analogReadResolution(12);                   // Встановлюємо 12-бітну роздільну здатність
  analogSetPinAttenuation(LDR_PIN, ADC_11db); // Атенюація 11дБ дозволяє читати до ~3.1В
}

void loop()
{
  // Зчитування RAW даних
  int rawADC = analogRead(LDR_PIN);

  // Обчислення напруги за формулою
  // V = (ADC * V_ref) / (ADCmax)
  float calculatedV = (rawADC * V_REF) / ADC_MAX_12BIT;

  // Зчитування вбудованою функцією (у мілівольтах)
  int nativeMV = analogReadMilliVolts(LDR_PIN);
  float nativeV = nativeMV / 1000.0;

  // Обчислення похибки
  float error = 0;
  if (nativeV > 0)
  {
    error = abs(calculatedV - nativeV) / nativeV * 100.0;
  }

  // Вивід результатів у консоль
  Serial.println("--------------------------------------------------");
  Serial.printf("RAW ADC: %d", rawADC);
  Serial.printf("| Calc Voltage: %4.2f", calculatedV);
  Serial.printf("| Native Voltage: %4.2f", nativeV);
  Serial.printf("| V Error: %4.2f%", error);
  Serial.println("--------------------------------------------------");

  delay(1000);
}
