#include "slave_clock.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <time.h> 

static const char* TAG = "SLAVE_CLOCK";

// Konstruktor
SlaveClock::SlaveClock(gpio_num_t hbridge1_pin, gpio_num_t hbridge2_pin, gpio_num_t led_pin,
                     int pulse_width_ms, int pulse_interval_ms)
    : _hbridge1_pin(hbridge1_pin),
      _hbridge2_pin(hbridge2_pin),
      _led_pin(led_pin),
      _pulse_width_ms(pulse_width_ms),
      _pulse_interval_ms(pulse_interval_ms),
      _polarity_level(0)
{
    ESP_LOGI(TAG, "SlaveClock-Objekt wird erstellt.");
    _init_gpio();

    // Dummy-Impulse senden, um den Motor zu initialisieren
    ESP_LOGI(TAG, "Sende 2 Initialisierungsimpulse zur Motor-Polarisierung...");
    sendPulses(2);
}

// Private Helferfunktion zur GPIO-Initialisierung
void SlaveClock::_init_gpio() {
    gpio_config_t io_conf = {};

    // Bitmaske erstellen: Alle drei Pins auswählen (Input1, Input2 und LED)
    // (1ULL << PinNummer) schiebt eine 1 an die richtige Stelle
    io_conf.pin_bit_mask = (1ULL << _hbridge1_pin) | (1ULL << _hbridge2_pin) | (1ULL << _led_pin);

    // Modus: Ausgang
    io_conf.mode = GPIO_MODE_OUTPUT;

    // Pull-Down aktivieren (Sicherheit: Zieht Pin auf 0, wenn Chip im Reset ist)
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE; // WICHTIG für Motortreiber!

    // Interrupts deaktivieren
    io_conf.intr_type = GPIO_INTR_DISABLE;

    // Konfiguration anwenden
    gpio_config(&io_conf);

    // Initialen Zustand setzen
    gpio_set_level(_hbridge1_pin, 0);
    gpio_set_level(_hbridge2_pin, 0);
    gpio_set_level(_led_pin, 1);

    ESP_LOGI(TAG, "GPIOs für Motor und LED initialisiert (Pull-Down aktiv, Level Low).");
}

// Öffentliche Methode zum Senden von Impulsen
void SlaveClock::sendPulses(int count) {
    _send_pulses_internal(count);
}

// Interne Methode, die die eigentliche Arbeit macht
void SlaveClock::_send_pulses_internal(int count) {
    if (count <= 0) return;
    ESP_LOGI(TAG, "Sende %d Impuls(e)...", count);

    for (int i = 0; i < count; i++) {
        // 1. Impuls STARTEN + LED einschalten
        gpio_set_level(_led_pin, 0);

        if (_polarity_level) {
            // Positive Polarität: IN1=1, IN2=0
            gpio_set_level(_hbridge1_pin, 1);
            gpio_set_level(_hbridge2_pin, 0);
        } else {
            // Negative Polarität: IN1=0, IN2=1
            gpio_set_level(_hbridge1_pin, 0);
            gpio_set_level(_hbridge2_pin, 1);
        }

        // 2. Impulsdauer warten (Der Motor bewegt sich jetzt)
        vTaskDelay(pdMS_TO_TICKS(_pulse_width_ms));

        // 3. Impuls STOPPEN + LED ausschalten (Entspricht Enable = 0)
        // Beim DRV8871 bedeutet IN1=0 und IN2=0 "Coast" (Stromlos/Sleep)
        gpio_set_level(_hbridge1_pin, 0);
        gpio_set_level(_hbridge2_pin, 0);

        gpio_set_level(_led_pin, 1);

        // 4. Pause bis zum nächsten Impuls
        vTaskDelay(pdMS_TO_TICKS(_pulse_interval_ms));  

        // 5. Polarität für den nächsten Durchlauf wechseln
        _polarity_level = !_polarity_level;
    }
}

// Setzt die Startzeit der Uhr
void SlaveClock::setTime(uint8_t hour, uint8_t minute) {
    time_t now;
    time(&now);
    
    // 1. Wir füllen _clock_tm komplett mit dem heutigen Datum und der aktuellen Zeit
    localtime_r(&now, &_clock_tm);
    
    // 2. Jetzt überschreiben wir die Zeigerstellung
    _clock_tm.tm_hour = hour;
    _clock_tm.tm_min  = minute;
    _clock_tm.tm_sec  = 0; // Sekunden setzen wir auf 0 (Start einer Minute)
  
    ESP_LOGI(TAG, "SlaveClock Startzeit gesetzt auf: %02d.%02d.%04d %02d:%02d:00", 
             _clock_tm.tm_mday, 
             _clock_tm.tm_mon + 1,     // tm_mon ist 0-11
             _clock_tm.tm_year + 1900, // tm_year ist Jahre seit 1900
             _clock_tm.tm_hour, 
             _clock_tm.tm_min);
}

