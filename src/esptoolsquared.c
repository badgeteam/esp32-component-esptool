// SPDX-FileCopyrightText: 2025 Julian Scheffers
// SPDX-FileCopyrightText: 2025 Nicolai Electronics
// SPDX-License-Identifier: MIT

#include "esptoolsquared.h"
#include <esp_app_format.h>
#include <esp_log.h>
#include <string.h>
#include "chips.h"
#include "esp_check.h"
#include "esp_err.h"
#include "et2_macros.h"
#include "et2_slip.h"
#include "et2_uart.h"
#include "rom/md5_hash.h"

#define ET2_TIMEOUT        pdMS_TO_TICKS(1000)
#define FLASH_SECTOR_SIZE  4096
#define FLASH_WRITE_SIZE   0x4000
#define ESP_CHECKSUM_MAGIC 0xEF

// Command header
typedef struct {
    uint8_t  resp;
    uint8_t  cmd;
    uint16_t len;
    uint32_t chk;
} et2_hdr_t;
_Static_assert(sizeof(et2_hdr_t) == 8);

// Security info data
typedef struct {
    uint32_t flags;
    uint8_t  key_count;
    uint8_t  key_purpose[7];
    uint32_t chip_id;
} et2_sec_info_t;
_Static_assert(sizeof(et2_sec_info_t) == 16);

static char const        TAG[] = "ET2";
static uart_port_t       cur_uart;
static uint32_t          chip_id;    // Current chip ID value
static et2_chip_t const* chip_attr;  // Current chip attributes

// Send a command.
static esp_err_t et2_send_cmd(et2_cmd_t cmd, uint32_t chk, void const* param, size_t param_len, void** resp,
                              size_t* resp_len, uint32_t* len, uint32_t* val, const uint8_t* data, uint32_t data_len);
// Send a command and check response code.
static esp_err_t et2_send_cmd_check(et2_cmd_t cmd, uint32_t chk, void const* param, size_t param_len, void** resp,
                                    size_t* resp_len, uint32_t* len, uint32_t* val, const uint8_t* data,
                                    uint32_t data_len);

static uint32_t et2_checksum(const uint8_t* data, uint32_t data_length, uint32_t state) {
    for (uint32_t i = 0; i < data_length; i++) {
        state ^= data[i];
    }
    return state;
}

esp_err_t et2_read_magic_reg(uint32_t* out_magic) {
    return et2_cmd_read_reg(0x40001000, out_magic);
}

// Set interface used to a UART.
esp_err_t et2_setif_uart(uart_port_t uart) {
    cur_uart = uart;
    return ESP_OK;
}

// Wait for the ROM "waiting for download" message.
static esp_err_t et2_wait_dl() {
    char const msg[] = "waiting for download\r\n";
    size_t     i     = 0;
    TickType_t lim   = xTaskGetTickCount() + ET2_TIMEOUT * 5;
    while (xTaskGetTickCount() < lim) {
        char rxd = 0;
        if (et2_uart_read(cur_uart, (uint8_t*)&rxd, 1) == ESP_OK) {
            if (rxd != msg[i]) {
                ESP_LOGV(TAG, "NE %zu", i);
                i = 0;
            }
            if (rxd == msg[i]) {
                ESP_LOGV(TAG, "EQ %zu", i);
                i++;
                if (i >= strlen(msg)) {
                    ESP_LOGI(TAG, "Download boot detected");
                    return ESP_OK;
                }
            }
        }
    }
    ESP_LOGI(TAG, "Download boot timeout");
    return ESP_ERR_TIMEOUT;
}

// Try to connect to and synchronize with the ESP32.
esp_err_t et2_sync() {
    RETURN_ON_ERR(et2_wait_dl());
    // clang-format off
    uint8_t const sync_rom[] = {
        0x07, 0x07, 0x12, 0x20,
        0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
        0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
        0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
        0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
    };
    // clang-format on

    for (int i = 0; i < 5; i++) {
        uint32_t len;
        uint32_t val;
        if (et2_send_cmd(ET2_CMD_SYNC, 0, sync_rom, sizeof(sync_rom), NULL, NULL, &len, &val, NULL, 0) == ESP_OK) {
            ESP_LOGI(TAG, "SYNC %" PRIx32 " / %" PRIx32, len, val);
            if (len != 0) {
                ESP_LOGI(TAG, "Sync received");
                return ESP_OK;
            }
        }
    }

    ESP_LOGI(TAG, "Sync timeout");
    return ESP_ERR_TIMEOUT;
}

