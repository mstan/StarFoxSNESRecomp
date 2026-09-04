/* Star Fox 64 asset decoders: F3DEX display lists -> HostMeshBuilder
 * triangles, plus the SF64 skeleton (Limb tree) and Animation frame-0 poses.
 *
 * The decoder implements the subset of the F3DEX GBI that the SF64 vehicle
 * and effect display lists use (vertex loads, triangles, texture block
 * loads, tile state, nested lists) and reports anything else loudly so an
 * unexpected opcode never yields silent garbage.
 */
#pragma once

#include "host_mesh_builder.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Sf64Segment {
  uint8_t segment;      /* segment number (high byte of segment addresses) */
  const uint8_t *data;
  size_t size;
} Sf64Segment;

/* Material template applied to every triangle emitted by a display list.
 * SF64 sets combiner / geometry mode in the caller's RCP setup list, not in
 * the model list, so the importer supplies it here. */
typedef struct Sf64MaterialTemplate {
  uint32_t flags;   /* HOST_MESH_MAT_* excluding TEXTURE (added when bound) */
  uint8_t prim[4];
  uint8_t env[4];
} Sf64MaterialTemplate;

typedef struct Sf64GfxStats {
  uint32_t triangles;
  uint32_t vertex_loads;
  uint32_t texture_binds;
  uint32_t nested_lists;
  uint32_t unknown_opcodes;
  uint8_t last_unknown_opcode;
} Sf64GfxStats;

typedef struct Sf64GfxContext {
  const Sf64Segment *segments;
  int segment_count;
  HostMeshBuilder *builder;
  Sf64MaterialTemplate material;
  /* When nonzero, swap the second and third vertex of every triangle so
   * N64 front faces become counter-clockwise in host_mesh's convention. */
  int swap_winding;
  Sf64GfxStats stats;
  const char *error; /* static string on failure */
} Sf64GfxContext;

void sf64_gfx_init(Sf64GfxContext *ctx, const Sf64Segment *segments,
                   int segment_count, HostMeshBuilder *builder,
                   const Sf64MaterialTemplate *material);

/* Resolve a segment address to bytes; returns NULL if out of range. */
const uint8_t *sf64_segment_ptr(const Sf64GfxContext *ctx, uint32_t seg_addr,
                                size_t need);

/* Interpret a display list into a new named builder display list.
 * Returns the display list index or -1 (ctx->error set). */
int sf64_gfx_import_display_list(Sf64GfxContext *ctx, uint32_t seg_addr,
                                 const char *name);

/* ---- skeleton / animation ---------------------------------------------- */

#define SF64_MAX_LIMBS 64

typedef struct Sf64Limb {
  uint32_t dlist;   /* segment address or 0 */
  float trans[3];
  int16_t rot[3];   /* binang, unused by the drawer (poses supply rotation) */
  uint32_t sibling; /* segment address */
  uint32_t child;
  int parent;       /* index into the skeleton array, -1 for root */
} Sf64Limb;

typedef struct Sf64Skeleton {
  Sf64Limb limbs[SF64_MAX_LIMBS];
  uint32_t limb_addr[SF64_MAX_LIMBS];
  int count;
} Sf64Skeleton;

/* Read a `Limb**` array (NULL terminated) and resolve the tree. */
int sf64_read_skeleton(const Sf64GfxContext *ctx, uint32_t seg_addr,
                       Sf64Skeleton *out, const char **error);

/* Evaluate an Animation at `frame` (Animation_GetFrameData semantics):
 * root translation in model units and per-limb rotations in degrees.
 * rot_deg must hold skeleton count entries. */
int sf64_read_animation_frame(const Sf64GfxContext *ctx, uint32_t seg_addr,
                              int frame, int limb_count,
                              HostMeshVec3 *root_trans, HostMeshVec3 *rot_deg,
                              int *frame_count, const char **error);

#ifdef __cplusplus
}
#endif
