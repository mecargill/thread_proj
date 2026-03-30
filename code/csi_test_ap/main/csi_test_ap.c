//stdlib
#include <stdio.h>
#include <string.h>//for strlen
//standard esp
#include "freertos/FreeRTOS.h" 
#include "freertos/task.h"     
#include "esp_system.h"       
#include "esp_log.h"           
#include "esp_err.h"
#include "lwip/sockets.h"

//wifi
#include "nvs_flash.h"         
#include "esp_wifi.h"          
#include "esp_event.h"        
#include "esp_netif.h"
#include "esp_mac.h"

const char *tag = "CSI_AP";

const char *payload = "HI";
const int port = 1234;

void send_udp_packet(void *pvParameters)
{
    struct sockaddr_in dest_addr = *(struct sockaddr_in*) pvParameters;//make sure to COPY by deref
    free(pvParameters);

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);

    while (true)
    {
        //sockaddr is generic, sockaddr_in is for ipv4 so we need to tell it the length of the specific one which we jsut casted away
        sendto(sock, payload, strlen(payload), 0, (struct sockaddr*) &dest_addr, sizeof(struct sockaddr_in));
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_id == IP_EVENT_ASSIGNED_IP_TO_CLIENT)
    {
        ip_event_assigned_ip_to_client_t *event = (ip_event_assigned_ip_to_client_t*) event_data;
        //This is an IPv4 address, which we will cast to a generic address in sendto
        struct sockaddr_in *dest_addr_ptr = malloc(sizeof(struct sockaddr_in));
        dest_addr_ptr->sin_addr.s_addr = (__uint32_t) event->ip.addr; //uint32_t to __uint32_t, for clarity
        dest_addr_ptr->sin_port = htons(port);
        dest_addr_ptr->sin_family = AF_INET;
        
        xTaskCreate(send_udp_packet, "send_udp_packet", 4096, dest_addr_ptr, 5, NULL);//5 is a medium priority, NULL bc we don't need handle
        
    }
}

void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) 
    {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(tag, "Station connected: " MACSTR, MAC2STR(event->mac));
    } 
    else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) 
    {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(tag, "Station disconnected: "MACSTR", AID=%d", MAC2STR(event->mac), event->aid);//association id
    }
}



void app_main(void)
{
    //initialize wifi connection to router

    nvs_flash_init();//needs memory to store phy cal

    esp_netif_init();//intiializes tcp/ip
    esp_event_loop_create_default();//allows for callbacks & events
    esp_netif_create_default_wifi_ap();//creates an ap interface

    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wifi_init_config);//configures wifi radio initialization
    //No password (open), hidden ssid, no channel switching - channel 1 only, no random features
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "CSI_TX",
            .channel = 1,
            .authmode = WIFI_AUTH_OPEN,
            .ssid_hidden = 1,
            .max_connection = 1,
            .csa_count = 0,
            .sae_ext = 0,
            .wpa3_compatible_mode = 0,
            .gtk_rekey_interval = 0,

        }
    };

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &wifi_config);//configures wifi interface

    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_STACONNECTED, &wifi_event_handler, NULL);
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_STADISCONNECTED, &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_ASSIGNED_IP_TO_CLIENT, &ip_event_handler, NULL);

    esp_wifi_start();//powers wifi radio
    esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW20);//just to be sure
    
}

/**
 * NOTES
 * 
 *
 * you need to lock phy rate to something low to avoid adaptation
 * you need to disable power saving potentially on sta
 */
