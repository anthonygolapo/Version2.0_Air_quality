#include "http_upload.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "config.h"
#include "device_auth.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

static const char *TAG = "http_upload";

static char *reading_to_json_string(const sensor_reading_t *reading) {
  cJSON *json = cJSON_CreateObject();
  if (json == NULL) {
    return NULL;
  }

  cJSON_AddStringToObject(json, "deviceId", reading->device_id);
  cJSON_AddNumberToObject(json, "sequenceNumber", reading->sequence_number);
  cJSON_AddStringToObject(json, "measuredAt", reading->measured_at);
  cJSON_AddNumberToObject(json, "pm1", reading->pm1);
  cJSON_AddNumberToObject(json, "pm25", reading->pm25);
  cJSON_AddNumberToObject(json, "pm10", reading->pm10);
  cJSON_AddNumberToObject(json, "co", reading->co);
  cJSON_AddNumberToObject(json, "no2", reading->no2);
  cJSON_AddNumberToObject(json, "o3", reading->o3);
  cJSON_AddNumberToObject(json, "so2", reading->so2);
  cJSON_AddNumberToObject(json, "temperatureC", reading->temperature_c);
  cJSON_AddNumberToObject(json, "humidityPercent", reading->humidity_percent);
  cJSON_AddNumberToObject(json, "batteryVoltage", reading->battery_voltage);
  cJSON_AddNumberToObject(json, "solarVoltage", reading->solar_voltage);
  cJSON_AddNumberToObject(json, "signalStrength", reading->signal_strength);
  cJSON_AddStringToObject(json, "firmwareVersion", reading->firmware_version);
  cJSON_AddNumberToObject(json, "alarmFlags", reading->alarm_flags);

  char *payload = cJSON_PrintUnformatted(json);
  cJSON_Delete(json);
  return payload;
}

bool http_upload_reading(const sensor_reading_t *reading, int *status_code) {
  if (reading == NULL) {
    return false;
  }

  char *payload = reading_to_json_string(reading);
  if (payload == NULL) {
    return false;
  }

  char timestamp[32];
  if (!device_auth_build_timestamp(timestamp, sizeof(timestamp))) {
    cJSON_free(payload);
    return false;
  }

  char signature_hex[65];
  if (!device_auth_build_signature(payload, reading->sequence_number, timestamp, signature_hex, sizeof(signature_hex))) {
    cJSON_free(payload);
    return false;
  }

  char sequence_number[16];
  snprintf(sequence_number, sizeof(sequence_number), "%u", (unsigned)reading->sequence_number);

  char credential_version[8];
  snprintf(credential_version, sizeof(credential_version), "%d", DEVICE_CREDENTIAL_VERSION);

  esp_http_client_config_t client_config = {
    .url = NODE_SERVER_URL,
    .method = HTTP_METHOD_POST,
    .timeout_ms = 15000,
    .transport_type = HTTP_TRANSPORT_OVER_SSL,
    .crt_bundle_attach = esp_crt_bundle_attach,
    .skip_cert_common_name_check = false
  };

  esp_http_client_handle_t client = esp_http_client_init(&client_config);
  if (client == NULL) {
    cJSON_free(payload);
    return false;
  }

  esp_http_client_set_header(client, "Content-Type", "application/json");
  esp_http_client_set_header(client, "x-device-id", DEVICE_ID);
  esp_http_client_set_header(client, "x-sequence-number", sequence_number);
  esp_http_client_set_header(client, "x-timestamp", timestamp);
  esp_http_client_set_header(client, "x-credential-version", credential_version);
  esp_http_client_set_header(client, "x-signature", signature_hex);
  esp_http_client_set_post_field(client, payload, (int)strlen(payload));

  esp_err_t err = esp_http_client_perform(client);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Upload failed: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    cJSON_free(payload);
    return false;
  }

  int response_code = esp_http_client_get_status_code(client);
  if (status_code != NULL) {
    *status_code = response_code;
  }

  ESP_LOGI(TAG, "Managed API returned HTTP %d", response_code);
  esp_http_client_cleanup(client);
  cJSON_free(payload);
  return response_code == 202;
}