// Set attributes according to chip ID.
void check_chip_id() {
    switch (chip_id & 0xffff) {
#ifdef CONFIG_ET2_SUPPORT_ESP32C3
        case ESP_CHIP_ID_ESP32C3:
            chip_attr = &et2_chip_esp32c3;
            break;
#else
        case ESP_CHIP_ID_ESP32C3:
            ESP_LOGW(TAG, "ESP32-C3 not supported!");
            break;
#endif
#ifdef CONFIG_ET2_SUPPORT_ESP32C2
        case ESP_CHIP_ID_ESP32C2:
            chip_attr = &et2_chip_esp32c2;
            break;
#else
        case ESP_CHIP_ID_ESP32C2:
            ESP_LOGW(TAG, "ESP32-C2 not supported!");
            break;
#endif
#ifdef CONFIG_ET2_SUPPORT_ESP32C6
        case ESP_CHIP_ID_ESP32C6:
            chip_attr = &et2_chip_esp32c6;
            break;
#else
        case ESP_CHIP_ID_ESP32C6:
            ESP_LOGW(TAG, "ESP32-C6 not supported!");
            break;
#endif
#ifdef CONFIG_ET2_SUPPORT_ESP32P4
        case ESP_CHIP_ID_ESP32P4:
            chip_attr = &et2_chip_esp32p4;
            break;
#else
        case ESP_CHIP_ID_ESP32P4:
            ESP_LOGW(TAG, "ESP32-P4 not supported!");
            break;
#endif
#ifdef CONFIG_ET2_SUPPORT_ESP32S2
        case ESP_CHIP_ID_ESP32S2:
            chip_attr = &et2_chip_esp32s2;
            break;
#else
        case ESP_CHIP_ID_ESP32S2:
            ESP_LOGW(TAG, "ESP32-S2 not supported!");
            break;
#endif
#ifdef CONFIG_ET2_SUPPORT_ESP32S3
        case ESP_CHIP_ID_ESP32S3:
            chip_attr = &et2_chip_esp32s3;
            break;
#else
        case ESP_CHIP_ID_ESP32S3:
            ESP_LOGW(TAG, "ESP32-S3 not supported!");
            break;
#endif
        default:
            ESP_LOGW(TAG, "Unknown chip ID 0x%04" PRIX32, chip_id & 0xffff);
            break;
    }
}

// Detect an ESP32 and, if present, read its chip ID.
// If a pointer is NULL, the property is not read.
esp_err_t et2_detect(uint32_t* chip_id_out) {
    void*  resp;
    size_t resp_len;
    RETURN_ON_ERR(et2_send_cmd(ET2_CMD_SEC_INFO, 0, NULL, 0, &resp, &resp_len, NULL, NULL, NULL, 0));
    LEN_CHECK_MIN(resp, resp_len, sizeof(et2_sec_info_t));
    chip_id = ((et2_sec_info_t*)resp)->chip_id;
    check_chip_id();
    *chip_id_out = chip_id;
    free(resp);
    return ESP_OK;
}

// Upload and start a flasher stub.
esp_err_t et2_run_stub() {
    uint32_t chip_id;
    RETURN_ON_ERR(et2_detect(&chip_id));
    if (!chip_attr) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    // Upload the stub.
    ESP_LOGI(TAG, "Uploading flasher stub text @ 0x%" PRIx32 " (0x%" PRIx32 " bytes)...", chip_attr->stub->text_start,
             chip_attr->stub->text_len);
    RETURN_ON_ERR(et2_mem_write(chip_attr->stub->text_start, chip_attr->stub->text, chip_attr->stub->text_len),
                  ESP_LOGE(TAG, "Failed to upload stub"));

    ESP_LOGI(TAG, "Uploading flasher stub data @ 0x%" PRIx32 " (0x%" PRIx32 " bytes)...", chip_attr->stub->data_start,
             chip_attr->stub->data_len);
    RETURN_ON_ERR(et2_mem_write(chip_attr->stub->data_start, chip_attr->stub->data, chip_attr->stub->data_len),
                  ESP_LOGE(TAG, "Failed to upload stub"));

    // Start the stub.
    ESP_LOGI(TAG, "Starting flasher stub...");
    ESP_LOGD(TAG, "Entrypoint 0x%08zx", chip_attr->stub->entry);
    RETURN_ON_ERR(et2_cmd_mem_end(chip_attr->stub->entry), ESP_LOGE(TAG, "Failed to start stub"));

    // Verify that the stub has successfully started.
    void*  resp;
    size_t resp_len;
    RETURN_ON_ERR(et2_slip_receive(cur_uart, &resp, &resp_len), ESP_LOGE(TAG, "Stub did not respond"));
    if (resp_len != 4 || memcmp(resp, "OHAI", 4)) {
        ESP_LOGE(TAG, "Unexpected response from stub");
        free(resp);
        return ESP_ERR_INVALID_RESPONSE;
    } else {
        ESP_LOGI(TAG, "Stub responded correctly");
    }

    if (chip_attr == &et2_chip_esp32c6) {
        ESP_LOGW(TAG, "Switched chip type to ESP32C6 with stub");
        chip_attr = &et2_chip_esp32c6_stub;
    }

    return ESP_OK;
}

