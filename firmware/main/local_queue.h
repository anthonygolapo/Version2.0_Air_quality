#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "sensor_reader.h"

typedef struct {
  char path[96];
  sensor_reading_t reading;
} queued_reading_t;

bool local_queue_init(void);
bool local_queue_next_sequence_number(uint32_t *sequence_number);
bool local_queue_store(const sensor_reading_t *reading);
bool local_queue_peek_oldest(queued_reading_t *queued);
size_t local_queue_peek_oldest_batch(queued_reading_t *queued, size_t capacity);
bool local_queue_delete(const char *path);
bool local_queue_mark_dead_letter(const char *path);
size_t local_queue_count(void);
