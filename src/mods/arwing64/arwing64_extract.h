/* Arwing64 owner-asset extraction: Star Fox 64 (US v1.1) ROM -> N64MESHB
 * blob containing the Arwing skeleton, its display lists (intact and broken
 * wings, flaps, laser guns, body), the wing-pose animations, and the engine
 * glow / barrel-roll shield effect quads.
 *
 * Display list names in the blob are the decomp's symbol names so the
 * presentation code and tests can refer to them stably:
 *   aAwBodyDL aAwRightWingDL aAwLeftWingDL aAwRightWingBrokenDL
 *   aAwLeftWingBrokenDL aAwFlap1DL..aAwFlap4DL aAwLaserGun1DL aAwLaserGun2DL
 *   aOrbDL_red aOrbDL_blue aOrbDL_green aOrbDL_orange aBarrelRollDL
 * Poses: wings_half_open wings_closed wings_open (frame 0 of the SF64 anims).
 * Limb indices follow aAwArwingSkel[] (0 = root, 17 = body).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* SF64 US rev1 segment addresses (assets/yaml/us/rev1/ast_arwing.yaml and
 * ast_common.yaml in the sonicdcer/sf64 decomp). */
enum {
  kSf64Addr_ArwingSkel = 0x3016610u,
  kSf64Addr_WingsHalfOpenAnim = 0x3015AF4u,
  kSf64Addr_WingsClosedAnim = 0x3015C28u,
  kSf64Addr_WingsOpenAnim = 0x30163C4u,
  kSf64Addr_BodyDL = 0x3009B60u,
  kSf64Addr_RightWingDL = 0x3015D80u,
  kSf64Addr_LeftWingDL = 0x3016660u,
  kSf64Addr_RightWingBrokenDL = 0x3014BF0u,
  kSf64Addr_LeftWingBrokenDL = 0x3015120u,
  kSf64Addr_Flap1DL = 0x30155E0u,
  kSf64Addr_Flap2DL = 0x30154A0u,
  kSf64Addr_Flap3DL = 0x3015730u,
  kSf64Addr_Flap4DL = 0x3015880u,
  kSf64Addr_LaserGun1DL = 0x3011720u,
  kSf64Addr_LaserGun2DL = 0x3011450u,
  kSf64Addr_OrbDL = 0x1024AC0u,
  kSf64Addr_BarrelRollDL = 0x101DC10u,
};

/* Limb indices in aAwArwingSkel[] with the roles the SF64 limb-draw override
 * gives them (fox_display.c Display_ArwingOverrideLimbDraw). */
enum {
  kArwingLimb_Root = 0,
  kArwingLimb_UpperRightFlap = 1,
  kArwingLimb_LowerRightFlap = 2,
  kArwingLimb_RightLaserGun = 4,
  kArwingLimb_LowerLeftFlap = 5,
  kArwingLimb_UpperLeftFlap = 6,
  kArwingLimb_LeftLaserGun = 8,
  kArwingLimb_WingA = 12, /* aAwRightWingDL in the decomp's naming */
  kArwingLimb_WingB = 13, /* aAwLeftWingDL */
  kArwingLimb_Body = 17,
  kArwingLimb_Count = 18,
};

typedef struct Arwing64ExtractStats {
  uint32_t triangles;
  uint32_t textures;
  uint32_t materials;
  uint32_t display_lists;
  uint32_t limbs;
  uint32_t poses;
  uint32_t unknown_opcodes;
  size_t blob_size;
} Arwing64ExtractStats;

/* Extract from an already-loaded ROM image (see sf64_rom.h). On success
 * *blob (malloc'd) holds the N64MESHB bytes. error is a static string. */
int arwing64_extract_mesh(const uint8_t *rom_image, size_t rom_size,
                          uint8_t **blob, size_t *blob_size,
                          Arwing64ExtractStats *stats, const char **error);

#ifdef __cplusplus
}
#endif
