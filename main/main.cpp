#include <stdio.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "display.hpp"
#include "slave_clock.hpp"
#include "wifi_provisioner.hpp"
#include "button.hpp"

#include "config.h"



static const char* TAG = "MAIN_APP";



extern "C" void app_main(void) {

    SlaveClock clock(PULSE_GPIO_HBRIDGE1, PULSE_GPIO_HBRIDGE2, PULSE_GPIO_LED,
                PULSE_WIDTH_MS, PULSE_INTERVAL_MS);

    Display oled;
    oled.init(PIN_SDA, PIN_SCL);
    oled.setScreensaverTimeout(5 * 60);  // Display nach 5 Minuten ausschalten

    // Button initialisieren (active_low, da gegen GND geschaltet)
    Button button(BUTTON_PIN);

    // Bei kurzem Tastendruck Display ein-/ausschalten
    button.onShortPress([&oled](Button::PressType) {
        oled.togglePower();
    });

    // Initialer Status (wird erst sichtbar wenn Display eingeschaltet wird)
    oled.disp_status("Booting...");
    vTaskDelay(pdMS_TO_TICKS(1000));



    WifiProvisioner provisioner;

    if (provisioner.is_provisioned()) {
        provisioner.get_credentials();
    } else {
        oled.disp_status("Provisioning...");
        provisioner.start_provisioning("ESP32-WiFi-Provisioning", false);
    }

    oled.disp_status("Connecting...");
    provisioner.connect_sta();

    oled.disp_status("Synching time");
    while(!provisioner.is_time_synchronized()) {

        ESP_LOGI(TAG, "Zeit ist noch nicht mit dem NTP-Server synchronisiert.");

        // Warte eine Sekunde bis zur nächsten Ausgabe
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    oled.disp_status("Time synched"); 
    clock.setTime(provisioner.get_provisioned_hour(), provisioner.get_provisioned_minute());
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Hauptschleife
    while (true) {

        time_t now;
        time(&now);

        struct tm timeinfo;
        localtime_r(&now, &timeinfo);

        oled.disp_time(timeinfo);

        clock.update();

        vTaskDelay(pdMS_TO_TICKS(1000));
    }

}