#include "et2_uart.h"
#include <stdint.h>
#include "driver/uart.h"
#include "esp_log.h"
#include "et2_macros.h"

static char const TAG[] = "ET2 UART";

#define ET2_UART_TIMEOUT 2000

esp_err_t et2_uart_write(uart_port_t uart, uint8_t const* data, size_t len) {
    int res = uart_write_bytes(uart, data, len);
    if (res < 0) {
        ESP_LOGE(TAG, "UART write failed");
        return ESP_FAIL;
    } else if (res != len) {
        ESP_LOGE(TAG, "Incorrect UART write count; expected %zd, got %d", len, res);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t et2_uart_read(uart_port_t uart, uint8_t* out_data, size_t len) {
    int res = uart_read_bytes(uart, out_data, len, pdMS_TO_TICKS(ET2_UART_TIMEOUT));
    if (res < 0) {
        ESP_LOGE(TAG, "UART read failed");
        return ESP_FAIL;
    } else if (res == 0) {
        return ESP_ERR_TIMEOUT;
    } else if (res != len) {
        ESP_LOGE(TAG, "Incorrect UART read count; expected %zd, got %d", len, res);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t et2_uart_set_baudrate(uart_port_t uart, uint32_t baudrate) {
    return uart_set_baudrate(uart, baudrate);
}
