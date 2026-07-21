#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "sensor_reader.h"

char *reading_json_serialize(const sensor_reading_t *reading);
char *reading_json_serialize_batch(
  const char *device_id,
  const char *batch_id,
  const sensor_reading_t *const *readings,
  size_t reading_count
);
bool reading_json_deserialize(const char *json, sensor_reading_t *reading);
void reading_json_free(char *json);
