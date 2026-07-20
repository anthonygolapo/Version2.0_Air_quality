#include "spiffs_init.h"

#include "esp_log.h"
#include "esp_spiffs.h"

static const char *TAG = "spiffs";

bool spiffs_init(void) {
  esp_vfs_spiffs_conf_t conf = {
    .base_path = "/spiffs",
    .partition_label = NULL,
    .max_files = 8,
    .format_if_mount_failed = true
  };

  esp_err_t err = esp_vfs_spiffs_register(&conf);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to mount SPIFFS: %s", esp_err_to_name(err));
    return false;
  }

  size_t total = 0;
  size_t used = 0;
  esp_spiffs_info(NULL, &total, &used);
  ESP_LOGI(TAG, "SPIFFS mounted: total=%u used=%u", (unsigned)total, (unsigned)used);
  return true;
}
