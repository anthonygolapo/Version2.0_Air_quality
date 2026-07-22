#include "sensor_reader.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "sensor_reader";

static const int LED_PIN = 2;

static const uint32_t DGS2_READ_TIMEOUT_MS = 1500;
static const uint8_t DGS2_MAX_RETRIES = 3;
static const uint8_t SPS30_MAX_RETRIES = 3;
static const uint32_t SPS30_COMMAND_DELAY_MS = 20;

static const gpio_num_t I2C_SDA_PIN = GPIO_NUM_8;
static const gpio_num_t I2C_SCL_PIN = GPIO_NUM_9;
static const uint8_t SPS30_I2C_ADDRESS = 0x69;
static const uint32_t I2C_FREQ_HZ = 100000;
static const uint16_t SPS30_CMD_START_MEASUREMENT = 0x0010;
static const uint16_t SPS30_CMD_READ_DATA_READY = 0x0202;
static const uint16_t SPS30_CMD_READ_MEASURED_VALUES = 0x0300;
static const uint8_t SPS30_OUTPUT_FORMAT_FLOAT_MSB = 0x03;
static const uint8_t SPS30_OUTPUT_FORMAT_FLOAT_LSB = 0x00;

static const gpio_num_t SPI_MOSI_PIN = GPIO_NUM_11;
static const gpio_num_t SPI_MISO_PIN = GPIO_NUM_13;
static const gpio_num_t SPI_SCK_PIN = GPIO_NUM_12;

static const gpio_num_t SC16_CS_PIN = GPIO_NUM_4;
static const gpio_num_t SC16_RST_PIN = GPIO_NUM_14;
static const gpio_num_t SC16_IRQ_PIN = GPIO_NUM_5;
static const uint32_t SC16_XTAL_HZ = 14745600UL;
static const uint32_t DGS2_BAUD = 9600;
static const uint8_t SC16_CHANNEL_A = 0;
static const uint8_t SC16_CHANNEL_B = 1;

static const gpio_num_t O3_RX_PIN = GPIO_NUM_41;
static const gpio_num_t O3_TX_PIN = GPIO_NUM_42;
static const gpio_num_t CO_RX_PIN = GPIO_NUM_39;
static const gpio_num_t CO_TX_PIN = GPIO_NUM_40;

static const int32_t INVALID_INT_SENTINEL = -9999;
static const float INVALID_FLOAT_SENTINEL = -9999.0f;
static const char *DGS2_SINGLE_MEASUREMENT_COMMAND = "\r";

static const uart_port_t O3_UART_PORT = UART_NUM_1;
static const uart_port_t CO_UART_PORT = UART_NUM_2;

typedef struct {
  bool valid;
  char sensor_name[8];
  char serial_number[32];
  int32_t ppb;
  float temperature_c;
  float humidity_percent;
} dgs2_reading_t;

typedef struct {
  bool valid;
  float pm1_0;
  float pm2_5;
  float pm10;
} sps30_reading_t;

typedef struct {
  bool valid;
  float temperature_c;
  float humidity_percent;
} env_reading_t;

static spi_device_handle_t g_sc16_device;
static bool g_spi_ready;
static bool g_uart_ready;
static bool g_i2c_ready;
static bool g_sc16_ready;
static bool g_sps30_ready;
static bool g_buses_initialized;

static dgs2_reading_t g_co_reading;
static dgs2_reading_t g_o3_reading;
static dgs2_reading_t g_no2_reading;
static dgs2_reading_t g_so2_reading;
static sps30_reading_t g_sps30_reading;
static env_reading_t g_env_reading;

static float normalized_dgs2_ppb(const dgs2_reading_t *reading) {
  if (reading == NULL || !reading->valid) {
    return 0.0f;
  }
  if (reading->ppb < 0) {
    ESP_LOGW(TAG, "[%s] PPB=%" PRId32 " is below zero; storing 0 ppb", reading->sensor_name, reading->ppb);
    return 0.0f;
  }
  return (float)reading->ppb;
}

static void format_now_iso8601(char *buffer, size_t size) {
  time_t now = time(NULL);
  struct tm utc_tm;
  gmtime_r(&now, &utc_tm);
  strftime(buffer, size, "%Y-%m-%dT%H:%M:%SZ", &utc_tm);
}

static bool is_finite_float(float value) {
  return !isnan(value) && isfinite(value);
}

