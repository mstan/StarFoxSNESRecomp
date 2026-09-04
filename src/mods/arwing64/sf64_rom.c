#include "sf64_rom.h"

#include "sha1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

float sf64_bef32(const uint8_t *p) {
  const uint32_t u = sf64_be32(p);
  float f;
  memcpy(&f, &u, 4);
  return f;
}

static int normalise_byte_order(uint8_t *data, size_t size,
                                const char **error) {
  if (size < 0x40 || (size & 3u) != 0) {
    if (error) *error = "sf64: image too small or not word aligned";
    return 0;
  }
  const uint8_t m0 = data[0], m1 = data[1], m2 = data[2], m3 = data[3];
  if (m0 == 0x80 && m1 == 0x37 && m2 == 0x12 && m3 == 0x40) return 1; /* z64 */
  if (m0 == 0x37 && m1 == 0x80 && m2 == 0x40 && m3 == 0x12) {
    /* v64: swap byte pairs */
    for (size_t i = 0; i < size; i += 2) {
      const uint8_t t = data[i];
      data[i] = data[i + 1];
      data[i + 1] = t;
    }
    return 1;
  }
  if (m0 == 0x40 && m1 == 0x12 && m2 == 0x37 && m3 == 0x80) {
    /* n64: little-endian words */
    for (size_t i = 0; i < size; i += 4) {
      const uint8_t a = data[i], b = data[i + 1];
      data[i] = data[i + 3];
      data[i + 1] = data[i + 2];
      data[i + 2] = b;
      data[i + 3] = a;
    }
    return 1;
  }
  if (error) *error = "sf64: not an N64 ROM image (unknown byte order)";
  return 0;
}

int sf64_rom_load_memory(Sf64Rom *rom, const void *data, size_t size,
                         const char **error) {
  if (error) *error = NULL;
  if (!rom || !data) {
    if (error) *error = "sf64: bad arguments";
    return 0;
  }
  memset(rom, 0, sizeof(*rom));
  if (size != SF64_US11_SIZE) {
    if (error) *error = "sf64: wrong size (expected a 12 MiB Star Fox 64 image)";
    return 0;
  }
  rom->data = (uint8_t *)malloc(size);
  if (!rom->data) {
    if (error) *error = "sf64: out of memory";
    return 0;
  }
  memcpy(rom->data, data, size);
  rom->size = size;
  if (!normalise_byte_order(rom->data, rom->size, error)) {
    sf64_rom_free(rom);
    return 0;
  }
  uint8_t digest[20];
  sha1_buffer(rom->data, rom->size, digest);
  sha1_hex(digest, rom->sha1_hex);
  if (strcmp(rom->sha1_hex, SF64_US11_SHA1) != 0) {
    if (error)
      *error = "sf64: SHA-1 does not match Star Fox 64 (USA) Rev A / v1.1";
    sf64_rom_free(rom);
    return 0;
  }
  rom->dma_table = SF64_US11_DMA_TABLE;
  /* Count entries until the zero terminator; sanity-check monotonic layout. */
  uint32_t count = 0;
  uint32_t prev_end = 0;
  for (;;) {
    const uint32_t at = rom->dma_table + count * 16u;
    if (at + 16u > rom->size || count >= kSf64File_Count) break;
    const uint32_t rs = sf64_be32(rom->data + at + 4);
    const uint32_t re = sf64_be32(rom->data + at + 8);
    if (rs == 0 && re == 0 && count > 0) break;
    if (re <= rs || re > rom->size || rs < prev_end) {
      if (error) *error = "sf64: corrupt file table";
      sf64_rom_free(rom);
      return 0;
    }
    prev_end = re;
    count++;
  }
  if (count < 13) {
    if (error) *error = "sf64: file table too short";
    sf64_rom_free(rom);
    return 0;
  }
  rom->file_count = count;
  return 1;
}