bool SlaveClock::_is_leap(int y) {
    // - Alle 4 Jahre (y % 4 == 0)
    // - Aber NICHT alle 100 Jahre (y % 100 != 0)
    // - Außer es sind 400 Jahre (y % 400 == 0)
    return (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
}

time_t SlaveClock::_timegm(struct tm *tm) {
    // 1. Jahr holen
    // tm_year ist "Jahre seit 1900" (z.B. 125 für 2025).
    long long y = tm->tm_year + 1900;
    
    // 2. Monate normalisieren
    // Falls durch Rechenoperationen mal "Monat -1" oder "Monat 13" reinkommt,
    // wird das hier korrigiert und das Jahr entsprechend angepasst.
    int m = tm->tm_mon; // 0 = Januar, 11 = Dezember
    
    if (m >= 12 || m < 0) {
        int adj = m / 12;
        m %= 12;
        if (m < 0) { 
            adj--; 
            m += 12; 
        }
        y += adj; // Jahr erhöhen oder verringern
    }
    
    // 3. Tage bis zum Monatsbeginn (für ein Nicht-Schaltjahr)
    // Beispiel: Bis März (Index 2) sind im Jan(31) + Feb(28) = 59 Tage vergangen.
    static const int days_before[] = {
        0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
    };
    
    // Start-Tage berechnen: Tage vor diesem Monat + Tage im aktuellen Monat
    // tm_mday ist 1-basiert, unsere Zeitrechnung startet aber bei 0, daher -1.
    long long days = days_before[m] + tm->tm_mday - 1;
    
    // 4. Schaltjahr-Korrektur für das *aktuelle* Jahr
    // Wenn wir ein Schaltjahr haben UND wir schon nach Februar sind (m > 1),
    // dann gab es einen 29. Februar -> wir müssen 1 Tag addieren.
    // (m=1 ist Februar, m=2 ist März)
    if (_is_leap(y) && m > 1) days++;
    
    // 5. Differenz zum Unix-Epoch (01.01.1970) berechnen
    // Wir ziehen 1970 ab, um die Anzahl der vergangenen Jahre zu bekommen.
    y -= 1970;
    
    // 6. Gesamtzahl der Tage berechnen
    // Formel:
    // + 365 * y          : Normale Tage für jedes vergangene Jahr
    // + (y + 2) / 4      : Plus ein Tag für jedes 4. Jahr (Schaltjahr)
    // - (y + 2) / 100    : Minus ein Tag für jedes 100. Jahr (kein Schaltjahr)
    // + (y + 302) / 400  : Plus ein Tag für jedes 400. Jahr (doch Schaltjahr)
    //
    // Hinweis zu den "Magischen Zahlen" (+2, +302):
    // Da wir ab 1970 zählen und nicht ab Jahr 0, müssen wir den Offset im 
    // Schaltjahr-Zyklus korrigieren (1972 war das erste Schaltjahr nach 1970).
    days += 365 * y + (y + 2) / 4 - (y + 2) / 100 + (y + 302) / 400;
    
    // 7. Alles in Sekunden umrechnen
    // 86400 = Sekunden pro Tag
    // 3600  = Sekunden pro Stunde
    // 60    = Sekunden pro Minute
    return days * 86400 + tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec;
}

// Prüft die Zeit und aktualisiert die Uhr
void SlaveClock::update() {
    time_t now;
    time(&now);
    struct tm timeinfo_now;
    localtime_r(&now, &timeinfo_now);

    // Hilfspuffer für lesbare Log-Ausgaben der Zeit
    char str_now[32];
    char str_clock[32];
    
    // Formatieren: "Tag.Monat.Jahr Stunde:Minute:Sekunde"
    strftime(str_now, sizeof(str_now), "%d.%m.%Y %H:%M:%S", &timeinfo_now);
    strftime(str_clock, sizeof(str_clock), "%d.%m.%Y %H:%M:%S", &_clock_tm);

    // Beide Zeiten in eine vergleichbare Zahl wandeln.
    // timegm ist hier immer noch nötig für Jahreswechsel & Co.
    time_t now_linear   = _timegm(&timeinfo_now);
    time_t clock_linear = _timegm(&_clock_tm);

    // difftime gibt Sekunden zurück
    int minutes_diff = (int)difftime(now_linear, clock_linear) / 60;

    ESP_LOGI(TAG, "---------------------");
    ESP_LOGI(TAG, "Systemzeit (Soll): %s (Linear: %ld)", str_now, (long)now_linear);
    ESP_LOGI(TAG, "Nebenuhr   (Ist) : %s (Linear: %ld)", str_clock, (long)clock_linear);
    ESP_LOGI(TAG, "Differenz        : %d Minuten", minutes_diff);

    if (minutes_diff > 0) {
        // A) Uhr geht nach -> Aufholen
        ESP_LOGI(TAG, "Sende %d Impulse", minutes_diff);
        _send_pulses_internal(minutes_diff);
        
        // Da wir aufgeholt haben, sind wir jetzt synchron.
        _clock_tm = timeinfo_now; 
        _clock_tm.tm_sec = 0; // Sekunden werden nicht gebraucht

    } else if (minutes_diff < 0) {
        // B) Uhr geht vor (oder Zeitumstellung zurück) -> Warten
        ESP_LOGI(TAG, "Warte noch %d Minuten", -minutes_diff);

    }
}