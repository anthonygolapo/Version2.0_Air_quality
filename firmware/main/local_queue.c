#include "local_queue.h"

#include <dirent.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "esp_log.h"
#include "reading_json.h"
#include "sensor_reader.h"

static const char *TAG = "local_queue";
static const char *QUEUE_ROOT = "/spiffs";
static const char *SEQ_FILE = "/spiffs/sequence.txt";

static bool is_queue_file(const char *name) {
  if (name == NULL) {
    return false;
  }

  size_t length = strlen(name);
  return length == 17 &&
         strncmp(name, "q_", 2) == 0 &&
         strcmp(name + length - 5, ".json") == 0;
}

bool local_queue_init(void) {
  DIR *dir = opendir(QUEUE_ROOT);
  if (dir == NULL) {
    return false;
  }
  closedir(dir);
  return true;
}

bool local_queue_next_sequence_number(uint32_t *sequence_number) {
  if (sequence_number == NULL) {
    return false;
  }

  uint32_t current = 0;
  FILE *input = fopen(SEQ_FILE, "r");
  if (input != NULL) {
    if (fscanf(input, "%" SCNu32, &current) != 1) {
      current = 0;
    }
    fclose(input);
  }

  current += 1;

  FILE *output = fopen(SEQ_FILE, "w");
  if (output == NULL) {
    return false;
  }
  fprintf(output, "%" PRIu32, current);
  fclose(output);

  *sequence_number = current;
  return true;
}

bool local_queue_store(const sensor_reading_t *reading) {
  if (reading == NULL) {
    return false;
  }

  char *serialized = reading_json_serialize(reading);
  if (serialized == NULL) {
    return false;
  }

  char path[96];
  snprintf(path, sizeof(path), "%s/q_%010" PRIu32 ".json", QUEUE_ROOT, reading->sequence_number);

  FILE *file = fopen(path, "w");
  if (file == NULL) {
    reading_json_free(serialized);
    return false;
  }

  fprintf(file, "%s", serialized);
  fclose(file);
  reading_json_free(serialized);
  ESP_LOGI(TAG, "Queued reading %" PRIu32, reading->sequence_number);
  return true;
}

bool local_queue_peek_oldest(queued_reading_t *queued) {
  return local_queue_peek_oldest_batch(queued, 1) == 1;
}

size_t local_queue_peek_oldest_batch(queued_reading_t *queued, size_t capacity) {
  if (queued == NULL || capacity == 0) {
    return 0;
  }

  const size_t selection_capacity = capacity > 32 ? 32 : capacity;
  char selected_names[32][64] = {{0}};
  size_t selected_count = 0;
  DIR *dir = opendir(QUEUE_ROOT);
  if (dir == NULL) {
    return 0;
  }

  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (!is_queue_file(entry->d_name) || strlen(entry->d_name) >= sizeof(selected_names[0])) {
      continue;
    }

    size_t position = 0;
    while (position < selected_count && strcmp(selected_names[position], entry->d_name) < 0) {
      position++;
    }
    if (position >= selection_capacity) {
      continue;
    }

    if (selected_count < selection_capacity) selected_count++;
    for (size_t index = selected_count - 1; index > position; index--) {
      memcpy(selected_names[index], selected_names[index - 1], sizeof(selected_names[index]));
    }
    size_t name_length = strlen(entry->d_name);
    memcpy(selected_names[position], entry->d_name, name_length + 1);
  }
  closedir(dir);

  size_t loaded_count = 0;
  for (size_t index = 0; index < selected_count; index++) {
    char path[96];
    snprintf(path, sizeof(path), "%s/%s", QUEUE_ROOT, selected_names[index]);
    FILE *file = fopen(path, "r");
    if (file == NULL) {
      ESP_LOGW(TAG, "Unable to open queued file %s", path);
      continue;
    }

    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (length <= 0) {
      fclose(file);
      ESP_LOGW(TAG, "Moving empty queue file to dead letter: %s", path);
      local_queue_mark_dead_letter(path);
      continue;
    }

    char *buffer = calloc((size_t)length + 1, sizeof(char));
    if (buffer == NULL) {
      fclose(file);
      break;
    }
    size_t bytes_read = fread(buffer, 1, (size_t)length, file);
    fclose(file);

    if (bytes_read != (size_t)length || !reading_json_deserialize(buffer, &queued[loaded_count].reading)) {
      ESP_LOGW(TAG, "Moving unreadable queue file to dead letter: %s", path);
      free(buffer);
      local_queue_mark_dead_letter(path);
      continue;
    }
    free(buffer);
    snprintf(queued[loaded_count].path, sizeof(queued[loaded_count].path), "%s", path);
    loaded_count++;
  }
  return loaded_count;
}

bool local_queue_delete(const char *path) {
  if (path == NULL) {
    return false;
  }
  return unlink(path) == 0;
}

bool local_queue_mark_dead_letter(const char *path) {
  if (path == NULL) {
    return false;
  }

  char destination[96];
  int written = snprintf(destination, sizeof(destination), "%s", path);
  if (written <= 0 || (size_t)written >= sizeof(destination)) {
    return false;
  }

  char *filename = strrchr(destination, '/');
  if (filename == NULL || filename[1] != 'q') {
    return false;
  }
  filename[1] = 'd';
  return rename(path, destination) == 0;
}

size_t local_queue_count(void) {
  size_t count = 0;
  DIR *dir = opendir(QUEUE_ROOT);
  if (dir == NULL) {
    return 0;
  }

  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (is_queue_file(entry->d_name)) {
      count++;
    }
  }
  closedir(dir);
  return count;
}
