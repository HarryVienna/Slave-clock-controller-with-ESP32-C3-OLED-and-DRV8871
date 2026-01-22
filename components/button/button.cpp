/**
 * @file button.cpp
 * @brief Implementation of Button class for ESP-IDF
 */

#include "button.hpp"
#include "esp_log.h"

static const char* TAG = "BUTTON";

// Konstruktor
Button::Button(gpio_num_t pin, bool active_low,
               uint32_t debounce_ms, uint32_t long_press_ms,
               uint32_t repeat_ms, uint32_t double_click_ms)
    : _pin(pin),
      _active_low(active_low),
      _debounce_ms(debounce_ms),
      _long_press_ms(long_press_ms),
      _repeat_ms(repeat_ms),
      _double_click_ms(double_click_ms)
{
    ESP_LOGI(TAG, "Button-Objekt wird erstellt für GPIO %d", _pin);

    // GPIO konfigurieren
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << _pin);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = _active_low ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = _active_low ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;

    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO %d Konfiguration fehlgeschlagen: %s", _pin, esp_err_to_name(err));
        return;
    }

    // Monitoring-Task erstellen
    BaseType_t ret = xTaskCreate(
        _taskWrapper,
        "button_task",
        BUTTON_TASK_STACK_SIZE,
        this,
        BUTTON_TASK_PRIORITY,
        &_task_handle
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Task-Erstellung fehlgeschlagen für GPIO %d", _pin);
        return;
    }

    ESP_LOGI(TAG, "Button erfolgreich initialisiert auf GPIO %d", _pin);
}

// Destruktor
Button::~Button() {
    if (_task_handle) {
        vTaskDelete(_task_handle);
        ESP_LOGI(TAG, "Button-Task gelöscht für GPIO %d", _pin);
    }
}

// Statischer Task-Wrapper für FreeRTOS
void Button::_taskWrapper(void* param) {
    Button* self = static_cast<Button*>(param);
    self->_taskLoop();
}

// Button-Zustand lesen (berücksichtigt active_low)
bool Button::_readState() const {
    int level = gpio_get_level(_pin);
    return _active_low ? (level == 0) : (level == 1);
}

// Callback-Helfer
void Button::_callShortPress() {
    _click_detected = true;
    _last_press_type = PressType::Short;

    if (_short_press_cb) {
        _short_press_cb(PressType::Short);
    }
}

void Button::_callLongPress() {
    _click_detected = true;
    _last_press_type = PressType::Long;

    if (_long_press_cb) {
        _long_press_cb(PressType::Long);
    }
}

void Button::_callDoubleClick() {
    _click_detected = true;
    _last_press_type = PressType::Double;

    if (_double_click_cb) {
        _double_click_cb(PressType::Double);
    }
}

// Haupt-Task-Schleife
void Button::_taskLoop() {
    // Initialisierung
    _last_state = _readState();
    _last_debounce_time = 0;
    _press_start_time = 0;
    _last_repeat_time = 0;
    _long_press_triggered = false;
    _last_release_time = 0;
    _waiting_for_double_click = false;

    ESP_LOGI(TAG, "Button-Task gestartet für GPIO %d", _pin);

    while (true) {
        bool current_state = _readState();
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

        // Entprellung: Prüfen ob Zustand gewechselt und Entprellzeit abgelaufen
        if (current_state != _last_state &&
            (now - _last_debounce_time) > _debounce_ms) {

            _last_debounce_time = now;
            _last_state = current_state;

            if (current_state) {
                // Button gedrückt
                _press_start_time = now;
                _long_press_triggered = false;
                ESP_LOGD(TAG, "Button gedrückt auf GPIO %d", _pin);
            } else {
                // Button losgelassen
                uint32_t press_duration = now - _press_start_time;

                // Nur kurze Drücke für Doppelklick-Erkennung verarbeiten
                if (!_long_press_triggered && press_duration < _long_press_ms) {

                    // Prüfen ob wir auf zweiten Klick warten (Doppelklick-Erkennung)
                    if (_waiting_for_double_click &&
                        (now - _last_release_time) < _double_click_ms) {
                        // Zweiter Klick innerhalb Timeout - Doppelklick!
                        ESP_LOGD(TAG, "Doppelklick erkannt auf GPIO %d", _pin);
                        _callDoubleClick();
                        _waiting_for_double_click = false;
                    } else {
                        // Erster Klick oder Timeout abgelaufen
                        ESP_LOGD(TAG, "Kurzer Druck erkannt auf GPIO %d (Dauer: %lu ms)",
                                 _pin, press_duration);
                        _waiting_for_double_click = true;
                        _last_release_time = now;
                    }
                } else if (_long_press_triggered) {
                    ESP_LOGD(TAG, "Langer Druck beendet auf GPIO %d (Dauer: %lu ms)",
                             _pin, press_duration);
                }

                _long_press_triggered = false;
            }
        }

        // Langer Druck Erkennung und Wiederholung
        if (current_state &&
            (now - _press_start_time) > _long_press_ms) {

            if (!_long_press_triggered) {
                // Erster langer Druck - Doppelklick-Wartezeit abbrechen
                _long_press_triggered = true;
                _waiting_for_double_click = false;
                _last_repeat_time = now;
                ESP_LOGD(TAG, "Langer Druck erkannt auf GPIO %d", _pin);
                _callLongPress();
            } else if (_enable_repeat &&
                      (now - _last_repeat_time) > _repeat_ms) {
                // Wiederholung bei gehaltenem Button
                _last_repeat_time = now;
                ESP_LOGD(TAG, "Langer Druck Wiederholung auf GPIO %d", _pin);
                _callLongPress();
            }
        }

        // Doppelklick-Timeout - kurzen Druck auslösen wenn Timeout abgelaufen
        if (_waiting_for_double_click &&
            (now - _last_release_time) > _double_click_ms) {
            ESP_LOGD(TAG, "Doppelklick-Timeout - kurzer Druck ausgelöst auf GPIO %d", _pin);
            _callShortPress();
            _waiting_for_double_click = false;
        }

        // Kurze Pause um CPU-Last zu reduzieren
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// Blockierendes Warten auf Tastendruck
Button::PressType Button::waitForPress() {
    _click_detected = false;

    while (!_click_detected) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    return _last_press_type;
}

// Aktueller Button-Zustand
bool Button::isPressed() const {
    return _readState();
}
