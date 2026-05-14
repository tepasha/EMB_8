#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

enum LedState : uint8_t
{
    Off = 0,
    On = 1
};
enum SystemMode : uint8_t
{
    Blinking,
    AlwaysOn,
    AlwaysOff
};

// Клас-конфігурація з static const та constexpr
struct Config
{
    static constexpr uint8_t LED_PIN = 48;               // Номер піну вбудований LED ESP32
    static constexpr uint8_t BUTTON_PIN = 0;             // Пін кнопки BOOT
    static constexpr uint32_t BLINK_INTERVAL = 500;      // Час блимання (мс)
    static constexpr uint32_t SERIAL_BAUD = 9600;        // Швидкість серійного з'єднання
    static constexpr uint32_t STATS_ITERATIONS = 100000; // Для вимірювання швидкості loop
};

class Led
{
private:
    uint8_t _pin;
    LedState _internalState = LedState::Off;
    Adafruit_NeoPixel _pixel; // Об'єкт для керування RGB

public:
    // Ініціалізуємо NeoPixel (1 діод на вказаному піні)
    Led(uint8_t pin) : _pin(pin), _pixel(1, pin, NEO_GRB + NEO_KHZ800) {}

    void init()
    {
        _pixel.begin();
        _pixel.setBrightness(50); // Не ставити 255, щоб не сліпило
        set(LedState::Off);
    }

    void set(LedState state)
    {
        _internalState = state;
        if (state == LedState::On)
        {
            _pixel.setPixelColor(0, _pixel.Color(0, 255, 0)); // Зелений колір
        }
        else
        {
            _pixel.setPixelColor(0, _pixel.Color(0, 0, 0)); // Вимкнено
        }
        _pixel.show(); // Команда на оновлення кольору
    }

    void toggle()
    {
        set((_internalState == LedState::On) ? LedState::Off : LedState::On);
    }
};
volatile bool buttonPressed = false;
SystemMode currentMode = SystemMode::Blinking;

void IRAM_ATTR handleButtonInterrupt()
{
    buttonPressed = true;
}

Led statusLed(Config::LED_PIN);

void setup()
{
    Serial.begin(Config::SERIAL_BAUD);

    statusLed.init();

    pinMode(Config::BUTTON_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(Config::BUTTON_PIN), handleButtonInterrupt, FALLING);

    Serial.println("System Initialized. Mode: Blinking");
}

void loop()
{
    static uint32_t iterations = 0;
    static uint32_t lastStatsTime = micros();

    iterations++;
    if (iterations >= Config::STATS_ITERATIONS)
    {
        uint32_t currentTime = micros();
        float avgTime = static_cast<float>(currentTime - lastStatsTime) / Config::STATS_ITERATIONS;
        Serial.print("Avg loop time (us): ");
        Serial.println(avgTime);

        iterations = 0;
        lastStatsTime = micros();
    }

    if (buttonPressed)
    {
        static uint32_t lastInterruptTime = 0;
        uint32_t interruptTime = millis();

        if (interruptTime - lastInterruptTime > 200)
        {
            if (currentMode == SystemMode::Blinking)
                currentMode = SystemMode::AlwaysOn;
            else if (currentMode == SystemMode::AlwaysOn)
                currentMode = SystemMode::AlwaysOff;
            else
                currentMode = SystemMode::Blinking;

            Serial.print("Mode changed to: ");
            Serial.println(static_cast<int>(currentMode));

            if (currentMode == SystemMode::AlwaysOn)
                statusLed.set(LedState::On);
            if (currentMode == SystemMode::AlwaysOff)
                statusLed.set(LedState::Off);
        }
        lastInterruptTime = interruptTime;
        buttonPressed = false;
    }

    static uint32_t lastBlinkTime = 0;
    uint32_t currentMillis = millis();

    switch (currentMode)
    {
    case SystemMode::Blinking:
        if (currentMillis - lastBlinkTime >= Config::BLINK_INTERVAL)
        {
            statusLed.toggle();
            lastBlinkTime = currentMillis;
        }
        break;
    case SystemMode::AlwaysOn:
        statusLed.set(LedState::On);
        break;

    case SystemMode::AlwaysOff:
        statusLed.set(LedState::Off);
        break;
    }
}
