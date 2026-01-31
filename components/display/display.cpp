#include "display.hpp"
#include "esp_log.h"
#include "rom/gpio.h"
#include <cstring>
#include <cstdio>

extern "C" {
    #include "u8g2_esp32_hal.h"
}

static const char* TAG = "Display";

/**
 * @brief Setzt den I2C-Bus durch Clock-Cycling zurück
 *
 * Wenn der ESP32 während einer I2C-Übertragung resettet wird,
 * kann der Slave (Display) in einem ungültigen Zustand hängen bleiben.
 * Diese Funktion sendet 9 Clock-Pulse um den Bus zu befreien.
 */
static void i2c_bus_reset(gpio_num_t sda_pin, gpio_num_t scl_pin) {
    ESP_LOGI(TAG, "I2C Bus Reset (Clock-Cycling)...");

    // Pins als GPIO konfigurieren
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << sda_pin) | (1ULL << scl_pin);
    io_conf.mode = GPIO_MODE_OUTPUT_OD;  // Open-Drain für I2C
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&io_conf);

    // Beide Pins HIGH setzen
    gpio_set_level(sda_pin, 1);
    gpio_set_level(scl_pin, 1);
    esp_rom_delay_us(10);

    // 9 Clock-Pulse senden um eventuell hängende Slaves zu befreien
    for (int i = 0; i < 9; i++) {
        gpio_set_level(scl_pin, 0);
        esp_rom_delay_us(10);
        gpio_set_level(scl_pin, 1);
        esp_rom_delay_us(10);
    }

    // STOP-Bedingung senden: SDA LOW->HIGH während SCL HIGH
    gpio_set_level(sda_pin, 0);
    esp_rom_delay_us(10);
    gpio_set_level(scl_pin, 1);
    esp_rom_delay_us(10);
    gpio_set_level(sda_pin, 1);
    esp_rom_delay_us(10);

    ESP_LOGI(TAG, "I2C Bus Reset abgeschlossen");
}

Display::Display() {
    _currentStatus = "-";
    _currentTime = "00:00:00";
}

Display::~Display() {
    if (_screensaverTimer) {
        xTimerDelete(_screensaverTimer, 0);
    }
}

bool Display::init(gpio_num_t sda_pin, gpio_num_t scl_pin, gpio_num_t rst_pin) {
    // I2C-Bus zurücksetzen falls er in einem ungültigen Zustand ist
    i2c_bus_reset(sda_pin, scl_pin);

    u8g2_esp32_hal_t u8g2_esp32_hal = U8G2_ESP32_HAL_DEFAULT;
    u8g2_esp32_hal.bus.i2c.sda = sda_pin;
    u8g2_esp32_hal.bus.i2c.scl = scl_pin;
    u8g2_esp32_hal.reset = rst_pin;
    u8g2_esp32_hal_init(u8g2_esp32_hal);

    // Setup für 0.42" Display (72x40)
    u8g2_Setup_ssd1306_i2c_72x40_er_f(
        &u8g2,
        U8G2_R0,
        u8g2_esp32_i2c_byte_cb,
        u8g2_esp32_gpio_and_delay_cb
    );

    u8x8_SetI2CAddress(&u8g2.u8x8, 0x3C << 1);
    u8g2_InitDisplay(&u8g2);
    u8g2_SetPowerSave(&u8g2, 0);
    
    // Erstes Zeichnen
    updateScreen();
    
    return true;
}

void Display::disp_status(const std::string& status) {
    _currentStatus = status;
    updateScreen();
}

void Display::disp_time(const struct tm& timeinfo) {
    char timeStr[16]; // Puffer groß genug machen

    // Hier passiert jetzt die Formatierung
    // %02d = mindestens 2 Stellen, mit Nullen aufgefüllt
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d", 
             timeinfo.tm_hour, 
             timeinfo.tm_min, 
             timeinfo.tm_sec);
    
    // Den formatierten String in die Klassen-Variable speichern
    _currentTime = timeStr;
    
    // Neu zeichnen
    updateScreen();
}

void Display::updateScreen() {
    u8g2_ClearBuffer(&u8g2);

    // --- 1. STATUS ZEILE (OBEN) ---
    // Sehr kleine Schrift, um Platz zu sparen
    // u8g2_font_profont10_mr ist ca 6-7px hoch, sehr gut lesbar
    u8g2_SetFont(&u8g2, u8g2_font_profont10_mr);
    
    // Linie zur Trennung zeichnen (optional, bei Y=8)
    u8g2_DrawHLine(&u8g2, 0, 9, 72);

    // Text linksbündig oder zentriert? Links ist meist besser für Status.
    // Baseline bei y=7
    u8g2_DrawStr(&u8g2, 0, 7, _currentStatus.c_str());


    // --- 2. UHRZEIT (UNTEN) ---
    // Wir brauchen eine Schrift, die 8 Zeichen ("00:00:00") auf 72px quetscht.
    // Das sind max 9px pro Zeichen.
    // u8g2_font_profont15_mr ist ca 8px breit -> 8*8 = 64px. Passt perfekt.
    // Höhe ist ca 11-12px (Baseline to Top).
    u8g2_SetFont(&u8g2, u8g2_font_profont15_mr);

    // Zentrieren berechnen
    int width = u8g2_GetStrWidth(&u8g2, _currentTime.c_str());
    int x = (72 - width) / 2;
    if (x < 0) x = 0;
    
    // Y-Position: Etwas unter der Linie. Baseline bei ca. 30
    u8g2_DrawStr(&u8g2, x, 32, _currentTime.c_str());

    u8g2_SendBuffer(&u8g2);
}

void Display::setPowerSave(bool enable) {
    _powerSave = enable;
    u8g2_SetPowerSave(&u8g2, enable ? 1 : 0);
    ESP_LOGI(TAG, "Display Power Save: %s", enable ? "ON" : "OFF");
}

void Display::togglePower() {
    setPowerSave(!_powerSave);

    // Timer zurücksetzen wenn Display eingeschaltet wird
    if (isOn()) {
        _resetScreensaverTimer();
    }
}

void Display::setScreensaverTimeout(uint32_t timeout_seconds) {
    _screensaverTimeout = timeout_seconds;

    if (timeout_seconds == 0) {
        // Screensaver deaktivieren
        if (_screensaverTimer) {
            xTimerStop(_screensaverTimer, 0);
        }
        return;
    }

    // Timer erstellen falls noch nicht vorhanden
    if (!_screensaverTimer) {
        _screensaverTimer = xTimerCreate(
            "screensaver",
            pdMS_TO_TICKS(timeout_seconds * 1000),
            pdFALSE,  // Einmalig, nicht periodisch
            this,     // Timer-ID = this-Pointer für Callback
            _screensaverTimerCallback
        );
    } else {
        // Timer-Periode aktualisieren
        xTimerChangePeriod(_screensaverTimer, pdMS_TO_TICKS(timeout_seconds * 1000), 0);
    }

    // Timer starten wenn Display an ist
    if (isOn()) {
        _resetScreensaverTimer();
    }
}

void Display::_resetScreensaverTimer() {
    if (_screensaverTimer && _screensaverTimeout > 0) {
        xTimerReset(_screensaverTimer, 0);
    }
}

void Display::_screensaverTimerCallback(TimerHandle_t xTimer) {
    // this-Pointer aus Timer-ID holen
    Display* self = static_cast<Display*>(pvTimerGetTimerID(xTimer));
    ESP_LOGI(TAG, "Screensaver: Display wird ausgeschaltet");
    self->setPowerSave(true);
}