#include "device_auth.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "mbedtls/md.h"
#include "psa/crypto.h"

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
  const char *batch_id,
  const char *timestamp,
  char *signature_hex,
  size_t signature_hex_size
) {
  if (raw_body == NULL || batch_id == NULL || timestamp == NULL || signature_hex == NULL || signature_hex_size < 65) {
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
    "POST\n/api/v1/readings\n%s\n%s\n%s\n%d\n%s",
    DEVICE_ID,
    batch_id,
    timestamp,
    DEVICE_CREDENTIAL_VERSION,
    payload_hash
  );

  if (written <= 0 || (size_t)written >= sizeof(canonical)) {
    return false;
  }

  unsigned char hmac[32];
  size_t hmac_length = 0;
  psa_key_id_t key_id = 0;
  psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
  const psa_algorithm_t algorithm = PSA_ALG_HMAC(PSA_ALG_SHA_256);

  if (psa_crypto_init() != PSA_SUCCESS) {
    return false;
  }

  psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_MESSAGE);
  psa_set_key_algorithm(&attributes, algorithm);
  psa_set_key_type(&attributes, PSA_KEY_TYPE_HMAC);

  psa_status_t status = psa_import_key(
    &attributes,
    (const unsigned char *)DEVICE_SECRET,
    strlen(DEVICE_SECRET),
    &key_id
  );
  psa_reset_key_attributes(&attributes);
  if (status != PSA_SUCCESS) {
    return false;
  }

  status = psa_mac_compute(
    key_id,
    algorithm,
    (const unsigned char *)canonical,
    strlen(canonical),
    hmac,
    sizeof(hmac),
    &hmac_length
  );
  psa_destroy_key(key_id);
  if (status != PSA_SUCCESS || hmac_length != sizeof(hmac)) {
    return false;
  }

  for (size_t i = 0; i < sizeof(hmac); i++) {
    snprintf(&signature_hex[i * 2], signature_hex_size - (i * 2), "%02x", hmac[i]);
  }
  signature_hex[64] = '\0';
  return true;
}
