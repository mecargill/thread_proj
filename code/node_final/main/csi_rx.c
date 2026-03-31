#include <stdio.h>
//standard esp
#include "freertos/FreeRTOS.h" // The OS kernel
#include "freertos/task.h" 
#include "freertos/queue.h"
#include "esp_system.h"        // Basic ESP32 system functions
#include "esp_log.h"           
#include "esp_err.h"
//specific
#include "nvs_flash.h"        
#include "esp_wifi.h"          
#include "esp_event.h"         
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "driver/uart.h"

#include "wifi_common.h"
#include "common.h"
#include "uart.h"


//These indices are INCLUDING the first word that is usually invalid (2 subcarriers worth) - indices in terms of subcarriers
#define CSI_SUBC_SET1_MIN_INDEX 17
#define CSI_SUBC_SET1_MAX_INDEX 27 //not inclusive
#define CSI_SUBC_SET2_MIN_INDEX 38
#define CSI_SUBC_SET2_MAX_INDEX 48 //not inclusive
//thresholds in terms of rolling variance of window length 50 of squared magnitudes of csi data
#define CSI_THRESHOLD_LOW 100000
#define CSI_THRESHOLD_HIGH 650000
#define CSI_IGNORE_LEN 20
#define CSI_MOV_AVG_LEN 30
#define CSI_ROL_VAR_LEN 50



static const char *tag = "[CSI_STA/RX]";

static QueueSetHandle_t csi_queue;

typedef struct {
    uint16_t avg_set1;
    uint16_t avg_set2;
} csi_mag_sq_t;

void set_as_sta()
{
    ESP_LOGI(tag, "Setting as STA");
    esp_netif_create_default_wifi_sta();//creates a station interface

    esp_wifi_set_mode(WIFI_MODE_STA);

    wifi_config_t wifi_config = {
        .sta = {
            //the id of the ap that will be transmitting - left it open
            .ssid = SSID
        }
    };
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);//configures wifi interface
}

//I could have just called connect and handled the disconnected event, but this seemed a bit simpler
//It's blocking, so it keeps everything linear
bool an_ap_already_exists()
{
    ESP_LOGI(tag, "Checking if AP exists");
    wifi_scan_config_t scan_config = {
        .ssid = (uint8_t *)SSID,
        .show_hidden = true
    };

    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true)); //true for blocking

    uint16_t ap_count = 0;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
    
    return ap_count > 0;

}

void csi_proc_stream(int8_t *csi_data, int start, uint16_t num_valid_samples)
{
    int64_t tstamp_us = esp_timer_get_time(); //8 bytes  
    static uint8_t start_bytes[2] = START_BYTES;
    static uint8_t end_bytes[2] =   END_BYTES;
    if (uart_ready)
    {
        uart_write_bytes(UART_NUM_0, start_bytes, 2);
        uart_write_bytes(UART_NUM_0, &num_valid_samples, 2);
        uart_write_bytes(UART_NUM_0, &tstamp_us, 8);
        uart_write_bytes(UART_NUM_0, csi_data+start, num_valid_samples*2);
        uart_write_bytes(UART_NUM_0, end_bytes, 2);
    }
    
}

