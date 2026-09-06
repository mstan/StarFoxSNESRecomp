#include "arwing64_extract.h"

#include "sf64_assets.h"
#include "sf64_rom.h"

#include <stdlib.h>
#include <string.h>

typedef struct NamedList {
  const char *name;
  uint32_t addr;
} NamedList;

static const NamedList kArwingLists[] = {
    {"aAwBodyDL", kSf64Addr_BodyDL},
    {"aAwRightWingDL", kSf64Addr_RightWingDL},
    {"aAwLeftWingDL", kSf64Addr_LeftWingDL},
    {"aAwRightWingBrokenDL", kSf64Addr_RightWingBrokenDL},
    {"aAwLeftWingBrokenDL", kSf64Addr_LeftWingBrokenDL},
    {"aAwFlap1DL", kSf64Addr_Flap1DL},
    {"aAwFlap2DL", kSf64Addr_Flap2DL},
    {"aAwFlap3DL", kSf64Addr_Flap3DL},
    {"aAwFlap4DL", kSf64Addr_Flap4DL},
    {"aAwLaserGun1DL", kSf64Addr_LaserGun1DL},
    {"aAwLaserGun2DL", kSf64Addr_LaserGun2DL},
};

static int list_index_for_addr(const int *indices, uint32_t addr) {
  for (size_t i = 0; i < sizeof(kArwingLists) / sizeof(kArwingLists[0]); i++)
    if (kArwingLists[i].addr == addr) return indices[i];
  return -1;
}

