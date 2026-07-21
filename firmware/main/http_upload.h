#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sensor_reader.h"

#define HTTP_UPLOAD_MAX_BATCH_SIZE 10

typedef struct {
  uint32_t confirmed[HTTP_UPLOAD_MAX_BATCH_SIZE];
  size_t confirmed_count;
  uint32_t terminal[HTTP_UPLOAD_MAX_BATCH_SIZE];
  size_t terminal_count;
} batch_upload_result_t;

bool http_upload_batch(
  const sensor_reading_t *const *readings,
  size_t reading_count,
  const char *batch_id,
  batch_upload_result_t *result,
  int *status_code
);
