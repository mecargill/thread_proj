#include "driver/uart.h"
#include "esp_err.h"
#include "common.h"
#include "uart.h"
#include "esp_timer.h"

#define BUF_SIZE 1024

static const char *tag = "[UART]";

void notify_host(uint16_t tag)
{
    static int64_t tstamp_us;
    static uint8_t start_bytes[2] = START_BYTES;
    static uint8_t end_bytes[2] =   END_BYTES;//The stream is shared with logging, so use this to check if anything interleaved
    tstamp_us = esp_timer_get_time();
    uart_write_bytes(UART_NUM_0, start_bytes, 2);
    uart_write_bytes(UART_NUM_0, &tag, 2);
    uart_write_bytes(UART_NUM_0, &tstamp_us, 8);
    uart_write_bytes(UART_NUM_0, end_bytes, 2);
}

void uart_init(int baud)
{
    ESP_LOGI(tag, "Configuring UART");
    const uart_config_t uart_config = {
        .baud_rate = baud,            // High speed 
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    // Install driver with: Port, RX buffer, TX buffer, queue size, queue handle, interrupt flags
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, BUF_SIZE * 2, BUF_SIZE * 2, 0, NULL, 0));

    ESP_ERROR_CHECK(uart_param_config(UART_NUM_0, &uart_config));
}



//this can help debug when logging is disabled
void test_send_packet(int offset)
{
    if (mode != NORMAL && uart_ready)
    {
        uint8_t start_bytes[2] = START_BYTES;
        uint8_t end_bytes[2] =   END_BYTES;
        uint8_t out_buf[64];
        out_buf[0] = 31;
        out_buf[1] = 0;
        for (int i = 2; i < 64; i++)
        {
            if (i % 2 == 0)
                out_buf[i] = i + offset;
            else    
                out_buf[i] = 0;
        }
        while (1)
        {
            uart_write_bytes(UART_NUM_0, start_bytes, 2);
            uart_write_bytes(UART_NUM_0, out_buf, 64);
            uart_write_bytes(UART_NUM_0, end_bytes, 2);
        }
    }
    else
    {
        ESP_LOGW(tag, "You are trying to send debugging packets while UART not ready.");
    }
    
}