void csi_proc_normal(int8_t *csi_data, uint16_t len)
{
    uint32_t mag_sq_sum1 = 0;
    static const uint32_t sz1 = CSI_SUBC_SET1_MAX_INDEX - CSI_SUBC_SET1_MIN_INDEX;

    uint32_t mag_sq_sum2 = 0;
    static const uint32_t sz2 = CSI_SUBC_SET2_MAX_INDEX - CSI_SUBC_SET2_MIN_INDEX;

    csi_mag_sq_t result;

    BaseType_t high_task_wakeup = pdFALSE;
    //avg the magnitude squared of the two subcarrier sets and write to queue for processing
    //indices include the potentially invalid first word (we don't use it anyways either way)
    if (2*CSI_SUBC_SET2_MAX_INDEX <= len)
    {
        for (int i = CSI_SUBC_SET1_MIN_INDEX; i < CSI_SUBC_SET1_MAX_INDEX; i++)
        {
            int csi_i = csi_data[2*i];
            int csi_q = csi_data[2*i + 1];
            mag_sq_sum1 += csi_i*csi_i + csi_q*csi_q;
        }
        result.avg_set1 = mag_sq_sum1/sz1;


        for (int i = CSI_SUBC_SET2_MIN_INDEX; i < CSI_SUBC_SET2_MAX_INDEX; i++)
        {
            int csi_i = csi_data[2*i];
            int csi_q = csi_data[2*i + 1];
            mag_sq_sum2 += csi_i*csi_i + csi_q*csi_q;
        }
        result.avg_set2 = mag_sq_sum2/sz2;


        xQueueSendFromISR(csi_queue, &result, &high_task_wakeup);
        //if pushing to queue woke up a task, go to it immediately without waiting for tick
        portYIELD_FROM_ISR(high_task_wakeup);
    }   
    
}

void csi_handler(void *ctx, wifi_csi_info_t *data)
{
    int8_t *csi_data = data->buf;

    

    if (mode == DEV_CSI)
    {
        int start = (data->first_word_invalid) ? 4 : 0; //word is 32 bits, 4 bytes, 2 subcarriers
        uint16_t num_valid_samples = (data->len - start)/2; 
        csi_proc_stream(csi_data, start, num_valid_samples);
    }
    else
    {
        csi_proc_normal(csi_data, data->len);
    }
}


//There are 2 types of CSI events - low is for general presence detection (walking, etc.), high is for 
//extreme motion
void csi_threshold(uint64_t rol_var)
{
    static bool last_was_below_low;
    static bool last_was_below_high;

    static int num_since_det_low = CSI_IGNORE_LEN;
    static int num_since_det_high = CSI_IGNORE_LEN;

    static uint16_t low_tag = CSI_TAG_LOW;
    static uint16_t high_tag = CSI_TAG_HIGH;

    num_since_det_high++;
    num_since_det_low++;

    if (rol_var > CSI_THRESHOLD_LOW)
    {
        if (last_was_below_low && num_since_det_low > CSI_IGNORE_LEN) 
        {
            notify_host(low_tag);
            num_since_det_low = 0;
        }
        last_was_below_low = false;
    }
    else
    {
        last_was_below_low = true;
    }

    if (rol_var > CSI_THRESHOLD_HIGH)
    {
        if (last_was_below_high && num_since_det_high > CSI_IGNORE_LEN) 
        {
            notify_host(high_tag);
            num_since_det_high = 0;
        }
        last_was_below_high = false;
    }
    else
    {
        last_was_below_high = true;
    }


}

typedef struct {
    uint16_t buf[CSI_MOV_AVG_LEN];
    uint32_t sum;
    uint16_t avg;
} mov_avg_info_t;

typedef struct {
    uint16_t buf[CSI_ROL_VAR_LEN];
    uint32_t sum;
    uint64_t sum_sq;
} rol_var_info_t;

