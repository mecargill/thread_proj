#include <stdio.h>
#include <driver/i2s_std.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_adc/adc_continuous.h>
#include <soc/soc_caps.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h> // The OS kernel
#include <freertos/task.h>  
#include <esp_attr.h>
#include "driver/uart.h"

#include "vib.h"
#include "common.h"
#include "uart.h"

#define VIB_READ_LEN 64 //in samples!! not bytes
#define VIB_WRITE_LEN 128 + 1 //+1 for the length field. in samples!!
#define VIB_DOWNSAMPLE_FACTOR 10
#define VIB_THRESH_FACTOR 3.9f
#define VIB_IGNORE_LENGTH 600
#define VIB_ALPHA_MEAN 0.001f
#define VIB_ALPHA_DEV 0.001f
#define VIB_MIN_DEV 30

static const char *tag = "[VIB]";

static TaskHandle_t vib_task_handle;


static bool IRAM_ATTR conv_done_cb(adc_continuous_handle_t handle, const adc_continuous_evt_data_t *edata, void *user_data) 
{
    BaseType_t high_task_wakeup = pdFALSE;
    
    // Notify the task (using the handle passed via user_data)
    vTaskNotifyGiveFromISR((TaskHandle_t)user_data, &high_task_wakeup);
    
    // Force an immediate context switch if the task is higher priority
    portYIELD_FROM_ISR(high_task_wakeup);
    
    return (high_task_wakeup == pdTRUE);
}



static void vib_proc_stream(adc_continuous_data_t *parsed_data, uint32_t num_samples)
{
    static uint8_t start_bytes[2] = START_BYTES;
    static uint8_t end_bytes[2] =   END_BYTES;//The stream is shared with logging, so use this to check if anything interleaved
    static uint16_t out_buf[VIB_WRITE_LEN+1];//Pack 12 bits into 16 bits, one extra for the length
    static uint16_t out_buf_idx = 1;//to keep track of how many samples we actually wrote
    static int ds_count = 0;

    for (int i = 0; i < num_samples; i++) 
    {
        ds_count++;
        if (parsed_data[i].valid && (ds_count >= VIB_DOWNSAMPLE_FACTOR)) 
        {
            ds_count = 0;
            out_buf[out_buf_idx] = (uint16_t) parsed_data[i].raw_data;
            out_buf_idx++;
        }
    }
    //only write once out_buf fils up enough
    if (out_buf_idx > (VIB_WRITE_LEN-1) - VIB_READ_LEN)
    {
        //because I increment index after write, it is also a count of how many samples written
        //including the lenth itself, so subtract one
        out_buf[0] = out_buf_idx-1;
        
        if (uart_ready)
        {
            //stream data if in dev mode so I can see on laptop
            uart_write_bytes(UART_NUM_0, start_bytes, 2);
            uart_write_bytes(UART_NUM_0, out_buf, out_buf_idx*sizeof(out_buf[0]));
            uart_write_bytes(UART_NUM_0, end_bytes, 2);
        }
        
        out_buf_idx = 1;
    }
}

static void vib_proc_normal(adc_continuous_data_t *parsed_data, uint32_t num_samples)
{
    static int num_since_det = 0;
    static bool last_was_below = true;

    static uint16_t vib_tag = VIB_TAG;
    static int ds_count = 0;

    static float mean = 0;
    static float dev = 0;

    for (int i = 0; i < num_samples; i++) 
    {
        ds_count++;
        if (ds_count >= VIB_DOWNSAMPLE_FACTOR)
        {
            ds_count = 0;
            num_since_det++;
            //We detect on rising edge, and ignore any detections within IGNORE_LEN after a previous one
            if (parsed_data[i].valid)
            {
                mean = VIB_ALPHA_MEAN * parsed_data[i].raw_data + (1 - VIB_ALPHA_MEAN) * mean;
                float diff = parsed_data[i].raw_data - mean;
                diff = (diff > 0) ? diff : -diff;
                dev = VIB_ALPHA_MEAN * (diff) + (1 - VIB_ALPHA_MEAN) * dev;
                dev = (dev < VIB_MIN_DEV) ? VIB_MIN_DEV : dev;
                if (diff > VIB_THRESH_FACTOR*dev)
                {
                    if (last_was_below && num_since_det > VIB_IGNORE_LENGTH) 
                    {
                        //declare detection, reset counter
                        notify_host(vib_tag);
                        num_since_det = 0;
                    }
                    last_was_below = false;
                }
                else
                {
                    last_was_below = true;
                }
                  
            }
            
        }
        
    }

}

//we are passed the adc handle
static void vib_proc_task(void *pvParameters)
{
    
    uint32_t num_samples = 0;
    adc_continuous_data_t parsed_data[VIB_READ_LEN];//number of samples to read - 12 bit resolution
    
    adc_continuous_handle_t handle = (adc_continuous_handle_t)pvParameters;

    while (1) {
        // Go to sleep until the ISR calls vTaskNotifyGiveFromISR
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        
        esp_err_t ret = adc_continuous_read_parse(handle, parsed_data, VIB_READ_LEN, &num_samples, 100);
        if (ret == ESP_OK) {

            if (mode == DEV_VIB)
            {
                vib_proc_stream(parsed_data, num_samples);
            }
            else
            {
                vib_proc_normal(parsed_data, num_samples);
            }
  
        }
        else
        {
            ESP_LOGE(tag, "ADC reading failed: %s", esp_err_to_name(ret));
        }

    }
    
}

void vib_init()
{
    

    ESP_LOGI(tag, "Configuring ADC for vibration");
     //get handle to adc and initialize driver
    adc_continuous_handle_t adc_handle;
    adc_continuous_handle_cfg_t adc_handle_cfg = {
        .max_store_buf_size = 1024,
        .conv_frame_size = VIB_READ_LEN,
    };
    //ESP_LOGI("CHECKPOINT", "intitializing and getting adc handle ");
    ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_handle_cfg, &adc_handle));

    adc_digi_pattern_config_t pattern_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bit_width = ADC_BITWIDTH_12,
        .channel = ADC_CHANNEL_3,
        .unit = ADC_UNIT_1
    };
    adc_continuous_config_t adc_cfg = {
        .pattern_num = 1,
        .adc_pattern = &pattern_cfg,
        .sample_freq_hz = 20000,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE1
    };
    //ESP_LOGI("CHECKPOINT", "configuring adc");
    ESP_ERROR_CHECK(adc_continuous_config(adc_handle, &adc_cfg));
    
    

    xTaskCreate(vib_proc_task, "vib_task", 4096, adc_handle, 6, &vib_task_handle);

   

    adc_continuous_evt_cbs_t cbs = {
        .on_conv_done = conv_done_cb,
    };
    ESP_LOGI(tag, "Registering ADC callback");
    ESP_ERROR_CHECK(adc_continuous_register_event_callbacks(adc_handle, &cbs, vib_task_handle));
    //the vib task handle is passed as input to conv_done_cb


    //ESP_LOGI("CHECKPOINT", "starting adc");
    ESP_ERROR_CHECK(adc_continuous_start(adc_handle));
}