static uint8_t sensirion_crc8(const uint8_t *data, size_t length) {
  uint8_t crc = 0xFF;
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      if ((crc & 0x80) != 0) {
        crc = (uint8_t)((crc << 1) ^ 0x31);
      } else {
        crc <<= 1;
      }
    }
  }
  return crc;
}

static bool is_numeric_string(const char *text) {
  if (text == NULL || *text == '\0') {
    return false;
  }

  bool has_digit = false;
  bool has_decimal = false;
  for (size_t i = 0; text[i] != '\0'; i++) {
    const char c = text[i];
    if (c >= '0' && c <= '9') {
      has_digit = true;
      continue;
    }
    if ((c == '-' || c == '+') && i == 0) {
      continue;
    }
    if (c == '.' && !has_decimal) {
      has_decimal = true;
      continue;
    }
    return false;
  }

  return has_digit;
}

static void trim_ascii(char *text) {
  if (text == NULL) {
    return;
  }

  size_t start = 0;
  size_t end = strlen(text);
  while (text[start] == ' ' || text[start] == '\r' || text[start] == '\n' || text[start] == '\t') {
    start++;
  }
  while (end > start &&
      (text[end - 1] == ' ' || text[end - 1] == '\r' || text[end - 1] == '\n' || text[end - 1] == '\t')) {
    end--;
  }

  if (start > 0) {
    memmove(text, text + start, end - start);
  }
  text[end - start] = '\0';
}

static bool split_csv_line(char *raw, char *fields[], size_t max_fields, size_t *field_count) {
  if (raw == NULL || fields == NULL || field_count == NULL) {
    return false;
  }

  *field_count = 0;
  char *cursor = raw;
  while (cursor != NULL && *cursor != '\0') {
    if (*field_count >= max_fields) {
      return false;
    }

    fields[*field_count] = cursor;
    (*field_count)++;

    char *comma = strchr(cursor, ',');
    if (comma == NULL) {
      break;
    }

    *comma = '\0';
    cursor = comma + 1;
  }

  for (size_t i = 0; i < *field_count; i++) {
    trim_ascii(fields[i]);
  }

  return *field_count > 0;
}

static void log_retry(const char *sensor_name, uint8_t attempt, const char *reason) {
  ESP_LOGW(TAG, "[%s] Attempt %u/%u failed: %s", sensor_name, attempt, DGS2_MAX_RETRIES, reason);
}

static esp_err_t init_uart_port(uart_port_t port, gpio_num_t tx_pin, gpio_num_t rx_pin) {
  const uart_config_t config = {
    .baud_rate = DGS2_BAUD,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    .source_clk = UART_SCLK_DEFAULT
  };

  ESP_RETURN_ON_ERROR(uart_driver_install(port, 512, 0, 0, NULL, 0), TAG, "uart_driver_install failed");
  ESP_RETURN_ON_ERROR(uart_param_config(port, &config), TAG, "uart_param_config failed");
  ESP_RETURN_ON_ERROR(uart_set_pin(port, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE), TAG, "uart_set_pin failed");
  return ESP_OK;
}

static esp_err_t init_i2c_bus(void) {
  const i2c_config_t config = {
    .mode = I2C_MODE_MASTER,
    .sda_io_num = I2C_SDA_PIN,
    .scl_io_num = I2C_SCL_PIN,
    .sda_pullup_en = GPIO_PULLUP_ENABLE,
    .scl_pullup_en = GPIO_PULLUP_ENABLE,
    .master.clk_speed = I2C_FREQ_HZ
  };

  ESP_RETURN_ON_ERROR(i2c_param_config(I2C_NUM_0, &config), TAG, "i2c_param_config failed");
  ESP_RETURN_ON_ERROR(i2c_driver_install(I2C_NUM_0, config.mode, 0, 0, 0), TAG, "i2c_driver_install failed");
  return ESP_OK;
}

static esp_err_t sps30_write_command(uint16_t command, const uint8_t *data, size_t data_length) {
  uint8_t buffer[8] = {
    (uint8_t)(command >> 8),
    (uint8_t)(command & 0xFF)
  };
  size_t length = 2;

  if (data != NULL && data_length == 2) {
    buffer[length++] = data[0];
    buffer[length++] = data[1];
    buffer[length++] = sensirion_crc8(data, 2);
  }

  return i2c_master_write_to_device(I2C_NUM_0, SPS30_I2C_ADDRESS, buffer, length, pdMS_TO_TICKS(100));
}

