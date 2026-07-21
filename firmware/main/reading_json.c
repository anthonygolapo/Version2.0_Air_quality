#include "reading_json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *reading_json_serialize(const sensor_reading_t *reading) {
  if (reading == NULL) {
    return NULL;
  }

  const char *format =
    "{\"deviceId\":\"%s\",\"sequenceNumber\":%u,\"measuredAt\":\"%s\","
    "\"pm1\":%.3f,\"pm25\":%.3f,\"pm10\":%.3f,\"co\":%.3f,\"no2\":%.3f,"
    "\"o3\":%.3f,\"so2\":%.3f,\"temperatureC\":%.3f,\"humidityPercent\":%.3f,"
    "\"batteryVoltage\":%.3f,\"solarVoltage\":%.3f,\"signalStrength\":%d,"
    "\"firmwareVersion\":\"%s\",\"alarmFlags\":%u}";

  int length = snprintf(
    NULL,
    0,
    format,
    reading->device_id,
    (unsigned)reading->sequence_number,
    reading->measured_at,
    reading->pm1,
    reading->pm25,
    reading->pm10,
    reading->co,
    reading->no2,
    reading->o3,
    reading->so2,
    reading->temperature_c,
    reading->humidity_percent,
    reading->battery_voltage,
    reading->solar_voltage,
    reading->signal_strength,
    reading->firmware_version,
    (unsigned)reading->alarm_flags
  );
  if (length <= 0) {
    return NULL;
  }

  char *json = calloc((size_t)length + 1, sizeof(char));
  if (json == NULL) {
    return NULL;
  }

  snprintf(
    json,
    (size_t)length + 1,
    format,
    reading->device_id,
    (unsigned)reading->sequence_number,
    reading->measured_at,
    reading->pm1,
    reading->pm25,
    reading->pm10,
    reading->co,
    reading->no2,
    reading->o3,
    reading->so2,
    reading->temperature_c,
    reading->humidity_percent,
    reading->battery_voltage,
    reading->solar_voltage,
    reading->signal_strength,
    reading->firmware_version,
    (unsigned)reading->alarm_flags
  );

  return json;
}

char *reading_json_serialize_batch(
  const char *device_id,
  const char *batch_id,
  const sensor_reading_t *const *readings,
  size_t reading_count
) {
  if (device_id == NULL || batch_id == NULL || readings == NULL || reading_count == 0) {
    return NULL;
  }

  char **serialized = calloc(reading_count, sizeof(char *));
  if (serialized == NULL) {
    return NULL;
  }

  int prefix_length = snprintf(
    NULL,
    0,
    "{\"deviceId\":\"%s\",\"batchId\":\"%s\",\"readings\":[",
    device_id,
    batch_id
  );
  if (prefix_length <= 0) {
    free(serialized);
    return NULL;
  }

  size_t total_length = (size_t)prefix_length + 2;
  for (size_t index = 0; index < reading_count; index++) {
    serialized[index] = reading_json_serialize(readings[index]);
    if (serialized[index] == NULL) {
      for (size_t cleanup = 0; cleanup < index; cleanup++) free(serialized[cleanup]);
      free(serialized);
      return NULL;
    }
    total_length += strlen(serialized[index]) + (index > 0 ? 1 : 0);
  }

  char *json = calloc(total_length + 1, sizeof(char));
  if (json == NULL) {
    for (size_t index = 0; index < reading_count; index++) free(serialized[index]);
    free(serialized);
    return NULL;
  }

  size_t offset = (size_t)snprintf(
    json,
    total_length + 1,
    "{\"deviceId\":\"%s\",\"batchId\":\"%s\",\"readings\":[",
    device_id,
    batch_id
  );
  for (size_t index = 0; index < reading_count; index++) {
    if (index > 0) json[offset++] = ',';
    size_t item_length = strlen(serialized[index]);
    memcpy(json + offset, serialized[index], item_length);
    offset += item_length;
    free(serialized[index]);
  }
  memcpy(json + offset, "]}", 3);
  free(serialized);
  return json;
}

bool reading_json_deserialize(const char *json, sensor_reading_t *reading) {
  if (json == NULL || reading == NULL) {
    return false;
  }

  sensor_reading_t parsed = {0};
  unsigned sequence_number = 0;
  unsigned alarm_flags = 0;

  int matched = sscanf(
    json,
    "{\"deviceId\":\"%31[^\"]\",\"sequenceNumber\":%u,\"measuredAt\":\"%31[^\"]\","
    "\"pm1\":%f,\"pm25\":%f,\"pm10\":%f,\"co\":%f,\"no2\":%f,"
    "\"o3\":%f,\"so2\":%f,\"temperatureC\":%f,\"humidityPercent\":%f,"
    "\"batteryVoltage\":%f,\"solarVoltage\":%f,\"signalStrength\":%d,"
    "\"firmwareVersion\":\"%31[^\"]\",\"alarmFlags\":%u}",
    parsed.device_id,
    &sequence_number,
    parsed.measured_at,
    &parsed.pm1,
    &parsed.pm25,
    &parsed.pm10,
    &parsed.co,
    &parsed.no2,
    &parsed.o3,
    &parsed.so2,
    &parsed.temperature_c,
    &parsed.humidity_percent,
    &parsed.battery_voltage,
    &parsed.solar_voltage,
    &parsed.signal_strength,
    parsed.firmware_version,
    &alarm_flags
  );

  if (matched != 17) {
    return false;
  }

  parsed.sequence_number = (uint32_t)sequence_number;
  parsed.alarm_flags = (uint32_t)alarm_flags;
  *reading = parsed;
  return true;
}

void reading_json_free(char *json) {
  free(json);
}
