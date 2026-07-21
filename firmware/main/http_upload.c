#include "http_upload.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "device_auth.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "reading_json.h"

static const char *TAG = "http_upload";

typedef struct {
  char data[4096];
  size_t length;
  bool overflowed;
} response_buffer_t;

static response_buffer_t response_buffer;

static esp_err_t http_event_handler(esp_http_client_event_t *event) {
  response_buffer_t *response = (response_buffer_t *)event->user_data;
  if (event->event_id != HTTP_EVENT_ON_DATA || response == NULL || event->data_len <= 0) {
    return ESP_OK;
  }

  size_t available = sizeof(response->data) - response->length - 1;
  if ((size_t)event->data_len > available) {
    response->overflowed = true;
    return ESP_OK;
  }

  memcpy(response->data + response->length, event->data, (size_t)event->data_len);
  response->length += (size_t)event->data_len;
  response->data[response->length] = '\0';
  return ESP_OK;
}

static bool contains_sequence(const uint32_t *values, size_t count, uint32_t value) {
  for (size_t index = 0; index < count; index++) {
    if (values[index] == value) return true;
  }
  return false;
}

static bool parse_sequence_array(
  const char *json,
  const char *key,
  uint32_t *output,
  size_t *output_count,
  size_t output_capacity
) {
  char marker[64];
  int marker_length = snprintf(marker, sizeof(marker), "\"%s\"", key);
  if (marker_length <= 0 || (size_t)marker_length >= sizeof(marker)) return false;

  const char *cursor = strstr(json, marker);
  if (cursor == NULL) return false;
  cursor = strchr(cursor + marker_length, '[');
  if (cursor == NULL) return false;
  cursor++;

  while (true) {
    while (isspace((unsigned char)*cursor) || *cursor == ',') cursor++;
    if (*cursor == ']') return true;
    if (!isdigit((unsigned char)*cursor)) return false;

    char *end = NULL;
    unsigned long parsed = strtoul(cursor, &end, 10);
    if (end == cursor || parsed > UINT32_MAX) return false;
    if (!contains_sequence(output, *output_count, (uint32_t)parsed)) {
      if (*output_count >= output_capacity) return false;
      output[(*output_count)++] = (uint32_t)parsed;
    }
    cursor = end;
  }
}

bool http_upload_batch(
  const sensor_reading_t *const *readings,
  size_t reading_count,
  const char *batch_id,
  batch_upload_result_t *result,
  int *status_code
) {
  if (
    readings == NULL || reading_count == 0 ||
    reading_count > HTTP_UPLOAD_MAX_BATCH_SIZE || batch_id == NULL || result == NULL
  ) {
    return false;
  }

  memset(result, 0, sizeof(*result));
  if (status_code != NULL) *status_code = 0;
  char *payload = reading_json_serialize_batch(DEVICE_ID, batch_id, readings, reading_count);
  if (payload == NULL) return false;

  char timestamp[32];
  char signature_hex[65];
  if (
    !device_auth_build_timestamp(timestamp, sizeof(timestamp)) ||
    !device_auth_build_signature(payload, batch_id, timestamp, signature_hex, sizeof(signature_hex))
  ) {
    reading_json_free(payload);
    return false;
  }

  char credential_version[8];
  snprintf(credential_version, sizeof(credential_version), "%d", DEVICE_CREDENTIAL_VERSION);
  memset(&response_buffer, 0, sizeof(response_buffer));
  esp_http_client_config_t client_config = {
    .url = NODE_SERVER_URL,
    .method = HTTP_METHOD_POST,
    .timeout_ms = 30000,
    .transport_type = HTTP_TRANSPORT_OVER_SSL,
    .crt_bundle_attach = esp_crt_bundle_attach,
    .skip_cert_common_name_check = false,
    .event_handler = http_event_handler,
    .user_data = &response_buffer
  };

  esp_http_client_handle_t client = esp_http_client_init(&client_config);
  if (client == NULL) {
    reading_json_free(payload);
    return false;
  }

  esp_http_client_set_header(client, "Content-Type", "application/json");
  esp_http_client_set_header(client, "x-device-id", DEVICE_ID);
  esp_http_client_set_header(client, "x-batch-id", batch_id);
  esp_http_client_set_header(client, "x-timestamp", timestamp);
  esp_http_client_set_header(client, "x-credential-version", credential_version);
  esp_http_client_set_header(client, "x-signature", signature_hex);
  esp_http_client_set_post_field(client, payload, (int)strlen(payload));

  esp_err_t error = esp_http_client_perform(client);
  if (error != ESP_OK) {
    ESP_LOGW(TAG, "Batch upload failed: %s", esp_err_to_name(error));
    esp_http_client_cleanup(client);
    reading_json_free(payload);
    return false;
  }

  int response_code = esp_http_client_get_status_code(client);
  if (status_code != NULL) *status_code = response_code;
  ESP_LOGI(TAG, "Managed API returned HTTP %d for batch %s", response_code, batch_id);

  bool parsed = false;
  if (response_code == 200 && !response_buffer.overflowed) {
    parsed =
      parse_sequence_array(response_buffer.data, "acceptedSequenceNumbers", result->confirmed, &result->confirmed_count, HTTP_UPLOAD_MAX_BATCH_SIZE) &&
      parse_sequence_array(response_buffer.data, "duplicateSequenceNumbers", result->confirmed, &result->confirmed_count, HTTP_UPLOAD_MAX_BATCH_SIZE) &&
      parse_sequence_array(response_buffer.data, "conflictingSequenceNumbers", result->terminal, &result->terminal_count, HTTP_UPLOAD_MAX_BATCH_SIZE) &&
      parse_sequence_array(response_buffer.data, "rejectedSequenceNumbers", result->terminal, &result->terminal_count, HTTP_UPLOAD_MAX_BATCH_SIZE);
  }
  if (response_buffer.overflowed) ESP_LOGW(TAG, "Managed API response exceeded local response buffer");

  esp_http_client_cleanup(client);
  reading_json_free(payload);
  return response_code == 200 && parsed;
}
