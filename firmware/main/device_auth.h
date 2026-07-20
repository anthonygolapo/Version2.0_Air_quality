#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool device_auth_build_timestamp(char *output, size_t output_size);
bool device_auth_build_signature(
  const char *raw_body,
  uint32_t sequence_number,
  const char *timestamp,
  char *signature_hex,
  size_t signature_hex_size
);
