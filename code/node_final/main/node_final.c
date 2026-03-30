#include <stdio.h>

#include "esp_wifi.h" 

#include "csi_rx.h"
#include "csi_tx.h"
#include "uart.h"
#include "vib.h"
#include "sound.h"
#include "wifi_common.h"
#include "common.h"

#include "freertos/FreeRTOS.h" 
#include "freertos/task.h"  
#include "esp_system.h"        // Basic ESP32 system functions
#include "esp_log.h"           // The "professional" version of printf
#include "esp_err.h"

#include "driver/uart.h"//delete later

static const char *tag = "[MAIN]";
enum Mode mode = NORMAL;
bool uart_ready = false;

void app_main(void)
{
   
    ESP_LOGI(tag, "Starting up...");
    

    wifi_common_setup();
    set_as_sta();
    esp_wifi_start();

    if (an_ap_already_exists())
    {
        ESP_LOGI(tag, "AP found, taking RX role");
        enable_csi_rx();
        //in this case there is now a callback to recieve csi and sending       
    }
    else
    {     
        ESP_LOGI(tag, "AP not found, taking TX role");   
        esp_wifi_stop(); //You can't switch modes while wifi is running
        set_as_ap();
        esp_wifi_start();
        enable_csi_tx();
        //in this case there is now a background task to send udp packets
    }

    sound_init(); //now there is a task reading mic data 
    vib_init(); //now there is a callback to send adc data 
    
     
    if (mode == NORMAL)
    {
        uart_init(115200); 
    }
    else
    {
        vTaskDelay(pdMS_TO_TICKS(500));//to let any error logs show up before disabling
        esp_log_level_set("*", ESP_LOG_NONE); //to not interfere with uart0 data streaming 
        uart_init(921600);
        uart_ready = true; 
    }

}

