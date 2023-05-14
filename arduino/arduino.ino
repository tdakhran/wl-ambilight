#include <FastLED.h>

#include "led_layout.hpp"

constexpr uint32_t DataPin           = 15;
constexpr uint16_t ShutdownTimeoutMs = 5000;
constexpr uint32_t ColorCorrection   = 0x80FFFF;    // reduce blue
constexpr uint8_t  Brightness        = 128;         // half brightness

CRGB leds[LedsTotal];

void wait_for_serial_data(size_t bytes)
{
    long int tp = millis();
    while (Serial.available() < bytes)
    {
        if (millis() - tp > ShutdownTimeoutMs)
        {
            reset();
            return;
        }
    }
}

void reset()
{
    for (auto &led : leds)
        led = CRGB(0, 0, 0);
    FastLED.show();
    delay(10);
}

void setup()
{
    delay(2000);
    Serial.begin(230400);
    FastLED.addLeds<WS2812B, DataPin, GBR>(leds, LedsTotal).setCorrection(ColorCorrection);
    FastLED.setBrightness(Brightness);
    reset();
}

bool assert_magic(char const *magic)
{
    for (int i = 0; i < 4; ++i)
    {
        wait_for_serial_data(1);
        if (Serial.read() != magic[i])
        {
            reset();
            return false;
        }
    }
    return true;
}

void loop()
{
    if (!assert_magic(SerialDataHeader))
    {
        delay(1);
        return;
    }

    for (int i = 0; i < LedsTotal; ++i)
    {
        wait_for_serial_data(3);
        Serial.readBytes((char *)(leds + i), 3);
    }

    if (!assert_magic(SerialDataFooter))
    {
        delay(1);
        return;
    }

    FastLED.show();
    delay(1);
}
