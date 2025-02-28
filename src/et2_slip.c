#include "et2_slip.h"
#include <stdint.h>
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "et2_macros.h"
#include "et2_uart.h"

#define SLIP_END     0xC0
#define SLIP_ESC     0xDB
#define SLIP_ESC_END 0xDC
#define SLIP_ESC_ESC 0xDD

static char const TAG[] = "ET2 SLIP";

esp_err_t et2_slip_send_startstop(uart_port_t uart) {
    return et2_uart_write(uart, (uint8_t[]){SLIP_END}, 1);
}

esp_err_t et2_slip_send_data(uart_port_t uart, uint8_t const* data, size_t len) {
    if (len < 1) {
        return ESP_OK;
    }
    while (len) {
        esp_err_t res;
        if (*data == SLIP_ESC) {
            res = et2_uart_write(uart, (uint8_t[]){SLIP_ESC, SLIP_ESC_ESC}, 2);
        } else if (*data == SLIP_END) {
            res = et2_uart_write(uart, (uint8_t[]){SLIP_ESC, SLIP_ESC_END}, 2);
        } else {
            res = et2_uart_write(uart, (uint8_t*)data, 1);
        }
        if (res != ESP_OK) {
            return res;
        }
        len--;
        data++;
    }
    return ESP_OK;
}

esp_err_t et2_slip_receive(uart_port_t uart, void** out_resp, size_t* out_resp_len) {
    // Wait for start of packet.
    while (true) {
        uint8_t rxd = 0;
        RETURN_ON_ERR(et2_uart_read(uart, &rxd, 1));
        if (rxd == SLIP_END) break;
        putchar(rxd);
    }

    size_t   len = 0;
    size_t   cap = 4096;
    uint8_t* buf = malloc(cap);
    if (!buf) {
        return ESP_ERR_NO_MEM;
    }

    while (true) {
        uint8_t rxd = 0;
        RETURN_ON_ERR(et2_uart_read(uart, &rxd, 1));

        if (rxd == SLIP_END) {
            // End of message.
            break;

        } else if (rxd == SLIP_ESC) {
            // Handle escape sequences.
            RETURN_ON_ERR(et2_uart_read(uart, &rxd, 1));
            if (rxd == SLIP_ESC_END) {
                rxd = SLIP_END;
            } else if (rxd == SLIP_ESC_ESC) {
                rxd = SLIP_ESC;
            } else {
                ESP_LOGE(TAG, "Invalid escape sequence 0xDB 0x%02" PRIX8, rxd);
                return ESP_ERR_INVALID_RESPONSE;
            }
        }

        // Append character to output.
        if (len >= cap) {
            cap       *= 2;
            void* mem  = realloc(buf, cap);
            if (!mem) {
                free(buf);
                return ESP_ERR_NO_MEM;
            }
            buf = mem;
        }
        buf[len++] = rxd;
    }

    *out_resp     = buf;
    *out_resp_len = len;
    return ESP_OK;
}
