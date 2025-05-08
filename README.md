# Esptool²

Component for flashing ESP32 chips using an ESP32 chip.

This component is still in active development and has currently only been tested for programming the ESP32-C6. Many basic esptool functions are not yet supported.

## Usage example

A proper usage example has yet to be written but the block of code below should give an idea.

```
// Initialize
uint32_t chip_id;
esp_log_level_set(TAG, ESP_LOG_DEBUG);
ESP_ERROR_CHECK(et2_detect(&chip_id));
ESP_LOGI(TAG, "ESP32 detected; chip id 0x%08" PRIx32, chip_id);

// Start stub
ESP_ERROR_CHECK(et2_run_stub());

// Write test data to flash
uint8_t* dummy_data = malloc(4096);
for (int i = 0; i < 4096; i++) dummy_data[i] = i;
ESP_ERROR_CHECK(et2_cmd_flash_begin(4096 * 4, 0));
ESP_ERROR_CHECK(et2_cmd_flash_data(dummy_data, 4096, 0));
ESP_ERROR_CHECK(et2_cmd_flash_data(dummy_data, 4096, 1));
ESP_ERROR_CHECK(et2_cmd_flash_data(dummy_data, 4096, 2));
ESP_ERROR_CHECK(et2_cmd_flash_data(dummy_data, 4096, 3));
ESP_ERROR_CHECK(et2_cmd_flash_finish(false));

// Read test data from flash
memset(dummy_data, 0, 4096);
ESP_ERROR_CHECK(et2_cmd_read_flash(0, 4096, dummy_data));
printf("Data read:\r\n");
for (size_t i = 0; i < 4096; i++) {
    printf("%02x", dummy_data[i]);
}
printf("\r\n");
free(dummy_data);
```

## License

The contents of this repository are made available under the terms of the MIT license, see [LICENSE](LICENSE) for the full license text.
