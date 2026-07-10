// board_esp32c2.h — pin map + panel quirks for the ESP32-C2 (ESP8684) SmallTV
// knockoff. 1.54" 240x240 ST7789V IPS over SPI (routed through the GPIO matrix).
//
// Pin map taken from a community ESPHome config for this exact device and
// confirmed against the board photos (ESP8684 QFN + CH340C + AMS1117-3.3).
// Unlike the ESP8266, the C2's SPI can use arbitrary GPIOs, so SCLK/MOSI are
// passed explicitly to the display bus. CS is hardwired to GND on the panel.
#pragma once

#define TFT_SCLK    4
#define TFT_MOSI    6
#define TFT_DC      5
#define TFT_RST     1
#define TFT_CS     -1   // CS tied to GND on the panel -> GFX_NOT_DEFINED
#define TFT_BL     18   // backlight (PWM, active-low)

// Panel colour order: verified on-device as RGB. Setting the MADCTL BGR bit
// swaps red/blue on this panel (yellow renders as cyan), so leave it at 0.
#define TFT_BGR     0

// Backlight is active-low (community config uses inverted PWM). Runtime-overridable.
#define TFT_BL_DEFAULT_INVERTED true

// No ambient-light sensor populated on this board -> auto-brightness compiled out.
#define HAS_LDR     0
#define ADC_MAX  4095   // ESP32-C2 ADC is 12-bit (unused while HAS_LDR == 0)
