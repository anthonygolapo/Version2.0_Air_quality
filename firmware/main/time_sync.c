#include "time_sync.h"

#include <time.h>

#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "time_sync";

bool time_sync_wait_for_time(void) {
  esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_init();

  for (int attempt = 0; attempt < 20; attempt++) {
    time_t now = time(NULL);
    if (now > 1700000000) {
      ESP_LOGI(TAG, "Time synchronized");
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }

  ESP_LOGW(TAG, "Time synchronization timed out");
  return false;
}
