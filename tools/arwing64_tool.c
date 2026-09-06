/* Arwing64 developer tool: extract the Star Fox 64 Arwing from an owner ROM,
 * write the N64MESHB blob, and render preview images through the same
 * host_mesh rasteriser the game uses.
 *
 *   arwing64_tool <sf64.z64> <out_dir> [--pose NAME] [--ss N] [--json]
 *
 * Outputs: <out_dir>/arwing64.bin, preview_front.png, preview_side.png,
 * preview_top.png, preview_broken.png, and (with --json) stats.json for the
 * differential test against the decomp's Torch output.
 */
#include "arwing64_extract.h"
#include "host_mesh.h"
#include "sf64_rom.h"
#include "sf64_audio_render.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Pinhole {
  float focal, cx, cy;
} Pinhole;

static void pinhole(void *ctx, float x, float y, float z, float *sx, float *sy) {
  const Pinhole *p = (const Pinhole *)ctx;
  *sx = p->cx + x * p->focal / z;
  *sy = p->cy - y * p->focal / z;
}

typedef struct Override {
  int broken_a, broken_b;
  int list_broken_a, list_broken_b;
} Override;

static int limb_override(void *ctx, int limb, int *dl, HostMeshVec3 *t,
                         HostMeshVec3 *r) {
  (void)t;
  (void)r;
  const Override *o = (const Override *)ctx;
  if (limb == kArwingLimb_WingA && o->broken_a) *dl = o->list_broken_a;
  if (limb == kArwingLimb_WingB && o->broken_b) *dl = o->list_broken_b;
  return 1;
}

static void rot_y(float m[9], float deg) {
  const float r = deg * 3.14159265f / 180.0f, c = cosf(r), s = sinf(r);
  const float k[9] = {c, 0, s, 0, 1, 0, -s, 0, c};
  memcpy(m, k, sizeof(k));
}
static void rot_x(float m[9], float deg) {
  const float r = deg * 3.14159265f / 180.0f, c = cosf(r), s = sinf(r);
  const float k[9] = {1, 0, 0, 0, c, -s, 0, s, c};
  memcpy(m, k, sizeof(k));
}
static void mul3(float out[9], const float a[9], const float b[9]) {
  float r[9];
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++)
      r[i * 3 + j] = a[i * 3] * b[j] + a[i * 3 + 1] * b[3 + j] + a[i * 3 + 2] * b[6 + j];
  memcpy(out, r, sizeof(r));
}

static int render_preview(const HostMesh *mesh, const char *path, float yaw_deg,
                          float pitch_deg, int pose, int supersample,
                          const Override *ov) {
  enum { W = 512, H = 512 };
  uint8_t *target = (uint8_t *)calloc((size_t)W * H * 4, 1);
  if (!target) return 0;
  /* Dark blue-grey background so the transparent-black rule is not used. */
  for (int i = 0; i < W * H; i++) {
    target[i * 4 + 0] = 40;
    target[i * 4 + 1] = 30;
    target[i * 4 + 2] = 24;
    target[i * 4 + 3] = 255;
  }
  HostMeshDrawParams p;
  host_mesh_draw_params_init(&p);
  p.mesh = mesh;
  p.pose = pose;
  Pinhole cam = {600.0f, W / 2.0f, H / 2.0f};
  p.projection.project = pinhole;
  p.projection.ctx = &cam;
  p.projection.near_z = 1.0f;
  /* Fit: bounds radius -> distance so the model spans ~70% of the frame. */
  const float ex = mesh->bounds_max.x - mesh->bounds_min.x;
  const float ey = mesh->bounds_max.y - mesh->bounds_min.y;
  const float ez = mesh->bounds_max.z - mesh->bounds_min.z;
  const float radius = 0.5f * sqrtf(ex * ex + ey * ey + ez * ez);
  const float centre[3] = {(mesh->bounds_max.x + mesh->bounds_min.x) * 0.5f,
                           (mesh->bounds_max.y + mesh->bounds_min.y) * 0.5f,
                           (mesh->bounds_max.z + mesh->bounds_min.z) * 0.5f};
  const float dist = radius * cam.focal / (0.35f * W) + radius;
  float ry[9], rx[9], rot[9];
  rot_y(ry, yaw_deg);
  rot_x(rx, pitch_deg);
  mul3(rot, rx, ry);
  /* cam = R * (model - centre) + (0,0,dist) */
  float rc[3] = {rot[0] * centre[0] + rot[1] * centre[1] + rot[2] * centre[2],
                 rot[3] * centre[0] + rot[4] * centre[1] + rot[5] * centre[2],
                 rot[6] * centre[0] + rot[7] * centre[1] + rot[8] * centre[2]};
  float trans[3] = {-rc[0], -rc[1], -rc[2] + dist};
  host_mesh_matrix_compose(p.model_to_camera, rot, 1.0f, trans);
  p.target = target;
  p.target_pitch = W * 4;
  p.target_width = W;
  p.target_height = H;
  p.supersample = supersample;
  p.light_dir[0] = 0.3f;
  p.light_dir[1] = 0.8f;
  p.light_dir[2] = -0.6f;
  p.ambient = 0.35f;
  p.diffuse = 0.65f;
  if (ov) {
    p.override.fn = limb_override;
    p.override.ctx = (void *)ov;
  }
  HostMeshDrawStats stats;
  p.stats = &stats;
  const uint32_t written = host_mesh_draw(&p);
  /* BGRA -> RGBA for stb. */
  for (int i = 0; i < W * H; i++) {
    const uint8_t b = target[i * 4];
    target[i * 4] = target[i * 4 + 2];
    target[i * 4 + 2] = b;
    target[i * 4 + 3] = 255;
  }
  const int ok = stbi_write_png(path, W, H, 4, target, W * 4);
  printf("preview %s: pixels=%u tris=%u/%u limbs=%u bbox=%d,%d..%d,%d\n", path,
         written, stats.triangles_rasterised, stats.triangles_submitted,
         stats.limbs_drawn, stats.bbox_min_x, stats.bbox_min_y,
         stats.bbox_max_x, stats.bbox_max_y);
  free(target);
  return ok && written > 0;
}