static esp_err_t sps30_read_after_command(uint16_t command, uint8_t *buffer, size_t length) {
  uint8_t command_bytes[2] = {
    (uint8_t)(command >> 8),
    (uint8_t)(command & 0xFF)
  };
  esp_err_t error = i2c_master_write_to_device(
    I2C_NUM_0,
    SPS30_I2C_ADDRESS,
    command_bytes,
    sizeof(command_bytes),
    pdMS_TO_TICKS(200)
  );
  if (error != ESP_OK) {
    return error;
  }

  // SPS30 commands require processing time before the response is read.
  vTaskDelay(pdMS_TO_TICKS(SPS30_COMMAND_DELAY_MS));
  return i2c_master_read_from_device(
    I2C_NUM_0,
    SPS30_I2C_ADDRESS,
    buffer,
    length,
    pdMS_TO_TICKS(200)
  );
}

static bool sps30_parse_words_with_crc(const uint8_t *raw, size_t raw_length, uint8_t *decoded, size_t decoded_length) {
  if (raw == NULL || decoded == NULL || raw_length != decoded_length + (decoded_length / 2)) {
    return false;
  }

  size_t raw_index = 0;
  size_t decoded_index = 0;
  while (raw_index + 2 < raw_length && decoded_index + 1 < decoded_length) {
    const uint8_t crc = sensirion_crc8(&raw[raw_index], 2);
    if (crc != raw[raw_index + 2]) {
      return false;
    }
    decoded[decoded_index++] = raw[raw_index++];
    decoded[decoded_index++] = raw[raw_index++];
    raw_index++;
  }

  return decoded_index == decoded_length;
}

static float sps30_decode_float_be(const uint8_t *data) {
  uint32_t bits = ((uint32_t)data[0] << 24) |
                  ((uint32_t)data[1] << 16) |
                  ((uint32_t)data[2] << 8) |
                  (uint32_t)data[3];
  float value = 0.0f;
  memcpy(&value, &bits, sizeof(value));
  return value;
}

static uint8_t sc16_make_address(uint8_t channel, uint8_t reg, bool read) {
  return (read ? 0x80 : 0x00) | ((reg & 0x0F) << 3) | ((channel & 0x01) << 1);
}

static esp_err_t sc16_transfer(uint8_t address, uint8_t *value, bool read) {
  if (!g_spi_ready || g_sc16_device == NULL) {
    return ESP_FAIL;
  }

  uint8_t tx_buffer[2] = {address, read ? 0x00 : *value};
  uint8_t rx_buffer[2] = {0};
  spi_transaction_t transaction = {
    .length = 16,
    .tx_buffer = tx_buffer,
    .rx_buffer = rx_buffer
  };

  esp_err_t err = spi_device_transmit(g_sc16_device, &transaction);
  if (err != ESP_OK) {
    return err;
  }

  if (read) {
    *value = rx_buffer[1];
  }
  return ESP_OK;
}

static void sc16_write_register(uint8_t channel, uint8_t reg, uint8_t value) {
  uint8_t data = value;
  if (sc16_transfer(sc16_make_address(channel, reg, false), &data, false) != ESP_OK) {
    ESP_LOGW(TAG, "[SC16] Failed to write register 0x%02X", reg);
  }
}

static uint8_t sc16_read_register(uint8_t channel, uint8_t reg) {
  uint8_t value = 0;
  if (sc16_transfer(sc16_make_address(channel, reg, true), &value, true) != ESP_OK) {
    ESP_LOGW(TAG, "[SC16] Failed to read register 0x%02X", reg);
  }
  return value;
}

static void sc16_hardware_reset(void) {
  gpio_set_level(SC16_RST_PIN, 0);
  vTaskDelay(pdMS_TO_TICKS(20));
  gpio_set_level(SC16_RST_PIN, 1);
  vTaskDelay(pdMS_TO_TICKS(100));
}

static bool sc16_scratchpad_test(void) {
  const uint8_t reg_spr = 0x07;
  sc16_write_register(SC16_CHANNEL_A, reg_spr, 0x5A);
  vTaskDelay(pdMS_TO_TICKS(2));
  const uint8_t a = sc16_read_register(SC16_CHANNEL_A, reg_spr);
  sc16_write_register(SC16_CHANNEL_A, reg_spr, 0xA5);
  vTaskDelay(pdMS_TO_TICKS(2));
  const uint8_t b = sc16_read_register(SC16_CHANNEL_A, reg_spr);
  return a == 0x5A && b == 0xA5;
}

