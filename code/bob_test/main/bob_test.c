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

//mic: inmp441 - https://invensense.tdk.com/wp-content/uploads/2015/02/INMP441.pdf LEFT ONLY, L/R set to GND
//amp: max98357 - https://www.analog.com/media/en/technical-documentation/data-sheets/max98357a-max98357b.pdf

#define READ_LEN 64*CONFIG_SOC_ADC_DIGI_DATA_BYTES_PER_CONV //256
#define THRESHOLD 3048

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

//we are passed the adc handle
void vibration_processing(void *pvParameters)
{
    // Keep a local buffer for processing
    uint8_t result[READ_LEN]; 
    uint32_t ret_num;
    adc_continuous_handle_t handle = (adc_continuous_handle_t)pvParameters;

    while (1) {
        // Go to sleep until the ISR calls vTaskNotifyGiveFromISR
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (adc_continuous_read(handle, result, READ_LEN, &ret_num, 0) == ESP_OK) {
            // format 1 was chosen. this has the first 4 bits as the channel number, but we know its 0 so we don't need that
            //also, it takes an buffer of byte size numbers so we have to convert
            for (int i = 0; i < ret_num; i += 10) {
                uint16_t raw_val = ((uint16_t)result[i+1] << 8 | (uint16_t)result[i]) & 0x0FFF;
                if (raw_val > THRESHOLD) 
                {
                    ESP_LOGI("DETECTION ", "%u", raw_val);
                }
            }
        }
    }
}

void app_main(void)
{
    
     //get handle to adc and initialize driver
    adc_continuous_handle_t adc_handle;
    adc_continuous_handle_cfg_t adc_handle_cfg = {
        .max_store_buf_size = 1024,
        .conv_frame_size = READ_LEN,
    };
    ESP_LOGI("CHECKPOINT", "intitializing and getting adc handle ");
    ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_handle_cfg, &adc_handle));

    adc_digi_pattern_config_t pattern_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bit_width = ADC_BITWIDTH_12,
        .channel = ADC_CHANNEL_0,
        .unit = ADC_UNIT_1
    };
    adc_continuous_config_t adc_cfg = {
        .pattern_num = 1,
        .adc_pattern = &pattern_cfg,
        .sample_freq_hz = 20000,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE1
    };
    ESP_LOGI("CHECKPOINT", "configuring adc");
    ESP_ERROR_CHECK(adc_continuous_config(adc_handle, &adc_cfg));

    xTaskCreate(vibration_processing, "vib_task", 4096, adc_handle, 6, &vib_task_handle);

    adc_continuous_evt_cbs_t cbs = {
        .on_conv_done = conv_done_cb,
    };
    ESP_LOGI("CHECKPOINT", "registering adc callback");
    ESP_ERROR_CHECK(adc_continuous_register_event_callbacks(adc_handle, &cbs, vib_task_handle));
    //the vib task handle is passed as input to conv_done_cb



    i2s_chan_handle_t tx_handle;
    i2s_chan_handle_t rx_handle;

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
    ESP_LOGI("CHECKPOINT", "enabling tx");
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
    ESP_LOGI("CHECKPOINT", "enabling rx");
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));

   
    ESP_LOGI("CHECKPOINT", "starting adc");
    ESP_ERROR_CHECK(adc_continuous_start(adc_handle));

    
    int32_t buffer[256];
    size_t bytes_read;
    size_t bytes_written;

    while (1)
    {
        i2s_channel_read(rx_handle, buffer, sizeof(buffer), &bytes_read, 100);


        for(int i = 0; i < 256; i++)
        {
            if (true)//buffer[i] < 50000000/4)
            {
                buffer[i] = 4*buffer[i];
            }
            else
            {
                buffer[i] = 50000000;
            }
        }
        //ESP_LOGI("AUDIO", "%d, %d, %d, %d", buffer[0], buffer[1], buffer[2], buffer[3]);
        
      

        i2s_channel_write(tx_handle, buffer, bytes_read, &bytes_written, 100);
    }
    
    i2s_channel_disable(tx_handle);
    i2s_channel_disable(rx_handle);
    i2s_del_channel(tx_handle);
    i2s_del_channel(rx_handle);
    
}
