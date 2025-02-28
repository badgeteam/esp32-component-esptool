// SPDX-FileCopyrightText: 2025 Julian Scheffers
// SPDX-License-Identifier: MIT

#pragma once

#include <driver/uart.h>
#include <stdint.h>
#include "esp_system.h"

// ESP flashing protocol commands.
typedef enum {
    ET2_CMD_FLASH_BEGIN   = 0x02,
    ET2_CMD_FLASH_DATA    = 0x03,
    ET2_CMD_FLASH_END     = 0x04,
    ET2_CMD_MEM_BEGIN     = 0x05,
    ET2_CMD_MEM_END       = 0x06,
    ET2_CMD_MEM_DATA      = 0x07,
    ET2_CMD_SYNC          = 0x08,
    ET2_CMD_WRITE_REG     = 0x09,
    ET2_CMD_READ_REG      = 0x0A,
    // Supported by ESP32-S2 and later.
    ET2_CMD_SEC_INFO      = 0x14,
    // Supported by stub only
    ET2_CMD_ERASE_FLASH   = 0xD0,
    ET2_CMD_ERASE_REGION  = 0xD1,
    ET2_CMD_READ_FLASH    = 0xD2,
    ET2_CMD_RUN_USER_CODE = 0xD3,
} et2_cmd_t;

// Set interface used to a UART.
esp_err_t et2_setif_uart(uart_port_t uart);
// Try to connect to and synchronize with the ESP32.
esp_err_t et2_sync();
// Detect an ESP32 and, if present, read its chip ID.
// If a pointer is NULL, the property is not read.
esp_err_t et2_detect(uint32_t* chip_id);
// Upload and start a flasher stub.
esp_err_t et2_run_stub();

// Write to a range of memory.
esp_err_t et2_mem_write(uint32_t addr, void const* wdata, uint32_t len);
// Write to a range of FLASH.
esp_err_t et2_flash_write(uint32_t flash_off, void const* wdata, uint32_t len);

// Send MEM_BEGIN command to initiate memory writes.
esp_err_t et2_cmd_mem_begin(uint32_t size, uint32_t blocks, uint32_t blocksize, uint32_t offset);
// Send MEM_DATA command to send memory write payload.
esp_err_t et2_cmd_mem_data(void const* data, uint32_t data_len, uint32_t seq);
// Send MEM_END command to restart into application.
esp_err_t et2_cmd_mem_end(uint32_t entrypoint);
// Send FLASH_BEGIN command to initiate memory writes.
esp_err_t et2_cmd_flash_begin(uint32_t size, uint32_t offset);
// Send FLASH_BLOCK command to send memory write payload.
esp_err_t et2_cmd_flash_data(const uint8_t* data, uint32_t data_len, uint32_t seq);
// Send FLASH_FINISH command to restart into application.
esp_err_t et2_cmd_flash_finish(bool reboot);

esp_err_t et2_cmd_read_reg(uint32_t address, uint32_t* out_value);
esp_err_t et2_cmd_read_flash(uint32_t offset, uint32_t length, uint8_t* out_data);

void et2_test(void);