static void sc16_configure_uart(uint8_t channel, uint32_t baud) {
  const uint8_t reg_lcr = 0x03;
  const uint8_t reg_efr = 0x02;
  const uint8_t reg_dll = 0x00;
  const uint8_t reg_dlh = 0x01;
  const uint8_t reg_fcr = 0x02;
  const uint8_t reg_ier = 0x01;

  sc16_write_register(channel, reg_lcr, 0xBF);
  sc16_write_register(channel, reg_efr, 0x10);
  sc16_write_register(channel, reg_lcr, 0x80);

  const uint16_t divisor = (uint16_t)(SC16_XTAL_HZ / (baud * 16UL));
  sc16_write_register(channel, reg_dll, divisor & 0xFF);
  sc16_write_register(channel, reg_dlh, (divisor >> 8) & 0xFF);

  sc16_write_register(channel, reg_lcr, 0x03);
  sc16_write_register(channel, reg_fcr, 0x07);
  vTaskDelay(pdMS_TO_TICKS(2));
  sc16_write_register(channel, reg_fcr, 0x01);
  sc16_write_register(channel, reg_ier, 0x00);
}

static int sc16_rx_available(uint8_t channel) {
  return sc16_read_register(channel, 0x09);
}

static int sc16_read_byte(uint8_t channel) {
  if (sc16_rx_available(channel) <= 0) {
    return -1;
  }
  return sc16_read_register(channel, 0x00);
}

