#pragma once

#include "nvs_flash.h"         // For the "Storage" (NVS)
#include "esp_wifi.h"          // The actual Wi-Fi driver functions
#include "esp_netif.h"
#include "esp_event.h"  

#define SSID "CSI_AP"
#define PORT 6000
#define MESSAGE " "

static void wifi_common_setup()
{
    nvs_flash_init();//needs memory to store phy cal

    esp_netif_init();//intiializes tcp/ip
    esp_event_loop_create_default();//allows for callbacks & events

    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wifi_init_config);
}

