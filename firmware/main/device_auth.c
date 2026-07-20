#include "device_auth.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "mbedtls/md.h"

static bool sha256_hex(const char *input, char *output, size_t output_size) {
  if (input == NULL || output == NULL || output_size < 65) {
    return false;
  }

  unsigned char hash[32];
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);

  const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (info == NULL || mbedtls_md_setup(&ctx, info, 0) != 0) {
    mbedtls_md_free(&ctx);
    return false;
  }

  if (mbedtls_md_starts(&ctx) != 0 ||
      mbedtls_md_update(&ctx, (const unsigned char *)input, strlen(input)) != 0 ||
      mbedtls_md_finish(&ctx, hash) != 0) {
    mbedtls_md_free(&ctx);
    return false;
  }

  mbedtls_md_free(&ctx);

  for (size_t i = 0; i < sizeof(hash); i++) {
    snprintf(&output[i * 2], output_size - (i * 2), "%02x", hash[i]);
  }
  output[64] = '\0';
  return true;
}

bool device_auth_build_timestamp(char *output, size_t output_size) {
  if (output == NULL || output_size < 21) {
    return false;
  }

  time_t now = time(NULL);
  struct tm utc_tm;
  gmtime_r(&now, &utc_tm);
  return strftime(output, output_size, "%Y-%m-%dT%H:%M:%SZ", &utc_tm) > 0;
}

bool device_auth_build_signature(
  const char *raw_body,
  uint32_t sequence_number,
  const char *timestamp,
  char *signature_hex,
  size_t signature_hex_size
) {
  if (raw_body == NULL || timestamp == NULL || signature_hex == NULL || signature_hex_size < 65) {
    return false;
  }

  char payload_hash[65];
  if (!sha256_hex(raw_body, payload_hash, sizeof(payload_hash))) {
    return false;
  }

  char canonical[512];
  int written = snprintf(
    canonical,
    sizeof(canonical),
    "POST\n/api/v1/readings\n%s\n%u\n%s\n%d\n%s",
    DEVICE_ID,
    (unsigned)sequence_number,
    timestamp,
    DEVICE_CREDENTIAL_VERSION,
    payload_hash
  );

  if (written <= 0 || (size_t)written >= sizeof(canonical)) {
    return false;
  }

  unsigned char hmac[32];
  const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (info == NULL) {
    return false;
  }

  if (mbedtls_md_hmac(info,
      (const unsigned char *)DEVICE_SECRET,
      strlen(DEVICE_SECRET),
      (const unsigned char *)canonical,
      strlen(canonical),
      hmac) != 0) {
    return false;
  }

  for (size_t i = 0; i < sizeof(hmac); i++) {
    snprintf(&signature_hex[i * 2], signature_hex_size - (i * 2), "%02x", hmac[i]);
  }
  signature_hex[64] = '\0';
  return true;
}