//only register this task if mode is not devcsi - otherwise queue will never be filled
void csi_proc_task(void *pvParameters)
{
    static csi_mag_sq_t mag_sq_avgs;
    static mov_avg_info_t mov_avg1;
    static mov_avg_info_t mov_avg2;
    static int mov_avg_idx = 0;
    static int mov_avg_fill_ct = 0;
    static rol_var_info_t rol_var1;
    static rol_var_info_t rol_var2;
    static int rol_var_idx = 0;
    static int rol_var_fill_ct = 0;

    static uint64_t result;
    
    while (1)
    {
        if (xQueueReceive(csi_queue, &mag_sq_avgs, portMAX_DELAY) == pdTRUE)
        {
            if (mov_avg_fill_ct < CSI_MOV_AVG_LEN)
            {
                //fill avg buffer - its not full yet
                mov_avg1.buf[mov_avg_fill_ct] = mag_sq_avgs.avg_set1;
                mov_avg1.sum += mag_sq_avgs.avg_set1;

                mov_avg2.buf[mov_avg_fill_ct] = mag_sq_avgs.avg_set2;
                mov_avg2.sum += mag_sq_avgs.avg_set2;

                mov_avg_fill_ct++;
            }
            else
            {
                //avg buffer full - update sum, calc avg
                mov_avg1.sum += mag_sq_avgs.avg_set1;
                mov_avg1.sum -= mov_avg1.buf[mov_avg_idx];
                mov_avg1.buf[mov_avg_idx] = mag_sq_avgs.avg_set1;
                mov_avg1.avg = mov_avg1.sum / CSI_MOV_AVG_LEN;

                mov_avg2.sum += mag_sq_avgs.avg_set2;
                mov_avg2.sum -= mov_avg2.buf[mov_avg_idx];
                mov_avg2.buf[mov_avg_idx] = mag_sq_avgs.avg_set2;
                mov_avg2.avg = mov_avg2.sum / CSI_MOV_AVG_LEN;

                mov_avg_idx = (mov_avg_idx + 1) % CSI_MOV_AVG_LEN;
                
                if (rol_var_fill_ct < CSI_ROL_VAR_LEN)
                {
                    //rolling var buffer not full
                    rol_var1.buf[rol_var_fill_ct] = mov_avg1.avg;
                    rol_var1.sum += mov_avg1.avg;
                    rol_var1.sum_sq += mov_avg1.avg * mov_avg1.avg;

                    rol_var2.buf[rol_var_fill_ct] = mov_avg2.avg;
                    rol_var2.sum += mov_avg2.avg;
                    rol_var2.sum_sq += mov_avg2.avg * mov_avg2.avg;

                    rol_var_fill_ct++;
                }
                else
                {
                    //rolling var buffer full
                    rol_var1.sum += mov_avg1.avg;
                    rol_var1.sum_sq += mov_avg1.avg * mov_avg1.avg;
                    rol_var1.sum -= rol_var1.buf[rol_var_idx];
                    rol_var1.sum_sq -= rol_var1.buf[rol_var_idx] * rol_var1.buf[rol_var_idx];
                    rol_var1.buf[rol_var_idx] = mov_avg1.avg;

                    rol_var2.sum += mov_avg2.avg;
                    rol_var2.sum_sq += mov_avg2.avg * mov_avg2.avg;
                    rol_var2.sum -= rol_var2.buf[rol_var_idx];
                    rol_var2.sum_sq -= rol_var2.buf[rol_var_idx] * rol_var2.buf[rol_var_idx];
                    rol_var2.buf[rol_var_idx] = mov_avg2.avg;

                    rol_var_idx = (rol_var_idx + 1) % CSI_ROL_VAR_LEN;

                    //variance up to factor of N - combine from set 1 and 2
                    result = rol_var1.sum_sq + rol_var2.sum_sq
                           - ((uint64_t) rol_var1.sum * rol_var1.sum + (uint64_t) rol_var2.sum * rol_var2.sum) / CSI_ROL_VAR_LEN;
                    csi_threshold(result);
                }

            }
            
        }
    }

} 

void enable_csi_rx()
{
    ESP_LOGI(tag, "Enabling csi callback");
    //2 queues for my 2 subcarrier sets
    csi_queue = xQueueCreate(64, sizeof(csi_mag_sq_t));       

    if (csi_queue == NULL) {
        // handle error
        ESP_LOGE(tag, "Failed to create queue");
    }

    if (mode != DEV_CSI)
    {
        xTaskCreate(csi_proc_task, "csi_proc", 4096, NULL, 5, NULL);
    }
    //connect to the ap
    esp_wifi_connect();

    //turn on csi
    wifi_csi_config_t csi_config = {
        .lltf_en           = true  
    };

    esp_wifi_set_csi_config(&csi_config);
    esp_wifi_set_csi_rx_cb(csi_handler, NULL);//registers my callback
    esp_wifi_set_csi(true);

}
