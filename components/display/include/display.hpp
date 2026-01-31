#pragma once

#include <string>
#include <ctime>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "u8g2.h"

class Display {
public:
    Display();
    ~Display();

    // Initialisierung
    bool init(gpio_num_t sda_pin, gpio_num_t scl_pin, gpio_num_t rst_pin = GPIO_NUM_NC);

    // Setzt den Status-Text (Obere Zeile, kleine Schrift)
    // Beispiel: "WIFI: OK" oder "Error"
    void disp_status(const std::string& status);

    // Setzt die Uhrzeit (Untere Zeile, große Schrift)
    void disp_time(const struct tm& timeinfo);

    // Display ein-/ausschalten (Power Save Mode)
    void setPowerSave(bool enable);

    // Toggle Display on/off
    void togglePower();

    // Gibt zurück ob Display an ist
    bool isOn() const { return !_powerSave; }

    // Screensaver: Display nach timeout_seconds automatisch ausschalten (0 = deaktiviert)
    void setScreensaverTimeout(uint32_t timeout_seconds);

private:
    u8g2_t u8g2;

    // Wir speichern den aktuellen Text, um bei Updates alles neu zeichnen zu können
    std::string _currentStatus;
    std::string _currentTime;

    // Interne Funktion zum Neuzeichnen des gesamten Screens
    void updateScreen();

    // Power Save Status
    bool _powerSave = false;

    // Screensaver Timer
    TimerHandle_t _screensaverTimer = nullptr;
    uint32_t _screensaverTimeout = 0;

    void _resetScreensaverTimer();
    static void _screensaverTimerCallback(TimerHandle_t xTimer);
};