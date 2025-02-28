#pragma once

#include <stdint.h>
#include "driver/uart.h"
#include "esp_err.h"

esp_err_t et2_slip_send_startstop(uart_port_t uart);
esp_err_t et2_slip_send_data(uart_port_t uart, uint8_t const* data, size_t len);
esp_err_t et2_slip_receive(uart_port_t uart, void** out_resp, size_t* out_resp_len);
