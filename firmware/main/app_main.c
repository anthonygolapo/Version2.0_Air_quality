#include <stdbool.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "config.h"
#include "esp_log.h"
#include "esp_random.h"
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
static const size_t MAX_BATCHES_PER_CYCLE = 3;

static uint32_t compute_backoff_ms(uint32_t attempt) {
  uint32_t capped_attempt = attempt > 6 ? 6 : attempt;
  uint32_t base = 5000u * (1u << capped_attempt);
  uint32_t jitter = esp_random() % 1000u;
  return base + jitter;
}

static bool sequence_in_list(uint32_t sequence, const uint32_t *values, size_t count) {
  for (size_t index = 0; index < count; index++) {
    if (values[index] == sequence) return true;
  }
  return false;
}

static void flush_ready_batches(void) {
  uint32_t attempt = 0;
  size_t batches_sent = 0;

  while (local_queue_count() >= HTTP_UPLOAD_MAX_BATCH_SIZE && batches_sent < MAX_BATCHES_PER_CYCLE) {
    queued_reading_t queued[HTTP_UPLOAD_MAX_BATCH_SIZE] = {0};
    size_t queued_count = local_queue_peek_oldest_batch(queued, HTTP_UPLOAD_MAX_BATCH_SIZE);
    if (queued_count != HTTP_UPLOAD_MAX_BATCH_SIZE) {
      return;
    }

    const sensor_reading_t *readings[HTTP_UPLOAD_MAX_BATCH_SIZE];
    for (size_t index = 0; index < queued_count; index++) readings[index] = &queued[index].reading;

    char batch_id[64];
    snprintf(
      batch_id,
      sizeof(batch_id),
      "%s-%010u-%010u",
      DEVICE_ID,
      (unsigned)queued[0].reading.sequence_number,
      (unsigned)queued[queued_count - 1].reading.sequence_number
    );

    int status_code = 0;
    batch_upload_result_t result;
    if (http_upload_batch(readings, queued_count, batch_id, &result, &status_code)) {
      size_t handled = 0;
      for (size_t index = 0; index < queued_count; index++) {
        uint32_t sequence = queued[index].reading.sequence_number;
        if (sequence_in_list(sequence, result.confirmed, result.confirmed_count)) {
          if (local_queue_delete(queued[index].path)) {
            handled++;
            ESP_LOGI(TAG, "Convex confirmed and deleted reading %u", (unsigned)sequence);
          } else {
            ESP_LOGW(TAG, "Convex confirmed reading %u but local deletion failed", (unsigned)sequence);
          }
        } else if (sequence_in_list(sequence, result.terminal, result.terminal_count)) {
          if (local_queue_mark_dead_letter(queued[index].path)) {
            handled++;
            ESP_LOGE(TAG, "Reading %u moved to dead letter after permanent rejection", (unsigned)sequence);
          } else {
            ESP_LOGW(TAG, "Failed to dead-letter reading %u", (unsigned)sequence);
          }
        }
      }

      if (handled == 0) {
        ESP_LOGW(TAG, "Batch %s returned no terminal sequence results; retaining all files", batch_id);
        return;
      }

      attempt = 0;
      batches_sent++;
      continue;
    }

    attempt++;
    uint32_t delay_ms = compute_backoff_ms(attempt);
    bool retryable = status_code == 0 || status_code == 429 || status_code >= 500;
    ESP_LOGW(TAG, "Batch %s not confirmed, HTTP=%d retryable=%s retry_in_ms=%u",
      batch_id, status_code, retryable ? "true" : "false", (unsigned)delay_ms);
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

    flush_ready_batches();
    ESP_LOGI(TAG, "Queue depth after cycle: %u", (unsigned)local_queue_count());
    vTaskDelay(pdMS_TO_TICKS(UPLOAD_INTERVAL_MS));
  }
}