int sf64_rom_load(Sf64Rom *rom, const char *path, const char **error) {
  if (error) *error = NULL;
  if (!rom || !path) {
    if (error) *error = "sf64: bad arguments";
    return 0;
  }
  FILE *f = fopen(path, "rb");
  if (!f) {
    if (error) *error = "sf64: cannot open ROM file";
    return 0;
  }
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    if (error) *error = "sf64: cannot seek ROM file";
    return 0;
  }
  const long len = ftell(f);
  if (len <= 0 || (size_t)len != SF64_US11_SIZE) {
    fclose(f);
    if (error) *error = "sf64: wrong size (expected a 12 MiB Star Fox 64 image)";
    return 0;
  }
  rewind(f);
  uint8_t *buf = (uint8_t *)malloc((size_t)len);
  if (!buf) {
    fclose(f);
    if (error) *error = "sf64: out of memory";
    return 0;
  }
  const size_t got = fread(buf, 1, (size_t)len, f);
  fclose(f);
  if (got != (size_t)len) {
    free(buf);
    if (error) *error = "sf64: short read";
    return 0;
  }
  const int ok = sf64_rom_load_memory(rom, buf, got, error);
  free(buf);
  return ok;
}

void sf64_rom_free(Sf64Rom *rom) {
  if (!rom) return;
  free(rom->data);
  memset(rom, 0, sizeof(*rom));
}

int sf64_rom_file_entry(const Sf64Rom *rom, uint32_t index,
                        Sf64FileEntry *out) {
  if (!rom || !rom->data || !out || index >= rom->file_count) return 0;
  const uint8_t *e = rom->data + rom->dma_table + index * 16u;
  out->vrom_start = sf64_be32(e);
  out->rom_start = sf64_be32(e + 4);
  out->rom_end = sf64_be32(e + 8);
  out->compressed = sf64_be32(e + 12);
  return out->rom_end > out->rom_start && out->rom_end <= rom->size;
}

int sf64_mio0_decode(const uint8_t *src, size_t src_size, uint8_t **out,
                     size_t *out_size) {
  if (!src || src_size < 16 || !out || !out_size) return 0;
  if (memcmp(src, "MIO0", 4) != 0) return 0;
  const uint32_t dest_size = sf64_be32(src + 4);
  const uint32_t comp_off = sf64_be32(src + 8);
  const uint32_t raw_off = sf64_be32(src + 12);
  if (dest_size == 0 || dest_size > (64u << 20) || comp_off > src_size ||
      raw_off > src_size || comp_off < 16)
    return 0;
  uint8_t *dst = (uint8_t *)malloc(dest_size);
  if (!dst) return 0;
  size_t bit_idx = 0, comp_idx = comp_off, raw_idx = raw_off, out_idx = 0;
  const size_t layout_bits = (comp_off - 16) * 8u;
  while (out_idx < dest_size) {
    if (bit_idx >= layout_bits) goto bad;
    const int bit = (src[16 + bit_idx / 8] >> (7 - (bit_idx % 8))) & 1;
    bit_idx++;
    if (bit) {
      if (raw_idx >= src_size) goto bad;
      dst[out_idx++] = src[raw_idx++];
    } else {
      if (comp_idx + 1 >= src_size) goto bad;
      const uint8_t b0 = src[comp_idx], b1 = src[comp_idx + 1];
      comp_idx += 2;
      const uint32_t length = ((b0 & 0xF0u) >> 4) + 3u;
      const uint32_t offset = (((uint32_t)(b0 & 0x0Fu) << 8) | b1) + 1u;
      if (offset > out_idx) goto bad;
      for (uint32_t i = 0; i < length; i++) {
        if (out_idx >= dest_size) break;
        dst[out_idx] = dst[out_idx - offset];
        out_idx++;
      }
    }
  }
  *out = dst;
  *out_size = dest_size;
  return 1;
bad:
  free(dst);
  return 0;
}

int sf64_rom_read_file(const Sf64Rom *rom, uint32_t index, uint8_t **out,
                       size_t *out_size, const char **error) {
  if (error) *error = NULL;
  Sf64FileEntry e;
  if (!sf64_rom_file_entry(rom, index, &e) || !out || !out_size) {
    if (error) *error = "sf64: bad file index";
    return 0;
  }
  const uint8_t *src = rom->data + e.rom_start;
  const size_t len = e.rom_end - e.rom_start;
  if (e.compressed) {
    if (!sf64_mio0_decode(src, len, out, out_size)) {
      if (error) *error = "sf64: MIO0 decode failed";
      return 0;
    }
    return 1;
  }
  uint8_t *copy = (uint8_t *)malloc(len);
  if (!copy) {
    if (error) *error = "sf64: out of memory";
    return 0;
  }
  memcpy(copy, src, len);
  *out = copy;
  *out_size = len;
  return 1;
}