static esp_err_t et2_send_cmd(et2_cmd_t cmd, uint32_t chk, void const* param, size_t param_len, void** resp,
                              size_t* resp_len, uint32_t* len, uint32_t* val, const uint8_t* data, uint32_t data_len) {
    void*  resp_dummy;
    size_t resp_len_dummy;
    bool   ignore_resp = false;
    if (!resp && !resp_len) {
        ignore_resp = true;
        resp        = &resp_dummy;
        resp_len    = &resp_len_dummy;
    } else if (!resp || !resp_len) {
        return ESP_ERR_INVALID_ARG;
    }

    if (data != NULL && data_len > 0) {
        chk = et2_checksum(data, data_len, ESP_CHECKSUM_MAGIC);
    }

    ESP_LOGI(TAG, "Send command op=0x%02X len=%zd byte%c chk=%" PRIx32, cmd, (param_len + data_len),
             (param_len + data_len) != 1 ? 's' : 0, chk);

    et2_hdr_t header = {0, cmd, param_len + data_len, chk};

    RETURN_ON_ERR(et2_slip_send_startstop(cur_uart));
    RETURN_ON_ERR(et2_slip_send_data(cur_uart, (uint8_t*)&header, sizeof(header)));
    RETURN_ON_ERR(et2_slip_send_data(cur_uart, (uint8_t*)param, param_len));
    if (data != NULL && data_len > 0) {
        RETURN_ON_ERR(et2_slip_send_data(cur_uart, data, data_len));
    }
    RETURN_ON_ERR(et2_slip_send_startstop(cur_uart));

    // Wait for max 100 tries for a response.
    for (int try = 0;; try++) {
        ESP_LOGD(TAG, "Receive try %d", try);
        RETURN_ON_ERR(et2_slip_receive(cur_uart, resp, resp_len));
        if (*resp_len < sizeof(et2_hdr_t) || ((et2_hdr_t*)*resp)->resp != 1) {
            continue;
        } else if (((et2_hdr_t*)*resp)->cmd == cmd) {
            break;
        } else if (try >= 100) {
            ESP_LOGE(TAG, "Receive timeout");
            return ESP_ERR_TIMEOUT;
        }
    }

    ESP_LOGD(TAG, "Receive len=%u", *resp_len);

    // Trim the header off of the response.
    if (len) {
        *len = ((et2_hdr_t*)*resp)->len;
    }
    if (val) {
        *val = ((et2_hdr_t*)*resp)->chk;
    }
    if (ignore_resp) {
        free(*resp);
    } else if (*resp_len == sizeof(et2_hdr_t)) {
        free(*resp);
        *resp     = NULL;
        *resp_len = 0;
    } else {
        *resp_len -= sizeof(et2_hdr_t);
        memmove(*resp, *resp + sizeof(et2_hdr_t), *resp_len);
    }

    return ESP_OK;
}

// Send a command and check response code.
static esp_err_t et2_send_cmd_check(et2_cmd_t cmd, uint32_t chk, void const* param, size_t param_len, void** resp,
                                    size_t* resp_len, uint32_t* len, uint32_t* val, const uint8_t* data,
                                    uint32_t data_len) {
    void*  resp_dummy;
    size_t resp_len_dummy;
    bool   ignore_resp = false;
    if (!resp && !resp_len) {
        ignore_resp = true;
        resp        = &resp_dummy;
        resp_len    = &resp_len_dummy;
    } else if (!resp || !resp_len) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t res = et2_send_cmd(cmd, chk, param, param_len, resp, resp_len, len, val, data, data_len);
    if (res) {
        return res;
    } else if (*resp_len < chip_attr->status_len) {
        free(*resp);
        return ESP_ERR_INVALID_RESPONSE;
    } else {
        uint8_t status, error;
        if (chip_attr->status_len == 2) {
            status = ((uint8_t*)*resp)[(*resp_len) - 2];
            error  = ((uint8_t*)*resp)[(*resp_len) - 1];
        } else {
            status = ((uint8_t*)*resp)[(*resp_len) - 4];
            error  = ((uint8_t*)*resp)[(*resp_len) - 3];
        }
        if (status) {
            free(*resp);
            ESP_LOGE(TAG, "Command 0x%02x failed with code 0x%02x", cmd, error);
            return ESP_FAIL;
        }
    }

    if (ignore_resp) {
        free(*resp);
    }

    return ESP_OK;
}

