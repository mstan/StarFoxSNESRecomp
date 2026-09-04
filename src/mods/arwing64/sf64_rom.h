/* Star Fox 64 (USA, Rev A / v1.1) owner-ROM access for the Arwing64 mod.
 *
 * Loads the user's own ROM image, normalises .z64/.v64/.n64 byte order,
 * verifies the exact identity by SHA-1, and exposes the game's file table
 * (the "DMA table" at 0xDE480 in v1.1) plus MIO0 decompression so callers can
 * read one asset segment (e.g. ast_arwing = file 9) as a flat byte array.
 *
 * Nothing here is game-specific beyond the table location and the identity;
 * no asset bytes ever leave the process except through the Arwing64 cache
 * writer, which the runtime only reads back after re-verifying hashes.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SF64_US11_SIZE 12582912u
#define SF64_US11_SHA1 "09f0d105f476b00efa5303a3ebc42e60a7753b7a"
#define SF64_US11_DMA_TABLE 0xDE480u

/* File indices in the v1.1 DMA table (tools/comptool.py file_names_us). */
enum Sf64File {
  kSf64File_Makerom = 0,
  kSf64File_Main = 1,
  kSf64File_DmaTable = 2,
  kSf64File_AudioSeq = 3,
  kSf64File_AudioBank = 4,
  kSf64File_AudioTable = 5,
  kSf64File_AstCommon = 6,
  kSf64File_AstBgSpace = 7,
  kSf64File_AstBgPlanet = 8,
  kSf64File_AstArwing = 9,
  kSf64File_AstLandmaster = 10,
  kSf64File_AstBlueMarine = 11,
  kSf64File_AstVersus = 12,
  kSf64File_Count = 64,
};

/* Segment numbers used by the asset display lists (segment address high
 * byte). ast_common is segment 1, the vehicle sets are segment 3. */
enum {
  kSf64Segment_Common = 1,
  kSf64Segment_Vehicle = 3,
};

typedef struct Sf64Rom {
  uint8_t *data;      /* big-endian normalised image */
  size_t size;
  char sha1_hex[41];
  uint32_t dma_table; /* ROM offset of the file table */
  uint32_t file_count;
} Sf64Rom;

typedef struct Sf64FileEntry {
  uint32_t vrom_start;
  uint32_t rom_start;
  uint32_t rom_end;
  uint32_t compressed; /* 1 = MIO0 */
} Sf64FileEntry;

/* Load and verify. Returns 0 and sets *error (static string) on failure:
 * wrong size, unknown byte order, SHA-1 mismatch, or unreadable table. */
int sf64_rom_load(Sf64Rom *rom, const char *path, const char **error);
int sf64_rom_load_memory(Sf64Rom *rom, const void *data, size_t size,
                         const char **error);
void sf64_rom_free(Sf64Rom *rom);

int sf64_rom_file_entry(const Sf64Rom *rom, uint32_t index,
                        Sf64FileEntry *out);

/* Read a file, decompressing MIO0 when flagged. *out is malloc'd. */
int sf64_rom_read_file(const Sf64Rom *rom, uint32_t index, uint8_t **out,
                       size_t *out_size, const char **error);

/* Standalone MIO0 decoder (header "MIO0", u32 out size, u32 comp offset,
 * u32 raw offset). Returns 0 on malformed input. */
int sf64_mio0_decode(const uint8_t *src, size_t src_size, uint8_t **out,
                     size_t *out_size);

/* Byte-order helpers for big-endian asset data. */
static inline uint16_t sf64_be16(const uint8_t *p) {
  return (uint16_t)((p[0] << 8) | p[1]);
}
static inline uint32_t sf64_be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static inline int16_t sf64_bes16(const uint8_t *p) {
  return (int16_t)sf64_be16(p);
}
float sf64_bef32(const uint8_t *p);

#ifdef __cplusplus
}
#endif
