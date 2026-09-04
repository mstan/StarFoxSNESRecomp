#include "sha1.h"

#include <string.h>

static uint32_t rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

static void sha1_block(Sha1Context *ctx, const uint8_t p[64]) {
  uint32_t w[80];
  for (int i = 0; i < 16; i++) {
    w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) |
           ((uint32_t)p[i * 4 + 2] << 8) | (uint32_t)p[i * 4 + 3];
  }
  for (int i = 16; i < 80; i++)
    w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
  uint32_t a = ctx->h[0], b = ctx->h[1], c = ctx->h[2], d = ctx->h[3],
           e = ctx->h[4];
  for (int i = 0; i < 80; i++) {
    uint32_t f, k;
    if (i < 20) {
      f = (b & c) | (~b & d);
      k = 0x5a827999u;
    } else if (i < 40) {
      f = b ^ c ^ d;
      k = 0x6ed9eba1u;
    } else if (i < 60) {
      f = (b & c) | (b & d) | (c & d);
      k = 0x8f1bbcdcu;
    } else {
      f = b ^ c ^ d;
      k = 0xca62c1d6u;
    }
    const uint32_t t = rol(a, 5) + f + e + k + w[i];
    e = d;
    d = c;
    c = rol(b, 30);
    b = a;
    a = t;
  }
  ctx->h[0] += a;
  ctx->h[1] += b;
  ctx->h[2] += c;
  ctx->h[3] += d;
  ctx->h[4] += e;
}

void sha1_init(Sha1Context *ctx) {
  ctx->h[0] = 0x67452301u;
  ctx->h[1] = 0xefcdab89u;
  ctx->h[2] = 0x98badcfeu;
  ctx->h[3] = 0x10325476u;
  ctx->h[4] = 0xc3d2e1f0u;
  ctx->length = 0;
  ctx->block_len = 0;
}

void sha1_update(Sha1Context *ctx, const void *data, size_t size) {
  const uint8_t *p = (const uint8_t *)data;
  ctx->length += size;
  while (size > 0) {
    const size_t take = 64 - ctx->block_len < size ? 64 - ctx->block_len : size;
    memcpy(ctx->block + ctx->block_len, p, take);
    ctx->block_len += take;
    p += take;
    size -= take;
    if (ctx->block_len == 64) {
      sha1_block(ctx, ctx->block);
      ctx->block_len = 0;
    }
  }
}

void sha1_final(Sha1Context *ctx, uint8_t digest[20]) {
  const uint64_t bits = ctx->length * 8u;
  const uint8_t one = 0x80;
  sha1_update(ctx, &one, 1);
  const uint8_t zero = 0;
  while (ctx->block_len != 56) sha1_update(ctx, &zero, 1);
  uint8_t len[8];
  for (int i = 0; i < 8; i++) len[i] = (uint8_t)(bits >> (56 - 8 * i));
  /* sha1_update would count these bytes; restore the length afterwards is
   * unnecessary since we finish here. */
  memcpy(ctx->block + 56, len, 8);
  sha1_block(ctx, ctx->block);
  for (int i = 0; i < 5; i++) {
    digest[i * 4] = (uint8_t)(ctx->h[i] >> 24);
    digest[i * 4 + 1] = (uint8_t)(ctx->h[i] >> 16);
    digest[i * 4 + 2] = (uint8_t)(ctx->h[i] >> 8);
    digest[i * 4 + 3] = (uint8_t)ctx->h[i];
  }
}

void sha1_hex(const uint8_t digest[20], char out[41]) {
  static const char hex[] = "0123456789abcdef";
  for (int i = 0; i < 20; i++) {
    out[i * 2] = hex[digest[i] >> 4];
    out[i * 2 + 1] = hex[digest[i] & 15];
  }
  out[40] = 0;
}

void sha1_buffer(const void *data, size_t size, uint8_t digest[20]) {
  Sha1Context ctx;
  sha1_init(&ctx);
  sha1_update(&ctx, data, size);
  sha1_final(&ctx, digest);
}