// Write to a range of memory.
esp_err_t et2_mem_write(uint32_t addr, void const* _wdata, uint32_t len) {
    uint8_t const* wdata = _wdata;
    ESP_LOGD(TAG, "Writing to RAM at 0x%08" PRIx32, addr);

    // Compute number of blocks.
    uint32_t blocks = (len + chip_attr->ram_block - 1) / chip_attr->ram_block;

    // Initiate write sequence.
    uint32_t payload[] = {len, blocks, chip_attr->ram_block, addr};
    RETURN_ON_ERR(et2_send_cmd_check(ET2_CMD_MEM_BEGIN, 0, payload, sizeof(payload), NULL, NULL, NULL, NULL, NULL, 0));

    // Send write data in blocks.
    for (uint32_t i = 0; i < blocks; i++) {
        uint32_t chunk_size = len - i * chip_attr->ram_block;
        if (chunk_size > chip_attr->ram_block) {
            chunk_size = chip_attr->ram_block;
        }
        RETURN_ON_ERR(et2_cmd_mem_data(wdata + i * chip_attr->ram_block, chunk_size, i));
    }

    return ESP_OK;
}

// Send MEM_DATA command to send memory write payload.
esp_err_t et2_cmd_mem_data(void const* _data, uint32_t data_len, uint32_t seq) {
    uint8_t const* data     = _data;
    uint32_t       header[] = {data_len, seq, 0, 0};
    uint8_t*       payload  = malloc(sizeof(header) + data_len);
    if (!payload) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(payload, header, sizeof(header));
    memcpy(payload + sizeof(header), data, data_len);
    uint32_t chk = 239;
    for (uint32_t i = 0; i < data_len; i++) {
        chk ^= data[i];
    }
    esp_err_t res =
        et2_send_cmd_check(ET2_CMD_MEM_DATA, chk, payload, sizeof(header) + data_len, NULL, NULL, NULL, NULL, NULL, 0);
    free(payload);
    return res;
}

// Send MEM_END command to restart into application.
esp_err_t et2_cmd_mem_end(uint32_t entrypoint) {
    uint32_t payload[] = {entrypoint == 0, entrypoint};
    ESP_LOGD(TAG, "Mem end, entrypoint: 0x%08" PRIx32, entrypoint);
    return et2_send_cmd_check(ET2_CMD_MEM_END, 0, payload, sizeof(payload), NULL, NULL, NULL, NULL, NULL, 0);
}

esp_err_t et2_cmd_read_reg(uint32_t address, uint32_t* out_value) {
    uint32_t val = 0;
    ESP_RETURN_ON_ERROR(
        et2_send_cmd_check(ET2_CMD_READ_REG, 0, &address, sizeof(uint32_t), NULL, NULL, NULL, &val, NULL, 0), TAG,
        "Failed to read register");
    *out_value = val;
    return ESP_OK;
}

esp_err_t et2_cmd_read_flash(uint32_t offset, uint32_t length, uint8_t* out_data) {
    uint32_t params[] = {offset, length, FLASH_SECTOR_SIZE, 64};
    ESP_RETURN_ON_ERROR(
        et2_send_cmd_check(ET2_CMD_READ_FLASH, 0, params, sizeof(params), NULL, NULL, NULL, NULL, NULL, 0), TAG,
        "Failed to read flash");

    // Receive data
    uint32_t received_length = 0;
    while (received_length < length) {
        uint8_t*  part        = NULL;
        size_t    part_length = 0;
        esp_err_t res         = et2_slip_receive(cur_uart, (void**)&part, &part_length);
        if (res != ESP_OK) {
            ESP_LOGE(TAG, "Failed to receive data: %s", esp_err_to_name(res));
            return res;
        }
        if ((received_length + part_length) < length && part_length < FLASH_SECTOR_SIZE) {
            ESP_LOGE(TAG, "Corrupt data, expected 0x%" PRIx16 " bytes but received 0x%" PRIx16 "bytes",
                     FLASH_SECTOR_SIZE, part_length);
            return res;
        }
        memcpy(&out_data[received_length], part, part_length);
        free(part);
        received_length += part_length;
        ESP_LOGI(TAG, "Reading flash... %u%% (%" PRIu32 " of %" PRIu32 " bytes)", (received_length * 100 / length),
                 received_length, length);
        et2_slip_send_startstop(cur_uart);
        et2_slip_send_data(cur_uart, (uint8_t*)&received_length, sizeof(uint32_t));
        et2_slip_send_startstop(cur_uart);
    }

    // Receive digest
    uint8_t*  digest        = NULL;
    size_t    digest_length = 0;
    esp_err_t res           = et2_slip_receive(cur_uart, (void**)&digest, &digest_length);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to receive digest");
        return res;
    }
    if (digest_length != 16) {
        free(digest);
        ESP_LOGE(TAG, "Received corrupted digest");
        return ESP_FAIL;
    }

    struct MD5Context context;

    uint8_t calculated_digest[16] = {0};
    MD5Init(&context);
    MD5Update(&context, out_data, length);
    MD5Final(calculated_digest, &context);

    if (memcmp(calculated_digest, digest, 16) != 0) {
        ESP_LOGE(TAG, "Digest does not match");
        free(digest);
        return ESP_FAIL;
    }

    free(digest);
    return ESP_OK;
}

