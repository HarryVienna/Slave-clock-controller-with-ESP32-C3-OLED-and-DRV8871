#pragma once

#include <sdkconfig.h>

#define PIN_SDA      GPIO_NUM_5
#define PIN_SCL      GPIO_NUM_6

#define PULSE_WIDTH_MS    350  // Pulse duration in milliseconds
#define PULSE_INTERVAL_MS 150  // Time between pulses
#define PULSE_GPIO_HBRIDGE1 GPIO_NUM_0 // Pin for Input1 of DRV8871 
#define PULSE_GPIO_HBRIDGE2 GPIO_NUM_1 // Pin for Input2 of DRV8871 
#define PULSE_GPIO_LED GPIO_NUM_8 // LED Pin of ESP32 C3 Board

#define BUTTON_PIN GPIO_NUM_9 // Button Pin




