int arwing64_extract_mesh(const uint8_t *rom_image, size_t rom_size,
                          uint8_t **blob, size_t *blob_size,
                          Arwing64ExtractStats *stats, const char **error) {
  if (error) *error = NULL;
  if (stats) memset(stats, 0, sizeof(*stats));
  if (!rom_image || !blob || !blob_size) {
    if (error) *error = "arwing64: bad arguments";
    return 0;
  }
  Sf64Rom rom;
  if (!sf64_rom_load_memory(&rom, rom_image, rom_size, error)) return 0;

  uint8_t *arwing_seg = NULL, *common_seg = NULL;
  size_t arwing_size = 0, common_size = 0;
  HostMeshBuilder *builder = NULL;
  int ok = 0;
  if (!sf64_rom_read_file(&rom, kSf64File_AstArwing, &arwing_seg,
                          &arwing_size, error) ||
      !sf64_rom_read_file(&rom, kSf64File_AstCommon, &common_seg, &common_size,
                          error))
    goto done;

  Sf64Segment segments[2] = {
      {kSf64Segment_Vehicle, arwing_seg, arwing_size},
      {kSf64Segment_Common, common_seg, common_size},
  };
  builder = host_mesh_builder_create();
  if (!builder) {
    if (error) *error = "arwing64: out of memory";
    goto done;
  }

  /* Arwing body/wings: SETUPDL_29 = G_CC_MODULATEIDECALA, lighting, cull
   * back, bilinear, opaque. */
  Sf64MaterialTemplate ship;
  memset(&ship, 0, sizeof(ship));
  ship.flags = HOST_MESH_MAT_SHADE | HOST_MESH_MAT_LIGHTING |
               HOST_MESH_MAT_CULL_BACK | HOST_MESH_MAT_BILINEAR;
  memset(ship.prim, 0xff, 4);
  memset(ship.env, 0xff, 4);
  /* SETUPDL_29 uses TEXEL0 alpha, not SHADE alpha. Several wing/flap
   * vertices have alpha zero in the ROM; that does not hide them on N64. */
  ship.ignore_vertex_alpha = 1;

  Sf64GfxContext ctx;
  sf64_gfx_init(&ctx, segments, 2, builder, &ship);
  /* N64 front faces are clockwise on screen with y up; host_mesh treats
   * counter-clockwise as front, so swap. Verified by preview render. */
  ctx.swap_winding = 1;

  int list_indices[sizeof(kArwingLists) / sizeof(kArwingLists[0])];
  for (size_t i = 0; i < sizeof(kArwingLists) / sizeof(kArwingLists[0]); i++) {
    list_indices[i] =
        sf64_gfx_import_display_list(&ctx, kArwingLists[i].addr,
                                     kArwingLists[i].name);
    if (list_indices[i] < 0) {
      if (error) *error = ctx.error ? ctx.error : "arwing64: list import failed";
      goto done;
    }
  }

  /* Engine glow orb: SETUPDL_67 combiner (PRIM-ENV)*TEXEL0+ENV, alpha
   * TEXEL0*PRIM, cloud surface (blend, no z write), no culling, no lighting.
   * One list per env colour used by Display_DrawEngineGlow. */
  static const struct {
    const char *name;
    uint8_t env[4];
  } kOrbs[] = {
      {"aOrbDL_red", {255, 0, 0, 255}},
      {"aOrbDL_blue", {0, 0, 255, 255}},
      {"aOrbDL_green", {0, 255, 0, 255}},
      {"aOrbDL_orange", {255, 64, 0, 255}},
  };
  for (size_t i = 0; i < sizeof(kOrbs) / sizeof(kOrbs[0]); i++) {
    Sf64MaterialTemplate glow;
    memset(&glow, 0, sizeof(glow));
    glow.flags = HOST_MESH_MAT_LERP_PRIM_ENV | HOST_MESH_MAT_PRIM_ALPHA |
                 HOST_MESH_MAT_ALPHA_BLEND | HOST_MESH_MAT_NO_ZWRITE |
                 HOST_MESH_MAT_BILINEAR;
    memset(glow.prim, 0xff, 4);
    memcpy(glow.env, kOrbs[i].env, 4);
    ctx.material = glow;
    if (sf64_gfx_import_display_list(&ctx, kSf64Addr_OrbDL, kOrbs[i].name) < 0) {
      if (error) *error = ctx.error ? ctx.error : "arwing64: orb import failed";
      goto done;
    }
  }
  {
    /* Barrel roll shield: prim (255,255,255,a) env (0,0,160,a); alpha is
     * applied at draw time through alpha_scale. */
    Sf64MaterialTemplate shield;
    memset(&shield, 0, sizeof(shield));
    shield.flags = HOST_MESH_MAT_LERP_PRIM_ENV | HOST_MESH_MAT_PRIM_ALPHA |
                   HOST_MESH_MAT_ALPHA_BLEND | HOST_MESH_MAT_NO_ZWRITE |
                   HOST_MESH_MAT_BILINEAR;
    memset(shield.prim, 0xff, 4);
    shield.env[0] = 0;
    shield.env[1] = 0;
    shield.env[2] = 160;
    shield.env[3] = 255;
    ctx.material = shield;
    if (sf64_gfx_import_display_list(&ctx, kSf64Addr_BarrelRollDL,
                                     "aBarrelRollDL") < 0) {
      if (error) *error = ctx.error ? ctx.error : "arwing64: shield import failed";
      goto done;
    }
  }

  /* PlayerShot_DrawLaser's textured, unlit laser geometry. Colour and alpha
   * come from the owner ROM's RGBA16 textures (SETUPDL_21). */
  {
    Sf64MaterialTemplate laser;
    memset(&laser, 0, sizeof(laser));
    laser.flags = HOST_MESH_MAT_ALPHA_BLEND | HOST_MESH_MAT_BILINEAR;
    memset(laser.prim, 0xff, 4);
    memset(laser.env, 0xff, 4);
    ctx.material = laser;
    if (sf64_gfx_import_display_list(&ctx, kSf64Addr_LaserGreenDL, "aLaserShotGreenDL") < 0 ||
        sf64_gfx_import_display_list(&ctx, kSf64Addr_LaserBlueDL, "aLaserShotBlueDL") < 0) {
      if (error) *error = "arwing64: laser import failed";
      goto done;
    }
  }

  /* Skeleton. */
  Sf64Skeleton skel;
  if (!sf64_read_skeleton(&ctx, kSf64Addr_ArwingSkel, &skel, error)) goto done;
  if (skel.count != kArwingLimb_Count) {
    if (error) *error = "arwing64: unexpected Arwing limb count";
    goto done;
  }
  /* Poses (frame 0 of each wing animation). The closed pose doubles as the
   * limb bind rotation since the N64 drawer ignores Limb.rot. */
  HostMeshVec3 half_open[kArwingLimb_Count], closed[kArwingLimb_Count],
      open[kArwingLimb_Count];
  HostMeshVec3 root_half, root_closed, root_open;
  if (!sf64_read_animation_frame(&ctx, kSf64Addr_WingsHalfOpenAnim, 0,
                                 skel.count, &root_half, half_open, NULL,
                                 error) ||
      !sf64_read_animation_frame(&ctx, kSf64Addr_WingsClosedAnim, 0,
                                 skel.count, &root_closed, closed, NULL,
                                 error) ||
      !sf64_read_animation_frame(&ctx, kSf64Addr_WingsOpenAnim, 0, skel.count,
                                 &root_open, open, NULL, error))
    goto done;
  for (int i = 0; i < skel.count; i++) {
    const Sf64Limb *l = &skel.limbs[i];
    const int dl = l->dlist ? list_index_for_addr(list_indices, l->dlist) : -1;
    if (l->dlist && dl < 0) {
      if (error) *error = "arwing64: limb references an unknown display list";
      goto done;
    }
    HostMeshVec3 trans = {l->trans[0], l->trans[1], l->trans[2]};
    if (host_mesh_builder_add_limb(builder, l->parent, dl, trans, closed[i]) !=
        i) {
      if (error) *error = "arwing64: limb append failed";
      goto done;
    }
  }
  if (host_mesh_builder_add_pose(builder, "wings_half_open", root_half,
                                 half_open, (uint32_t)skel.count) < 0 ||
      host_mesh_builder_add_pose(builder, "wings_closed", root_closed, closed,
                                 (uint32_t)skel.count) < 0 ||
      host_mesh_builder_add_pose(builder, "wings_open", root_open, open,
                                 (uint32_t)skel.count) < 0) {
    if (error) *error = "arwing64: pose append failed";
    goto done;
  }
  host_mesh_builder_set_model_scale(builder, 1.0f);
  if (!host_mesh_builder_finish(builder, blob, blob_size, error)) goto done;
  if (stats) {
    stats->triangles = host_mesh_builder_triangle_count(builder);
    stats->textures = host_mesh_builder_texture_count(builder);
    stats->materials = host_mesh_builder_material_count(builder);
    stats->display_lists = host_mesh_builder_display_list_count(builder);
    stats->limbs = host_mesh_builder_limb_count(builder);
    stats->poses = 3;
    stats->unknown_opcodes = ctx.stats.unknown_opcodes;
    stats->blob_size = *blob_size;
  }
  ok = 1;
done:
  host_mesh_builder_destroy(builder);
  free(arwing_seg);
  free(common_seg);
  sf64_rom_free(&rom);
  return ok;
}