// Send FLASH_BEGIN command to initiate memory writes
esp_err_t et2_cmd_flash_begin(uint32_t size, uint32_t offset) {
    uint32_t num_blocks = (size + FLASH_WRITE_SIZE - 1) / FLASH_WRITE_SIZE;
    uint32_t erase_size = size;
    uint32_t params[]   = {erase_size, num_blocks, FLASH_WRITE_SIZE, offset};
    return et2_send_cmd_check(ET2_CMD_FLASH_BEGIN, 0, params, sizeof(params), NULL, NULL, NULL, NULL, NULL, 0);
}

// Send FLASH_DATA command to send memory write payload
esp_err_t et2_cmd_flash_data(const uint8_t* data, uint32_t data_len, uint32_t seq) {
    uint32_t params[] = {data_len, seq, 0, 0};
    ESP_RETURN_ON_ERROR(
        et2_send_cmd_check(ET2_CMD_FLASH_DATA, 0, params, sizeof(params), NULL, NULL, NULL, NULL, data, data_len), TAG,
        "Failed to write to flash");
    return ESP_OK;
}

// Send FLASH_FINISH command to restart into application.
esp_err_t et2_cmd_flash_finish(bool reboot) {
    uint32_t params[] = {reboot ? 0 : 1};
    return et2_send_cmd_check(ET2_CMD_FLASH_END, 0, params, sizeof(params), NULL, NULL, NULL, NULL, NULL, 0);
}

// Write compressed data to flash

esp_err_t et2_cmd_deflate_begin(uint32_t uncompressed_size, uint32_t compressed_size, uint32_t offset) {
    uint32_t num_blocks = (compressed_size + FLASH_WRITE_SIZE - 1) / FLASH_WRITE_SIZE;
    uint32_t erase_size = uncompressed_size;
    uint32_t params[]   = {erase_size, num_blocks, FLASH_WRITE_SIZE, offset};
    return et2_send_cmd_check(ET2_CMD_DEFL_BEGIN, 0, params, sizeof(params), NULL, NULL, NULL, NULL, NULL, 0);
}

esp_err_t et2_cmd_deflate_data(const uint8_t* data, uint32_t data_len, uint32_t seq) {
    uint32_t params[] = {data_len, seq, 0, 0};
    ESP_RETURN_ON_ERROR(
        et2_send_cmd_check(ET2_CMD_DEFL_DATA, 0, params, sizeof(params), NULL, NULL, NULL, NULL, data, data_len), TAG,
        "Failed to write to flash");
    return ESP_OK;
}

esp_err_t et2_cmd_deflate_finish(bool reboot) {
    uint32_t params[] = {reboot ? 0 : 1};
    return et2_send_cmd_check(ET2_CMD_DEFL_END, 0, params, sizeof(params), NULL, NULL, NULL, NULL, NULL, 0);
}

// Erase entire flash
esp_err_t et2_cmd_erase_flash(void) {
    return et2_send_cmd_check(ET2_CMD_ERASE_FLASH, 0, NULL, 0, NULL, NULL, NULL, NULL, NULL, 0);
}

// Erase a region of flash
esp_err_t et2_cmd_erase_region(uint32_t offset, uint32_t length) {
    uint32_t params[] = {offset, length};
    return et2_send_cmd_check(ET2_CMD_ERASE_REGION, 0, params, sizeof(params), NULL, NULL, NULL, NULL, NULL, 0);
}
