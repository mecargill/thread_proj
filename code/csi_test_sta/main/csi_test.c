//stdlib
#include <stdio.h>
//standard esp
#include "freertos/FreeRTOS.h" // The OS kernel
#include "freertos/task.h"     // Required for vTaskDelay (keep-alive loop)
#include "esp_system.h"        // Basic ESP32 system functions
#include "esp_log.h"           // The "professional" version of printf
#include "esp_err.h"
//specific
#include "nvs_flash.h"         // For the "Storage" (NVS)
#include "esp_wifi.h"          // The actual Wi-Fi driver functions
#include "esp_event.h"         // To handle "Connected" or "Got IP" events
#include "esp_netif.h"
#include "esp_mac.h"

#define CSI_DATA_PREFIX "CSI_DATA,"


void csi_handler(void *ctx, wifi_csi_info_t *data)
{
    //this is the mac of the ap in my house
    //static const uint8_t ap_mac[6] = {0xb4, 0xfb, 0xe4, 0x44, 0x29, 0xb9};
    /**
     * NOTE YOU Could BE MISSING DATA HERE DUE TO LEN AND FIRSTWDINVALID
     * NOTE YOU COULD BE TOO SLOW
     */
    
    //cache some stuff 
    uint16_t len = data->len;
    int8_t *csi_data = data->buf;
    int start = (data->first_word_invalid) ? 4 : 0;
    //Lines should come in to python program as "CSI_DATA,rssi,length,firstwdinvalid ..."
    printf(CSI_DATA_PREFIX "%d,%d,%d,", data->rx_ctrl.rssi, len, data->first_word_invalid ? 1 : 0);
    for (int i = start; i < len; i++)
    {
        printf("%d%s", csi_data[i], (i == len - 1) ? "\n" : ",");
    }
    
}
void app_main(void)
{
    //initialize wifi connection to router

    nvs_flash_init();//needs memory to store phy cal

    esp_netif_init();//intiializes tcp/ip
    esp_event_loop_create_default();//allows for callbacks & events
    esp_netif_create_default_wifi_sta();//creates a station interface

    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wifi_init_config);//configures wifi radio initialization

    wifi_config_t wifi_config = {
        .sta = {
            //the id of the ap that will be transmitting - left it open
            .ssid = "CSI_TX"
        }
    };

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);//configures wifi interface

    esp_wifi_start();//powers wifi radio
    esp_wifi_connect();



    //turn on csi

    wifi_csi_config_t csi_config = {
        .lltf_en           = true //listen for 
    };

    esp_wifi_set_csi_config(&csi_config);
    esp_wifi_set_csi_rx_cb(csi_handler, NULL);//registers my callback
    esp_wifi_set_csi(true);

}