static uint8_t *read_file(const char *path, size_t *size) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  const long len = ftell(f);
  rewind(f);
  if (len <= 0) {
    fclose(f);
    return NULL;
  }
  uint8_t *buf = (uint8_t *)malloc((size_t)len);
  if (buf && fread(buf, 1, (size_t)len, f) != (size_t)len) {
    free(buf);
    buf = NULL;
  }
  fclose(f);
  if (buf) *size = (size_t)len;
  return buf;
}

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: arwing64_tool <sf64.z64> <out_dir> [--pose NAME] "
                    "[--ss N] [--json]\n");
    return 2;
  }
  const char *rom_path = argv[1], *out_dir = argv[2];
  if (argc >= 4 && (!strcmp(argv[3], "--audio") || !strcmp(argv[3], "--sample") || !strcmp(argv[3], "--verify-audio"))) {
    Sf64Rom rom; const char *error = NULL;
    if (!sf64_rom_load(&rom, rom_path, &error)) {
      fprintf(stderr, "%s\n", error); return 1;
    }
    int ok = 0;
    if (!strcmp(argv[3], "--sample") && argc == 5) {
      int16_t *pcm = NULL; uint32_t frames = 0;
      ok = sf64_audio_decode_sample(&rom, (unsigned)atoi(argv[4]), &pcm, &frames, &error);
      if (ok) {
        char path[1024]; snprintf(path, sizeof(path), "%s/sample.pcm", out_dir);
        FILE *f = fopen(path, "wb"); ok = f != NULL;
        if (f) {
          for (uint32_t i = 0; i < frames; i++) {
            uint8_t p[2] = {(uint8_t)pcm[i], (uint8_t)((uint16_t)pcm[i] >> 8)};
            if (fwrite(p, 1, 2, f) != 2) ok = 0;
          }
          if (fclose(f)) ok = 0;
        }
      }
      free(pcm);
    } else if (!strcmp(argv[3], "--verify-audio")) ok = sf64_audio_cache_valid(out_dir);
    else ok = sf64_audio_extract(&rom, out_dir, &error);
    sf64_rom_free(&rom);
    if (!ok) fprintf(stderr, "audio extraction: %s\n", error ? error : "write failed");
    return ok ? 0 : 1;
  }
  const char *pose_name = "wings_half_open";
  const char *break_mode = "both"; /* a | b | both | none */
  int ss = 2, want_json = 0;
  for (int i = 3; i < argc; i++) {
    if (!strcmp(argv[i], "--pose") && i + 1 < argc) pose_name = argv[++i];
    else if (!strcmp(argv[i], "--ss") && i + 1 < argc) ss = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--break") && i + 1 < argc) break_mode = argv[++i];
    else if (!strcmp(argv[i], "--json")) want_json = 1;
  }
  size_t rom_size = 0;
  uint8_t *rom = read_file(rom_path, &rom_size);
  if (!rom) {
    fprintf(stderr, "cannot read %s\n", rom_path);
    return 1;
  }
  uint8_t *blob = NULL;
  size_t blob_size = 0;
  Arwing64ExtractStats st;
  const char *err = NULL;
  if (!arwing64_extract_mesh(rom, rom_size, &blob, &blob_size, &st, &err)) {
    fprintf(stderr, "extract failed: %s\n", err ? err : "?");
    free(rom);
    return 1;
  }
  free(rom);
  printf("extracted: tris=%u textures=%u materials=%u lists=%u limbs=%u "
         "poses=%u unknown_ops=%u blob=%zu bytes\n",
         st.triangles, st.textures, st.materials, st.display_lists, st.limbs,
         st.poses, st.unknown_opcodes, st.blob_size);
  char path[1024];
  snprintf(path, sizeof(path), "%s/arwing64.bin", out_dir);
  FILE *f = fopen(path, "wb");
  if (!f || fwrite(blob, 1, blob_size, f) != blob_size) {
    fprintf(stderr, "cannot write %s\n", path);
    if (f) fclose(f);
    free(blob);
    return 1;
  }
  fclose(f);
  HostMesh *mesh = host_mesh_load(blob, blob_size, &err);
  free(blob);
  if (!mesh) {
    fprintf(stderr, "reload failed: %s\n", err ? err : "?");
    return 1;
  }
  printf("bounds: x %.1f..%.1f y %.1f..%.1f z %.1f..%.1f\n", mesh->bounds_min.x,
         mesh->bounds_max.x, mesh->bounds_min.y, mesh->bounds_max.y,
         mesh->bounds_min.z, mesh->bounds_max.z);
  for (uint32_t i = 0; i < mesh->display_list_count; i++) {
    const HostMeshDisplayList *d = &mesh->display_lists[i];
    HostMeshVec3 mn, mx;
    host_mesh_display_list_bounds(mesh, (int)i, &mn, &mx);
    printf("list %2u %-22s tris=%4u  x %.0f..%.0f y %.0f..%.0f z %.0f..%.0f\n",
           i, d->name, d->triangle_count, mn.x, mx.x, mn.y, mx.y, mn.z, mx.z);
  }
  for (uint32_t i = 0; i < mesh->limb_count; i++) {
    const HostMeshLimb *l = &mesh->limbs[i];
    printf("limb %2u parent=%2d list=%2d trans=(%.2f,%.2f,%.2f) rot=(%.1f,%.1f,%.1f)\n",
           i, l->parent, l->display_list, l->trans.x, l->trans.y, l->trans.z,
           l->rot_deg.x, l->rot_deg.y, l->rot_deg.z);
  }
  if (want_json) {
    snprintf(path, sizeof(path), "%s/stats.json", out_dir);
    f = fopen(path, "wb");
    if (f) {
      fprintf(f, "{\n  \"triangles\": %u,\n  \"textures\": %u,\n  \"materials\": %u,\n"
                 "  \"limbs\": %u,\n  \"poses\": %u,\n  \"lists\": {\n",
              st.triangles, st.textures, st.materials, st.limbs, st.poses);
      for (uint32_t i = 0; i < mesh->display_list_count; i++) {
        fprintf(f, "    \"%s\": %u%s\n", mesh->display_lists[i].name,
                mesh->display_lists[i].triangle_count,
                i + 1 < mesh->display_list_count ? "," : "");
      }
      fprintf(f, "  }\n}\n");
      fclose(f);
    }
  }
  const int pose = host_mesh_find_pose(mesh, pose_name);
  if (pose < 0) fprintf(stderr, "warning: pose %s not found\n", pose_name);
  else printf("pose %s index=%d wing_z=%.3f,%.3f\n", pose_name, pose,
              mesh->poses[pose].rot_deg[12].z, mesh->poses[pose].rot_deg[13].z);
  Override ov;
  ov.broken_a = !strcmp(break_mode, "a") || !strcmp(break_mode, "both");
  ov.broken_b = !strcmp(break_mode, "b") || !strcmp(break_mode, "both");
  ov.list_broken_a = host_mesh_find_display_list(mesh, "aAwRightWingBrokenDL");
  ov.list_broken_b = host_mesh_find_display_list(mesh, "aAwLeftWingBrokenDL");
  int ok = 1;
  /* The preview camera looks along +Z. The posed model's nose is +Z,
   * so yaw 0 sees the tail (rear view) and yaw 180 sees
   * the nose (front view; the viewer's left is the ship's starboard wing). */
  snprintf(path, sizeof(path), "%s/preview_rear.png", out_dir);
  ok &= render_preview(mesh, path, 0.0f, 0.0f, pose, ss, NULL);
  snprintf(path, sizeof(path), "%s/preview_front.png", out_dir);
  ok &= render_preview(mesh, path, 180.0f, 0.0f, pose, ss, NULL);
  snprintf(path, sizeof(path), "%s/preview_front_broken.png", out_dir);
  ok &= render_preview(mesh, path, 180.0f, 10.0f, pose, ss, &ov);
  snprintf(path, sizeof(path), "%s/preview_side.png", out_dir);
  ok &= render_preview(mesh, path, 90.0f, 0.0f, pose, ss, NULL);
  snprintf(path, sizeof(path), "%s/preview_top.png", out_dir);
  ok &= render_preview(mesh, path, 180.0f, -80.0f, pose, ss, NULL);
  snprintf(path, sizeof(path), "%s/preview_three_quarter.png", out_dir);
  ok &= render_preview(mesh, path, 150.0f, -25.0f, pose, ss, NULL);
  snprintf(path, sizeof(path), "%s/preview_broken.png", out_dir);
  ok &= render_preview(mesh, path, 150.0f, -25.0f, pose, ss, &ov);
  host_mesh_free(mesh);
  return ok ? 0 : 1;
}
