#include "local_queue.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cJSON.h"
#include "config.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "sensor_reader.h"

static const char *TAG = "local_queue";
static const char *QUEUE_DIR = "/spiffs/queue";
static const char *SEQ_FILE = "/spiffs/sequence.txt";

static bool ensure_queue_dir(void) {
  struct stat st;
  if (stat(QUEUE_DIR, &st) == 0) {
    return true;
  }
  return mkdir(QUEUE_DIR, 0777) == 0;
}

bool local_queue_init(void) {
  return ensure_queue_dir();
}

bool local_queue_next_sequence_number(uint32_t *sequence_number) {
  if (sequence_number == NULL) {
    return false;
  }

  uint32_t current = 0;
  FILE *input = fopen(SEQ_FILE, "r");
  if (input != NULL) {
    if (fscanf(input, "%u", &current) != 1) {
      current = 0;
    }
    fclose(input);
  }

  current += 1;

  FILE *output = fopen(SEQ_FILE, "w");
  if (output == NULL) {
    return false;
  }
  fprintf(output, "%u", current);
  fclose(output);

  *sequence_number = current;
  return true;
}

static cJSON *reading_to_json(const sensor_reading_t *reading) {
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
  return json;
}

static bool json_to_reading(cJSON *json, sensor_reading_t *reading) {
  if (json == NULL || reading == NULL) {
    return false;
  }

  cJSON *device_id = cJSON_GetObjectItemCaseSensitive(json, "deviceId");
  cJSON *sequence_number = cJSON_GetObjectItemCaseSensitive(json, "sequenceNumber");
  cJSON *measured_at = cJSON_GetObjectItemCaseSensitive(json, "measuredAt");
  cJSON *pm1 = cJSON_GetObjectItemCaseSensitive(json, "pm1");
  cJSON *pm25 = cJSON_GetObjectItemCaseSensitive(json, "pm25");
  cJSON *pm10 = cJSON_GetObjectItemCaseSensitive(json, "pm10");
  cJSON *co = cJSON_GetObjectItemCaseSensitive(json, "co");
  cJSON *no2 = cJSON_GetObjectItemCaseSensitive(json, "no2");
  cJSON *o3 = cJSON_GetObjectItemCaseSensitive(json, "o3");
  cJSON *so2 = cJSON_GetObjectItemCaseSensitive(json, "so2");
  cJSON *temperature_c = cJSON_GetObjectItemCaseSensitive(json, "temperatureC");
  cJSON *humidity_percent = cJSON_GetObjectItemCaseSensitive(json, "humidityPercent");
  cJSON *battery_voltage = cJSON_GetObjectItemCaseSensitive(json, "batteryVoltage");
  cJSON *solar_voltage = cJSON_GetObjectItemCaseSensitive(json, "solarVoltage");
  cJSON *signal_strength = cJSON_GetObjectItemCaseSensitive(json, "signalStrength");
  cJSON *firmware_version = cJSON_GetObjectItemCaseSensitive(json, "firmwareVersion");
  cJSON *alarm_flags = cJSON_GetObjectItemCaseSensitive(json, "alarmFlags");

  if (!cJSON_IsString(device_id) ||
      !cJSON_IsNumber(sequence_number) ||
      !cJSON_IsString(measured_at) ||
      !cJSON_IsNumber(pm1) ||
      !cJSON_IsNumber(pm25) ||
      !cJSON_IsNumber(pm10) ||
      !cJSON_IsNumber(co) ||
      !cJSON_IsNumber(no2) ||
      !cJSON_IsNumber(o3) ||
      !cJSON_IsNumber(so2) ||
      !cJSON_IsNumber(temperature_c) ||
      !cJSON_IsNumber(humidity_percent) ||
      !cJSON_IsNumber(battery_voltage) ||
      !cJSON_IsNumber(solar_voltage) ||
      !cJSON_IsNumber(signal_strength) ||
      !cJSON_IsString(firmware_version) ||
      !cJSON_IsNumber(alarm_flags)) {
    return false;
  }

  memset(reading, 0, sizeof(*reading));
  snprintf(reading->device_id, sizeof(reading->device_id), "%s", cJSON_GetStringValue(device_id));
  reading->sequence_number = (uint32_t)cJSON_GetNumberValue(sequence_number);
  snprintf(reading->measured_at, sizeof(reading->measured_at), "%s", cJSON_GetStringValue(measured_at));
  reading->pm1 = (float)cJSON_GetNumberValue(pm1);
  reading->pm25 = (float)cJSON_GetNumberValue(pm25);
  reading->pm10 = (float)cJSON_GetNumberValue(pm10);
  reading->co = (float)cJSON_GetNumberValue(co);
  reading->no2 = (float)cJSON_GetNumberValue(no2);
  reading->o3 = (float)cJSON_GetNumberValue(o3);
  reading->so2 = (float)cJSON_GetNumberValue(so2);
  reading->temperature_c = (float)cJSON_GetNumberValue(temperature_c);
  reading->humidity_percent = (float)cJSON_GetNumberValue(humidity_percent);
  reading->battery_voltage = (float)cJSON_GetNumberValue(battery_voltage);
  reading->solar_voltage = (float)cJSON_GetNumberValue(solar_voltage);
  reading->signal_strength = (int)cJSON_GetNumberValue(signal_strength);
  snprintf(reading->firmware_version, sizeof(reading->firmware_version), "%s", cJSON_GetStringValue(firmware_version));
  reading->alarm_flags = (uint32_t)cJSON_GetNumberValue(alarm_flags);
  return true;
}

