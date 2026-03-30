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

#include "sound.h"
#include "common.h"
#include "uart.h"
#include "driver/uart.h"

#define SPEAKER_SIGNAL 0xAB
#define SPEAKER_AMP 50000000
#define MIC_READ_LEN 64 
#define MIC_WRITE_LEN 128 
#define MIC_DOWNSAMPLE_FACTOR 10
#define MIC_THRESHOLD 5500000
#define MIC_EV_LEN 600
#define MIC_INACTIVITY_LEN 50

//Make these global so that all functions can use them - ie if we want to play a sound alter
static i2s_chan_handle_t tx_handle;
static i2s_chan_handle_t rx_handle;
static const char *tag = "[SOUND]";


//there isn't a type for 24 bits, so to speed up uart I'll make one
//my mic outputs 24 bits. this will be little endian like normal
typedef struct __attribute__((packed)) {
    uint8_t byte0;
    uint8_t byte1;
    uint8_t byte2;
} mic_sample_t;

void mic_proc_stream(int32_t *in_buf, int num_samples)
{
    static mic_sample_t out_buf[MIC_WRITE_LEN+1];//Pack 12 bits into 16 bits, one extra for the length
    static uint16_t out_buf_idx = 0;//to keep track of how many samples we actually wrote

    static uint8_t start_bytes[2] = START_BYTES;
    static uint8_t end_bytes[2] =  END_BYTES;//The stream is shared with logging, so use this to check if anything interleaved jic
    static int ds_count = 0;
    //PACK DATA INTO CUSTOM TYPE
    for (int i = 0; i < num_samples; i++) 
    {
        ds_count++;
        if (ds_count >= MIC_DOWNSAMPLE_FACTOR)
        {
            ds_count = 0;
            out_buf[out_buf_idx].byte0 = (uint8_t)(in_buf[i] & 0xFF);//lsb
            out_buf[out_buf_idx].byte1 = (uint8_t)((in_buf[i] >> 8) & 0xFF);
            out_buf[out_buf_idx].byte2 = (uint8_t)((in_buf[i] >> 16) & 0xFF);//msb
            out_buf_idx++;
        }            
    }
    
    //WRITE DATA OUT TO UART
    //only write once out_buf fils up enough
    if (out_buf_idx > MIC_WRITE_LEN - MIC_READ_LEN)
    {
        //because I increment out_buf_idx after write, it is also a count of how many samples written
        if (uart_ready)
        {
            //stream data if in dev mode so I can see on laptop
            uart_write_bytes(UART_NUM_0, start_bytes, 2);
            uart_write_bytes(UART_NUM_0, &out_buf_idx, 2);
            uart_write_bytes(UART_NUM_0, out_buf, out_buf_idx*sizeof(out_buf[0]));
            uart_write_bytes(UART_NUM_0, end_bytes, 2);
            
        }
        out_buf_idx = 0;
    }

}

//If threshold crossed, start event. Event continues if crossings keep happening (spaced less than INACTIVITY_LEN apart).
//If event lasts more than EV_LEN, notify of a detection. 
void mic_proc_normal(int32_t *in_buf, int num_samples)
{
    static int num_since_crossing = MIC_INACTIVITY_LEN;
    static int samples_in_event = 0;
    static bool last_was_below = true;
    static uint16_t mic_tag = MIC_TAG;
    static int ds_count = 0;

    for (int i = 0; i < num_samples; i++) 
    {
        ds_count++;
        if (ds_count >= MIC_DOWNSAMPLE_FACTOR) //triggers on equality - include > just to be safe
        {
            ds_count = 0;
            num_since_crossing++;

            if (num_since_crossing < MIC_INACTIVITY_LEN)
            {
                samples_in_event++;
            }
            else
            {
                samples_in_event = 0;
            }
            //converted power threshold of 3*10^13 to signed threshold via sqrt
            if (in_buf[i] > MIC_THRESHOLD || in_buf[i] < -MIC_THRESHOLD)
            {
                if (last_was_below)
                {
                    //crossing happened - reset counter
                    num_since_crossing = 0;
                }
                
                last_was_below = false;
            }
            else
            {
                last_was_below = true;
            }

    
            if (samples_in_event > MIC_EV_LEN)
            {
                //reset event
                notify_host(mic_tag);
                samples_in_event = 0;
            }
            
        }            
    }
}

void mic_processing(void *pvParameters)
{
    int32_t in_buf[MIC_READ_LEN];
    size_t bytes_read;

    while (true)
    {
        //READ DATA
        i2s_channel_read(rx_handle, in_buf, sizeof(in_buf), &bytes_read, 100);
        int num_samples = bytes_read/sizeof(in_buf[0]);
        
        if (mode == DEV_MIC)
        {
            mic_proc_stream(in_buf, num_samples);
        }
        else
        {
            mic_proc_normal(in_buf, num_samples);
        }
        
    }
}

int32_t alert_buf[100*50]; //square wave pd of 100 at 48kHz means 480 Hz. 500 periods means about 1s (buffer has 50 pds, repeat 10 times)
int32_t silence[64*50];
//only register if mode is NORMAL
void wait_for_alert(void *pvParameters)
{
    while(1)
    {
        static uint8_t signal;
        static size_t bytes_written = 1;
        
        if (uart_read_bytes(UART_NUM_0, &signal, 1, portMAX_DELAY) > 0 && signal == SPEAKER_SIGNAL)
        {
            for (int i = 0; i < 10; i++)
            {
                i2s_channel_write(tx_handle, alert_buf, sizeof(alert_buf), &bytes_written, portMAX_DELAY);
            }
            
            for (int i = 0; i < 10; i++)
            i2s_channel_write(tx_handle, silence, sizeof(silence), NULL, portMAX_DELAY);
            uart_flush(UART_NUM_0);
        }
    }
    
}

void sound_init()
{
    ESP_LOGI(tag, "Configuring sound");

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
   
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, &rx_handle));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(48000),
        .slot_cfg = { 
            .data_bit_width = I2S_DATA_BIT_WIDTH_32BIT, 
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT, 
            .slot_mode = I2S_SLOT_MODE_MONO, 
            .slot_mask = I2S_STD_SLOT_LEFT, 
            .ws_width = I2S_SLOT_BIT_WIDTH_32BIT, 
            .ws_pol = false, 
            .bit_shift = true, 
            .msb_right = false, 
        }, //picks left by default
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = GPIO_NUM_23,
            .ws = GPIO_NUM_22,
            .dout = GPIO_NUM_21,
            .din = GPIO_NUM_19,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    /* Initialize the channel */
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));

    /* Before writing data, start the TX channel first */
    //ESP_LOGI("CHECKPOINT", "enabling tx");
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
    //ESP_LOGI("CHECKPOINT", "enabling rx");
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));
    ESP_LOGI(tag, "Creating mic processing task\n");
    xTaskCreate(mic_processing, "mic_task", 4096, NULL, 3, NULL);//slightly lower priority (3)

    for (int i = 0; i < 100*50; i++)
    {
        if (i % 100 < 50)
        {
            alert_buf[i] = SPEAKER_AMP;
        }
        else
        {
            alert_buf[i] = -SPEAKER_AMP;
        }
    }
    xTaskCreate(wait_for_alert, "alert_task", 4096, NULL, 2, NULL);//low priority
}

