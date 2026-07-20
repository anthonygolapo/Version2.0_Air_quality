#pragma once

#include <stdbool.h>

#include "sensor_reader.h"

bool http_upload_reading(const sensor_reading_t *reading, int *status_code);