bool local_queue_store(const sensor_reading_t *reading) {
  if (reading == NULL || !ensure_queue_dir()) {
    return false;
  }

  cJSON *json = reading_to_json(reading);
  if (json == NULL) {
    return false;
  }

  char *serialized = cJSON_PrintUnformatted(json);
  cJSON_Delete(json);
  if (serialized == NULL) {
    return false;
  }

  char path[96];
  snprintf(path, sizeof(path), "%s/%010u.json", QUEUE_DIR, reading->sequence_number);

  FILE *file = fopen(path, "w");
  if (file == NULL) {
    cJSON_free(serialized);
    return false;
  }

  fprintf(file, "%s", serialized);
  fclose(file);
  cJSON_free(serialized);
  ESP_LOGI(TAG, "Queued reading %u", reading->sequence_number);
  return true;
}

bool local_queue_peek_oldest(queued_reading_t *queued) {
  if (queued == NULL) {
    return false;
  }

  DIR *dir = opendir(QUEUE_DIR);
  if (dir == NULL) {
    return false;
  }

  struct dirent *entry;
  char oldest_name[64] = {0};

  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_name[0] == '.') {
      continue;
    }
    if (oldest_name[0] == '\0' || strcmp(entry->d_name, oldest_name) < 0) {
      snprintf(oldest_name, sizeof(oldest_name), "%s", entry->d_name);
    }
  }
  closedir(dir);

  if (oldest_name[0] == '\0') {
    return false;
  }

  snprintf(queued->path, sizeof(queued->path), "%s/%s", QUEUE_DIR, oldest_name);
  FILE *file = fopen(queued->path, "r");
  if (file == NULL) {
    return false;
  }

  fseek(file, 0, SEEK_END);
  long length = ftell(file);
  fseek(file, 0, SEEK_SET);

  char *buffer = calloc((size_t)length + 1, sizeof(char));
  if (buffer == NULL) {
    fclose(file);
    return false;
  }

  fread(buffer, 1, (size_t)length, file);
  fclose(file);

  cJSON *json = cJSON_Parse(buffer);
  free(buffer);
  if (json == NULL) {
    return false;
  }

  bool ok = json_to_reading(json, &queued->reading);
  cJSON_Delete(json);
  return ok;
}

bool local_queue_delete(const char *path) {
  if (path == NULL) {
    return false;
  }
  return unlink(path) == 0;
}

size_t local_queue_count(void) {
  size_t count = 0;
  DIR *dir = opendir(QUEUE_DIR);
  if (dir == NULL) {
    return 0;
  }

  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_name[0] != '.') {
      count++;
    }
  }
  closedir(dir);
  return count;
}
