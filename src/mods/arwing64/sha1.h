/* Minimal SHA-1 (FIPS 180-4) for owner-ROM identity checks. */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Sha1Context {
  uint32_t h[5];
  uint64_t length;
  uint8_t block[64];
  size_t block_len;
} Sha1Context;

void sha1_init(Sha1Context *ctx);
void sha1_update(Sha1Context *ctx, const void *data, size_t size);
void sha1_final(Sha1Context *ctx, uint8_t digest[20]);
void sha1_hex(const uint8_t digest[20], char out[41]);
void sha1_buffer(const void *data, size_t size, uint8_t digest[20]);

#ifdef __cplusplus
}
#endif
