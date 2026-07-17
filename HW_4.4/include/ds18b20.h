#pragma once

#include <stdbool.h>
#include "driver/gpio.h"

// Ініціалізація GPIO для шини 1-Wire (open-drain, потрібен зовнішній підтягуючий
// резистор ~4.7 кОм між лінією даних та 3.3В; внутрішній pull-up вмикається як резерв).
void ds18b20_init(gpio_num_t pin);

// Запускає конвертацію температури "у фоні" (без очікування).
// Результат буде готовий не пізніше ніж через ~750 мс (12-біт) - зчитувати
// його треба НЕ швидше через ds18b20_read_temperature().
void ds18b20_start_conversion(gpio_num_t pin);

// Зчитує результат ОСТАННЬОЇ запущеної конвертації (з перевіркою CRC8).
// Повертає false, якщо датчик не відповів або CRC не збігся.
bool ds18b20_read_temperature(gpio_num_t pin, float *temperature_c);
