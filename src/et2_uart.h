#pragma once

#include <stdint.h>
#include "driver/uart.h"
#include "esp_err.h"

esp_err_t et2_uart_write(uart_port_t uart, uint8_t const* data, size_t len);
esp_err_t et2_uart_read(uart_port_t uart, uint8_t* out_data, size_t len);
esp_err_t et2_uart_set_baudrate(uart_port_t uart, uint32_t baudrate);
