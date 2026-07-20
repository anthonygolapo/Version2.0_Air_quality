#include <stdbool.h>
#include <math.h>
#include <stdint.h>

#include "config.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "http_upload.h"
#include "local_queue.h"
#include "sensor_reader.h"
#include "spiffs_init.h"
#include "time_sync.h"
#include "wifi_manager.h"

static const char *TAG = "app_main";

static uint32_t compute_backoff_ms(uint32_t attempt) {
  uint32_t capped_attempt = attempt > 6 ? 6 : attempt;
  uint32_t base = 5000u * (1u << capped_attempt);
  uint32_t jitter = esp_random() % 1000u;
  return base + jitter;
}

static void flush_queue_once(void) {
  queued_reading_t queued;
  uint32_t attempt = 0;

  while (local_queue_peek_oldest(&queued)) {
    int status_code = 0;
    if (http_upload_reading(&queued.reading, &status_code)) {
      if (!local_queue_delete(queued.path)) {
        ESP_LOGW(TAG, "Uploaded reading %u but failed to delete queue file %s",
          queued.reading.sequence_number, queued.path);
        return;
      }
      attempt = 0;
      ESP_LOGI(TAG, "Delivered and deleted reading %u", queued.reading.sequence_number);
      continue;
    }

    attempt++;
    uint32_t delay_ms = compute_backoff_ms(attempt);
    bool retryable = status_code == 0 || status_code == 429 || status_code == 500 || status_code == 503;
    ESP_LOGW(TAG, "Reading %u not accepted yet, HTTP=%d retryable=%s retry_in_ms=%u",
      queued.reading.sequence_number, status_code, retryable ? "true" : "false", delay_ms);
    if (!retryable) {
      delay_ms = UPLOAD_INTERVAL_MS;
    }
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    break;
  }
}

void app_main(void) {
  ESP_LOGI(TAG, "Starting air-quality uploader firmware");

  if (!spiffs_init() || !local_queue_init()) {
    ESP_LOGE(TAG, "Local persistent queue initialization failed");
    return;
  }

  sensor_reading_t reading;

  while (true) {
    if (!wifi_manager_connect()) {
      ESP_LOGW(TAG, "Wi-Fi unavailable; keeping readings on local queue");
    } else {
      time_sync_wait_for_time();
    }

    if (sensor_reader_collect(&reading) && local_queue_next_sequence_number(&reading.sequence_number)) {
      if (!local_queue_store(&reading)) {
        ESP_LOGE(TAG, "Failed to persist reading before upload attempt");
      }
    } else {
      ESP_LOGE(TAG, "Failed to collect reading or allocate sequence number");
    }

    flush_queue_once();
    ESP_LOGI(TAG, "Queue depth after cycle: %u", (unsigned)local_queue_count());
    vTaskDelay(pdMS_TO_TICKS(UPLOAD_INTERVAL_MS));
  }
}
