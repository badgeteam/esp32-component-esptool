#pragma once

#define RETURN_ON_ERR(x, ...) \
    do {                      \
        esp_err_t err = (x);  \
        if (err) {            \
            __VA_ARGS__;      \
            return err;       \
        }                     \
    } while (0)

#define LEN_CHECK_MIN(resp, resp_len, exp_len, ...)                                                                 \
    do {                                                                                                            \
        if ((resp_len) < (exp_len)) {                                                                               \
            ESP_LOGE(TAG, "Invalid response length; expected %zu, got %zu", (size_t)(exp_len), (size_t)(resp_len)); \
            free((resp));                                                                                           \
            __VA_ARGS__;                                                                                            \
            return ESP_ERR_INVALID_RESPONSE;                                                                        \
        }                                                                                                           \
    } while (0)

#define LEN_CHECK(resp, resp_len, exp_len, ...)                                                                     \
    do {                                                                                                            \
        if ((resp_len) != (exp_len)) {                                                                              \
            ESP_LOGE(TAG, "Invalid response length; expected %zu, got %zu", (size_t)(exp_len), (size_t)(resp_len)); \
            free((resp));                                                                                           \
            __VA_ARGS__;                                                                                            \
            return ESP_ERR_INVALID_RESPONSE;                                                                        \
        }                                                                                                           \
    } while (0)