static bool sc16_write_byte(uint8_t channel, uint8_t value) {
  const uint8_t reg_lsr = 0x05;
  const uint8_t reg_thr = 0x00;
  const uint8_t lsr_thr_empty = 0x20;
  const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(100);

  while ((sc16_read_register(channel, reg_lsr) & lsr_thr_empty) == 0) {
    if (xTaskGetTickCount() > deadline) {
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }

  sc16_write_register(channel, reg_thr, value);
  return true;
}

static void sc16_flush_rx(uint8_t channel) {
  const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(150);
  while (sc16_rx_available(channel) > 0) {
    (void)sc16_read_byte(channel);
    if (xTaskGetTickCount() > deadline) {
      break;
    }
  }
}

static bool sc16_write_string(uint8_t channel, const char *text) {
  if (text == NULL) {
    return false;
  }

  for (size_t i = 0; text[i] != '\0'; i++) {
    if (!sc16_write_byte(channel, (uint8_t)text[i])) {
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }

  return true;
}

static size_t read_line_from_sc16(uint8_t channel, char *buffer, size_t buffer_size, uint32_t timeout_ms) {
  size_t length = 0;
  const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);

  while (xTaskGetTickCount() < deadline) {
    while (sc16_rx_available(channel) > 0) {
      const int value = sc16_read_byte(channel);
      if (value < 0) {
        break;
      }

      const char c = (char)value;
      if (c == '\r' || c == '\n') {
        if (length > 0) {
          buffer[length] = '\0';
          trim_ascii(buffer);
          return strlen(buffer);
        }
      } else if (length + 1 < buffer_size) {
        buffer[length++] = c;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  buffer[length] = '\0';
  trim_ascii(buffer);
  return strlen(buffer);
}

static esp_err_t init_sc16_bridge(void) {
  gpio_config_t io_config = {
    .pin_bit_mask = (1ULL << SC16_CS_PIN) | (1ULL << SC16_RST_PIN),
    .mode = GPIO_MODE_OUTPUT,
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE
  };
  ESP_RETURN_ON_ERROR(gpio_config(&io_config), TAG, "gpio_config output failed");

  gpio_config_t irq_config = {
    .pin_bit_mask = (1ULL << SC16_IRQ_PIN),
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE
  };
  ESP_RETURN_ON_ERROR(gpio_config(&irq_config), TAG, "gpio_config input failed");

  gpio_set_level(SC16_CS_PIN, 1);
  gpio_set_level(SC16_RST_PIN, 1);

  spi_bus_config_t bus_config = {
    .mosi_io_num = SPI_MOSI_PIN,
    .miso_io_num = SPI_MISO_PIN,
    .sclk_io_num = SPI_SCK_PIN,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
    .max_transfer_sz = 16
  };
  ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus_config, SPI_DMA_DISABLED), TAG, "spi_bus_initialize failed");

  spi_device_interface_config_t dev_config = {
    .clock_speed_hz = 4000000,
    .mode = 0,
    .spics_io_num = SC16_CS_PIN,
    .queue_size = 1
  };
  ESP_RETURN_ON_ERROR(spi_bus_add_device(SPI2_HOST, &dev_config, &g_sc16_device), TAG, "spi_bus_add_device failed");

  g_spi_ready = true;
  sc16_hardware_reset();

  if (!sc16_scratchpad_test()) {
    ESP_LOGW(TAG, "[SC16] Scratchpad test failed.");
    return ESP_FAIL;
  }

  sc16_configure_uart(SC16_CHANNEL_A, DGS2_BAUD);
  sc16_configure_uart(SC16_CHANNEL_B, DGS2_BAUD);
  ESP_LOGI(TAG, "[SC16] Initialized.");
  return ESP_OK;
}

static esp_err_t initialize_buses_once(void) {
  if (g_buses_initialized) {
    return ESP_OK;
  }

  gpio_config_t led_config = {
    .pin_bit_mask = (1ULL << LED_PIN),
    .mode = GPIO_MODE_OUTPUT,
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE
  };
  ESP_RETURN_ON_ERROR(gpio_config(&led_config), TAG, "led gpio_config failed");
  gpio_set_level(LED_PIN, 1);

  if (init_uart_port(CO_UART_PORT, CO_TX_PIN, CO_RX_PIN) == ESP_OK &&
      init_uart_port(O3_UART_PORT, O3_TX_PIN, O3_RX_PIN) == ESP_OK) {
    g_uart_ready = true;
  }

  if (init_i2c_bus() == ESP_OK) {
    g_i2c_ready = true;
    ESP_LOGI(TAG, "[SPS30] I2C initialized at address 0x%02X.", SPS30_I2C_ADDRESS);
  } else {
    ESP_LOGW(TAG, "[SPS30] I2C initialization failed.");
  }

  if (init_sc16_bridge() == ESP_OK) {
    g_sc16_ready = true;
  } else {
    ESP_LOGW(TAG, "[SC16] Bridge unavailable. NO2 and SO2 will be invalid.");
  }

  g_buses_initialized = true;
  return ESP_OK;
}

static void flush_uart_port(uart_port_t port) {
  ESP_ERROR_CHECK_WITHOUT_ABORT(uart_flush_input(port));
}

static size_t read_line_from_uart(uart_port_t port, char *buffer, size_t buffer_size, uint32_t timeout_ms) {
  size_t length = 0;
  const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);

  while (xTaskGetTickCount() < deadline) {
    uint8_t byte = 0;
    int count = uart_read_bytes(port, &byte, 1, pdMS_TO_TICKS(20));
    if (count <= 0) {
      continue;
    }

    const char c = (char)byte;
    if (c == '\r' || c == '\n') {
      if (length > 0) {
        buffer[length] = '\0';
        trim_ascii(buffer);
        return strlen(buffer);
      }
    } else if (length + 1 < buffer_size) {
      buffer[length++] = c;
    }
  }

  buffer[length] = '\0';
  trim_ascii(buffer);
  return strlen(buffer);
}

static bool parse_dgs2_line(char *line, dgs2_reading_t *out) {
  if (line == NULL || out == NULL) {
    return false;
  }

  trim_ascii(line);
  if (line[0] == '\0') {
    return false;
  }

  char *fields[16] = {0};
  size_t field_count = 0;
  if (!split_csv_line(line, fields, 16, &field_count) || field_count < 7) {
    return false;
  }

  for (size_t i = 0; i < 7; i++) {
    if (fields[i] == NULL || fields[i][0] == '\0') {
      return false;
    }
  }

  if (!is_numeric_string(fields[1]) || !is_numeric_string(fields[2]) || !is_numeric_string(fields[3])) {
    return false;
  }

  snprintf(out->serial_number, sizeof(out->serial_number), "%s", fields[0]);
  out->ppb = (int32_t)strtol(fields[1], NULL, 10);
  out->temperature_c = strtof(fields[2], NULL);
  out->humidity_percent = strtof(fields[3], NULL);

  if (fabsf(out->temperature_c) > 150.0f) {
    out->temperature_c /= 100.0f;
  }
  if (fabsf(out->humidity_percent) > 100.0f) {
    out->humidity_percent /= 100.0f;
  }

  if (!is_finite_float(out->temperature_c) || !is_finite_float(out->humidity_percent)) {
    return false;
  }
  if (out->temperature_c < -50.0f || out->temperature_c > 150.0f) {
    return false;
  }
  if (out->humidity_percent < 0.0f || out->humidity_percent > 100.0f) {
    return false;
  }

  out->valid = true;
  return true;
}

static bool read_dgs2_direct(uart_port_t port, const char *sensor_name, dgs2_reading_t *out) {
  if (out == NULL || !g_uart_ready) {
    return false;
  }

  memset(out, 0, sizeof(*out));
  snprintf(out->sensor_name, sizeof(out->sensor_name), "%s", sensor_name);
  out->ppb = INVALID_INT_SENTINEL;
  out->temperature_c = INVALID_FLOAT_SENTINEL;
  out->humidity_percent = INVALID_FLOAT_SENTINEL;

  for (uint8_t attempt = 1; attempt <= DGS2_MAX_RETRIES; attempt++) {
    flush_uart_port(port);
    if (uart_write_bytes(port, DGS2_SINGLE_MEASUREMENT_COMMAND, strlen(DGS2_SINGLE_MEASUREMENT_COMMAND)) < 0) {
      log_retry(sensor_name, attempt, "uart write failed");
      continue;
    }

    char line[256] = {0};
    read_line_from_uart(port, line, sizeof(line), DGS2_READ_TIMEOUT_MS);
    ESP_LOGI(TAG, "[%s] Raw line: %s", sensor_name, line);

    dgs2_reading_t parsed = {0};
    snprintf(parsed.sensor_name, sizeof(parsed.sensor_name), "%s", sensor_name);
    if (line[0] == '\0') {
      log_retry(sensor_name, attempt, "timeout");
      continue;
    }
    if (!parse_dgs2_line(line, &parsed)) {
      log_retry(sensor_name, attempt, "invalid parse");
      continue;
    }

    ESP_LOGI(TAG, "[%s] PPB=%" PRId32 " TEMP=%.1f RH=%.1f",
      sensor_name, parsed.ppb, parsed.temperature_c, parsed.humidity_percent);
    *out = parsed;
    return true;
  }

  ESP_LOGW(TAG, "[%s] Failed after max retries.", sensor_name);
  return false;
}

static bool read_dgs2_sc16(uint8_t channel, const char *sensor_name, dgs2_reading_t *out) {
  if (out == NULL || !g_sc16_ready) {
    return false;
  }

  memset(out, 0, sizeof(*out));
  snprintf(out->sensor_name, sizeof(out->sensor_name), "%s", sensor_name);
  out->ppb = INVALID_INT_SENTINEL;
  out->temperature_c = INVALID_FLOAT_SENTINEL;
  out->humidity_percent = INVALID_FLOAT_SENTINEL;

  for (uint8_t attempt = 1; attempt <= DGS2_MAX_RETRIES; attempt++) {
    sc16_flush_rx(channel);
    if (!sc16_write_string(channel, DGS2_SINGLE_MEASUREMENT_COMMAND)) {
      log_retry(sensor_name, attempt, "bridge write failed");
      continue;
    }

    char line[256] = {0};
    read_line_from_sc16(channel, line, sizeof(line), DGS2_READ_TIMEOUT_MS);
    ESP_LOGI(TAG, "[%s] Raw line: %s", sensor_name, line);

    dgs2_reading_t parsed = {0};
    snprintf(parsed.sensor_name, sizeof(parsed.sensor_name), "%s", sensor_name);
    if (line[0] == '\0') {
      log_retry(sensor_name, attempt, "timeout");
      continue;
    }
    if (!parse_dgs2_line(line, &parsed)) {
      log_retry(sensor_name, attempt, "invalid parse");
      continue;
    }

    ESP_LOGI(TAG, "[%s] PPB=%" PRId32 " TEMP=%.1f RH=%.1f",
      sensor_name, parsed.ppb, parsed.temperature_c, parsed.humidity_percent);
    *out = parsed;
    return true;
  }

  ESP_LOGW(TAG, "[%s] Failed after max retries.", sensor_name);
  return false;
}

static env_reading_t average_valid_dgs2_environment(const dgs2_reading_t readings[], int count) {
  env_reading_t env = {0};
  float temp_sum = 0.0f;
  float humidity_sum = 0.0f;
  int valid_count = 0;

  for (int i = 0; i < count; i++) {
    if (!readings[i].valid) {
      continue;
    }
    if (!is_finite_float(readings[i].temperature_c) || !is_finite_float(readings[i].humidity_percent)) {
      continue;
    }

    temp_sum += readings[i].temperature_c;
    humidity_sum += readings[i].humidity_percent;
    valid_count++;
  }

  if (valid_count == 0) {
    return env;
  }

  env.valid = true;
  env.temperature_c = temp_sum / valid_count;
  env.humidity_percent = humidity_sum / valid_count;
  return env;
}

static bool ensure_sps30_measurement_running(void) {
  if (!g_i2c_ready) {
    return false;
  }

  for (uint8_t attempt = 1; attempt <= SPS30_MAX_RETRIES; attempt++) {
    const uint8_t payload[2] = {
      SPS30_OUTPUT_FORMAT_FLOAT_MSB,
      SPS30_OUTPUT_FORMAT_FLOAT_LSB
    };
    esp_err_t err = sps30_write_command(SPS30_CMD_START_MEASUREMENT, payload, sizeof(payload));
    if (err == ESP_OK) {
      ESP_LOGI(TAG, "[SPS30] Continuous measurement active.");
      return true;
    }

    ESP_LOGW(TAG, "[SPS30] startMeasurement attempt %u/%u failed: %s",
      attempt, SPS30_MAX_RETRIES, esp_err_to_name(err));
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  return false;
}

static bool read_sps30(sps30_reading_t *out) {
  if (out == NULL) {
    return false;
  }

  memset(out, 0, sizeof(*out));

  for (uint8_t attempt = 1; attempt <= SPS30_MAX_RETRIES; attempt++) {
    bool ready = false;
    const TickType_t ready_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(DGS2_READ_TIMEOUT_MS);

    while (xTaskGetTickCount() < ready_deadline) {
      uint8_t raw_ready[3] = {0};
      uint8_t decoded_ready[2] = {0};
      esp_err_t err = sps30_read_after_command(SPS30_CMD_READ_DATA_READY, raw_ready, sizeof(raw_ready));
      if (err != ESP_OK) {
        ESP_LOGW(TAG, "[SPS30] Data-ready read failed on attempt %u: %s",
          attempt, esp_err_to_name(err));
        break;
      }

      if (!sps30_parse_words_with_crc(raw_ready, sizeof(raw_ready), decoded_ready, sizeof(decoded_ready))) {
        ESP_LOGW(TAG, "[SPS30] Data-ready CRC failed on attempt %u", attempt);
        break;
      }

      if (decoded_ready[1] == 0x01) {
        ready = true;
        break;
      }

      vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (!ready) {
      ESP_LOGW(TAG, "[SPS30] Attempt %u/%u failed: timeout", attempt, SPS30_MAX_RETRIES);
      continue;
    }

    uint8_t raw_measurements[60] = {0};
    uint8_t decoded_measurements[40] = {0};
    esp_err_t err = sps30_read_after_command(
      SPS30_CMD_READ_MEASURED_VALUES,
      raw_measurements,
      sizeof(raw_measurements)
    );
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "[SPS30] Attempt %u/%u failed: read error %s",
        attempt, SPS30_MAX_RETRIES, esp_err_to_name(err));
      continue;
    }

    if (!sps30_parse_words_with_crc(
        raw_measurements,
        sizeof(raw_measurements),
        decoded_measurements,
        sizeof(decoded_measurements))) {
      ESP_LOGW(TAG, "[SPS30] Attempt %u/%u failed: CRC error", attempt, SPS30_MAX_RETRIES);
      continue;
    }

    const float mc1p0 = sps30_decode_float_be(&decoded_measurements[0]);
    const float mc2p5 = sps30_decode_float_be(&decoded_measurements[4]);
    const float mc10p0 = sps30_decode_float_be(&decoded_measurements[12]);

    if (!is_finite_float(mc1p0) || !is_finite_float(mc2p5) || !is_finite_float(mc10p0)) {
      ESP_LOGW(TAG, "[SPS30] Attempt %u/%u failed: invalid numeric values", attempt, SPS30_MAX_RETRIES);
      continue;
    }

    out->valid = true;
    out->pm1_0 = mc1p0;
    out->pm2_5 = mc2p5;
    out->pm10 = mc10p0;

    ESP_LOGI(TAG, "[SPS30] PM1=%.2f PM2.5=%.2f PM10=%.2f", out->pm1_0, out->pm2_5, out->pm10);
    return true;
  }

  ESP_LOGW(TAG, "[SPS30] Failed after max retries.");
  return false;
}

static void collect_gas_and_environment(void) {
  read_dgs2_direct(CO_UART_PORT, "CO", &g_co_reading);
  read_dgs2_direct(O3_UART_PORT, "O3", &g_o3_reading);

  if (g_sc16_ready) {
    read_dgs2_sc16(SC16_CHANNEL_B, "NO2", &g_no2_reading);
    read_dgs2_sc16(SC16_CHANNEL_A, "SO2", &g_so2_reading);
  }

  const dgs2_reading_t env_inputs[4] = {
    g_co_reading,
    g_o3_reading,
    g_no2_reading,
    g_so2_reading
  };
  g_env_reading = average_valid_dgs2_environment(env_inputs, 4);

  if (g_env_reading.valid) {
    ESP_LOGI(TAG, "[ENV] Averaged TEMP=%.1f RH=%.1f",
      g_env_reading.temperature_c, g_env_reading.humidity_percent);
  } else {
    ESP_LOGW(TAG, "[ENV] No valid DGS2 temperature/humidity values.");
  }
}

bool sensor_reader_collect(sensor_reading_t *reading) {
  if (reading == NULL) {
    return false;
  }

  if (initialize_buses_once() != ESP_OK) {
    return false;
  }

  if (!g_sps30_ready) {
    g_sps30_ready = ensure_sps30_measurement_running();
  }

  if (g_sps30_ready) {
    read_sps30(&g_sps30_reading);
  } else {
    ESP_LOGW(TAG, "[SPS30] Sensor unavailable. PM values marked invalid.");
  }
  collect_gas_and_environment();

  memset(reading, 0, sizeof(*reading));
  snprintf(reading->device_id, sizeof(reading->device_id), "%s", DEVICE_ID);
  snprintf(reading->firmware_version, sizeof(reading->firmware_version), "%s", FIRMWARE_VERSION);
  format_now_iso8601(reading->measured_at, sizeof(reading->measured_at));

  reading->pm1 = g_sps30_reading.valid ? g_sps30_reading.pm1_0 : 0.0f;
  reading->pm25 = g_sps30_reading.valid ? g_sps30_reading.pm2_5 : 0.0f;
  reading->pm10 = g_sps30_reading.valid ? g_sps30_reading.pm10 : 0.0f;
  reading->co = normalized_dgs2_ppb(&g_co_reading);
  reading->no2 = normalized_dgs2_ppb(&g_no2_reading);
  reading->o3 = normalized_dgs2_ppb(&g_o3_reading);
  reading->so2 = normalized_dgs2_ppb(&g_so2_reading);
  reading->temperature_c = g_env_reading.valid ? g_env_reading.temperature_c : 0.0f;
  reading->humidity_percent = g_env_reading.valid ? g_env_reading.humidity_percent : 0.0f;
  reading->battery_voltage = sensor_reader_get_battery_voltage();
  reading->solar_voltage = sensor_reader_get_solar_voltage();
  reading->signal_strength = sensor_reader_get_signal_strength();
  reading->alarm_flags = sensor_reader_compute_alarm_flags(reading);

  ESP_LOGI(TAG, "Collected sensor reading at %s", reading->measured_at);
  return true;
}

int sensor_reader_get_signal_strength(void) {
  wifi_ap_record_t ap_info = {0};
  if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
    return ap_info.rssi;
  }
  return -127;
}

float sensor_reader_get_battery_voltage(void) {
  // Integration point: replace with calibrated ADC read for the battery rail.
  return 3.91f;
}

float sensor_reader_get_solar_voltage(void) {
  // Integration point: replace with calibrated ADC read for the solar rail.
  return 5.14f;
}

uint32_t sensor_reader_compute_alarm_flags(const sensor_reading_t *reading) {
  uint32_t flags = 0;
  if (reading->pm25 > 35.0f) {
    flags |= 1u << 0;
  }
  if (reading->battery_voltage < 3.5f) {
    flags |= 1u << 1;
  }
  return flags;
}
