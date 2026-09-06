#include "sf64_assets.h"

#include "sf64_rom.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* F3DEX (v1) opcodes. G_IMMFIRST = 0xBF. */
enum {
  G_NOOP = 0x00,
  G_MTX = 0x01,
  G_MOVEMEM = 0x03,
  G_VTX = 0x04,
  G_DL = 0x06,
  G_TRI1 = 0xBF,
  G_CULLDL = 0xBE,
  G_POPMTX = 0xBD,
  G_MOVEWORD = 0xBC,
  G_TEXTURE = 0xBB,
  G_SETOTHERMODE_H = 0xBA,
  G_SETOTHERMODE_L = 0xB9,
  G_ENDDL = 0xB8,
  G_SETGEOMETRYMODE = 0xB7,
  G_CLEARGEOMETRYMODE = 0xB6,
  G_LINE3D = 0xB5,
  G_RDPHALF_1 = 0xB4,
  G_RDPHALF_2 = 0xB3,
  G_MODIFYVTX = 0xB2,
  G_TRI2 = 0xB1,
  G_BRANCH_Z = 0xB0,
  G_RDPFULLSYNC = 0xE9,
  G_RDPTILESYNC = 0xE8,
  G_RDPPIPESYNC = 0xE7,
  G_RDPLOADSYNC = 0xE6,
  G_SETKEYGB = 0xEA,
  G_SETKEYR = 0xEB,
  G_SETCONVERT = 0xEC,
  G_SETSCISSOR = 0xED,
  G_SETPRIMDEPTH = 0xEE,
  G_RDPSETOTHERMODE = 0xEF,
  G_LOADTLUT = 0xF0,
  G_SETTILESIZE = 0xF2,
  G_LOADBLOCK = 0xF3,
  G_LOADTILE = 0xF4,
  G_SETTILE = 0xF5,
  G_FILLRECT = 0xF6,
  G_SETFILLCOLOR = 0xF7,
  G_SETFOGCOLOR = 0xF8,
  G_SETBLENDCOLOR = 0xF9,
  G_SETPRIMCOLOR = 0xFA,
  G_SETENVCOLOR = 0xFB,
  G_SETCOMBINE = 0xFC,
  G_SETTIMG = 0xFD,
  G_SETZIMG = 0xFE,
  G_SETCIMG = 0xFF,
};

enum { G_IM_FMT_RGBA = 0, G_IM_FMT_YUV = 1, G_IM_FMT_CI = 2, G_IM_FMT_IA = 3,
       G_IM_FMT_I = 4 };
enum { G_IM_SIZ_4b = 0, G_IM_SIZ_8b = 1, G_IM_SIZ_16b = 2, G_IM_SIZ_32b = 3 };
enum { G_TX_MIRROR = 1, G_TX_CLAMP = 2 };
enum { kMaxNesting = 8, kVertexCache = 32, kMaxTextureBytes = 4096 };

typedef struct Tile {
  int fmt, siz, line, tmem, palette;
  int cmt, maskt, shiftt, cms, masks, shifts;
  int uls, ult, lrs, lrt; /* 10.2 fixed */
} Tile;

typedef struct DecodeState {
  Sf64GfxContext *ctx;
  struct {
    int16_t x, y, z;
    int16_t s, t;
    uint8_t cn[4];
  } vtx[kVertexCache];
  uint32_t timg_addr;
  int timg_fmt, timg_siz, timg_width;
  Tile tile[8];
  uint32_t tlut_addr;
  int tlut_count;
  int tex_scale_s, tex_scale_t; /* 0.16 */
  int texture_on;
  int bound_material; /* builder material index or -1 (dirty) */
  int depth;
} DecodeState;

void sf64_gfx_init(Sf64GfxContext *ctx, const Sf64Segment *segments,
                   int segment_count, HostMeshBuilder *builder,
                   const Sf64MaterialTemplate *material) {
  memset(ctx, 0, sizeof(*ctx));
  ctx->segments = segments;
  ctx->segment_count = segment_count;
  ctx->builder = builder;
  if (material) ctx->material = *material;
  else {
    memset(ctx->material.prim, 0xff, 4);
    memset(ctx->material.env, 0xff, 4);
  }
}

const uint8_t *sf64_segment_ptr(const Sf64GfxContext *ctx, uint32_t seg_addr,
                                size_t need) {
  const uint8_t segment = (uint8_t)(seg_addr >> 24);
  const uint32_t offset = seg_addr & 0x00ffffffu;
  for (int i = 0; i < ctx->segment_count; i++) {
    const Sf64Segment *s = &ctx->segments[i];
    if (s->segment != segment) continue;
    if ((size_t)offset + need > s->size) return NULL;
    return s->data + offset;
  }
  return NULL;
}

/* ---- texture decoding --------------------------------------------------- */

static uint32_t rgba16_to_argb(uint16_t p) {
  const uint32_t r = (p >> 11) & 31, g = (p >> 6) & 31, b = (p >> 1) & 31,
                 a = p & 1;
  return ((a ? 0xffu : 0u) << 24) | ((r * 255u / 31u) << 16) |
         ((g * 255u / 31u) << 8) | (b * 255u / 31u);
}

static int decode_texture(DecodeState *st, const Tile *rt, uint32_t *out,
                          int width, int height) {
  Sf64GfxContext *ctx = st->ctx;
  const int fmt = rt->fmt, siz = rt->siz;
  const size_t texels = (size_t)width * height;
  size_t bytes;
  switch (siz) {
  case G_IM_SIZ_4b: bytes = (texels + 1) / 2; break;
  case G_IM_SIZ_8b: bytes = texels; break;
  case G_IM_SIZ_16b: bytes = texels * 2; break;
  case G_IM_SIZ_32b: bytes = texels * 4; break;
  default: ctx->error = "sf64_gfx: bad texel size"; return 0;
  }
  const uint8_t *src = sf64_segment_ptr(ctx, st->timg_addr, bytes);
  if (!src) {
    ctx->error = "sf64_gfx: texture image out of segment";
    return 0;
  }
  const uint8_t *tlut = NULL;
  if (fmt == G_IM_FMT_CI) {
    const int count = siz == G_IM_SIZ_4b ? 16 : 256;
    tlut = sf64_segment_ptr(ctx, st->tlut_addr, (size_t)count * 2);
    if (!tlut) {
      ctx->error = "sf64_gfx: CI texture without TLUT";
      return 0;
    }
  }
  for (size_t i = 0; i < texels; i++) {
    uint32_t argb = 0xff000000u;
    switch (fmt) {
    case G_IM_FMT_RGBA:
      if (siz == G_IM_SIZ_16b) argb = rgba16_to_argb(sf64_be16(src + i * 2));
      else if (siz == G_IM_SIZ_32b)
        argb = ((uint32_t)src[i * 4 + 3] << 24) | ((uint32_t)src[i * 4] << 16) |
               ((uint32_t)src[i * 4 + 1] << 8) | src[i * 4 + 2];
      else goto unsupported;
      break;
    case G_IM_FMT_IA:
      if (siz == G_IM_SIZ_16b) {
        const uint8_t in = src[i * 2], a = src[i * 2 + 1];
        argb = ((uint32_t)a << 24) | ((uint32_t)in << 16) | ((uint32_t)in << 8) | in;
      } else if (siz == G_IM_SIZ_8b) {
        const uint8_t v = src[i];
        const uint32_t in = (v >> 4) * 17u, a = (v & 15) * 17u;
        argb = (a << 24) | (in << 16) | (in << 8) | in;
      } else if (siz == G_IM_SIZ_4b) {
        const uint8_t v = (i & 1) ? (src[i / 2] & 15) : (src[i / 2] >> 4);
        const uint32_t in = (v >> 1) * 255u / 7u, a = (v & 1) ? 255u : 0u;
        argb = (a << 24) | (in << 16) | (in << 8) | in;
      } else goto unsupported;
      break;
    case G_IM_FMT_I:
      if (siz == G_IM_SIZ_8b) {
        const uint32_t in = src[i];
        argb = (in << 24) | (in << 16) | (in << 8) | in;
      } else if (siz == G_IM_SIZ_4b) {
        const uint8_t v = (i & 1) ? (src[i / 2] & 15) : (src[i / 2] >> 4);
        const uint32_t in = v * 17u;
        argb = (in << 24) | (in << 16) | (in << 8) | in;
      } else goto unsupported;
      break;
    case G_IM_FMT_CI: {
      int index;
      if (siz == G_IM_SIZ_8b) index = src[i];
      else if (siz == G_IM_SIZ_4b)
        index = (i & 1) ? (src[i / 2] & 15) : (src[i / 2] >> 4);
      else goto unsupported;
      argb = rgba16_to_argb(sf64_be16(tlut + index * 2));
      break;
    }
    default:
    unsupported:
      ctx->error = "sf64_gfx: unsupported texture format";
      return 0;
    }
    out[i] = argb;
  }
  return 1;
}

static uint8_t wrap_mode(int cm) {
  if (cm & G_TX_CLAMP) return HOST_MESH_WRAP_CLAMP;
  if (cm & G_TX_MIRROR) return HOST_MESH_WRAP_MIRROR;
  return HOST_MESH_WRAP_REPEAT;
}

/* Bind the current tile 0 as a builder texture + material. */
static int ensure_material(DecodeState *st) {
  Sf64GfxContext *ctx = st->ctx;
  if (st->bound_material >= 0) return st->bound_material;
  const Sf64MaterialTemplate *mt = &ctx->material;
  uint32_t flags = mt->flags & ~(uint32_t)HOST_MESH_MAT_TEXTURE;
  int texture = -1;
  if (st->texture_on && st->timg_addr != 0) {
    const Tile *rt = &st->tile[0];
    const int width = ((rt->lrs - rt->uls) >> 2) + 1;
    const int height = ((rt->lrt - rt->ult) >> 2) + 1;
    if (width <= 0 || height <= 0 || width > 256 || height > 256) {
      ctx->error = "sf64_gfx: implausible tile size";
      return -1;
    }
    uint32_t *pixels = (uint32_t *)malloc((size_t)width * height * 4u);
    if (!pixels) {
      ctx->error = "sf64_gfx: out of memory";
      return -1;
    }
    if (!decode_texture(st, rt, pixels, width, height)) {
      free(pixels);
      return -1;
    }
    texture = host_mesh_builder_add_texture(
        ctx->builder, (uint16_t)width, (uint16_t)height, wrap_mode(rt->cms),
        wrap_mode(rt->cmt), (uint8_t)rt->masks, (uint8_t)rt->maskt, pixels);
    free(pixels);
    if (texture < 0) {
      ctx->error = "sf64_gfx: texture registration failed";
      return -1;
    }
    flags |= HOST_MESH_MAT_TEXTURE;
    ctx->stats.texture_binds++;
  }
  const int material = host_mesh_builder_add_material(ctx->builder, flags,
                                                      texture, mt->prim, mt->env);
  if (material < 0) {
    ctx->error = "sf64_gfx: material registration failed";
    return -1;
  }
  st->bound_material = material;
  return material;
}

static float texel_coord(int16_t tc, int scale16, int shift, int origin_10_2) {
  /* S10.5 texture coordinate times the 0.16 gSPTexture scale, then the tile
   * shift, minus the tile origin, in texels. */
  double v = (double)tc / 32.0 * ((double)scale16 / 65536.0);
  if (shift > 0 && shift <= 10) v /= (double)(1 << shift);
  else if (shift > 10) v *= (double)(1 << (16 - shift));
  v -= (double)origin_10_2 / 4.0;
  return (float)v;
}

static int emit_triangle(DecodeState *st, int i0, int i1, int i2) {
  Sf64GfxContext *ctx = st->ctx;
  if (i0 >= kVertexCache || i1 >= kVertexCache || i2 >= kVertexCache) {
    ctx->error = "sf64_gfx: triangle vertex index out of cache";
    return 0;
  }
  const int material = ensure_material(st);
  if (material < 0) return 0;
  if (ctx->swap_winding) {
    const int t = i1;
    i1 = i2;
    i2 = t;
  }
  const int idx[3] = {i0, i1, i2};
  const Tile *rt = &st->tile[0];
  HostMeshVertex v[3];
  for (int k = 0; k < 3; k++) {
    const int i = idx[k];
    memset(&v[k], 0, sizeof(v[k]));
    v[k].x = (float)st->vtx[i].x;
    v[k].y = (float)st->vtx[i].y;
    v[k].z = (float)st->vtx[i].z;
    v[k].u = texel_coord(st->vtx[i].s, st->tex_scale_s, rt->shifts, rt->uls);
    v[k].v = texel_coord(st->vtx[i].t, st->tex_scale_t, rt->shiftt, rt->ult);
    v[k].nx = (int8_t)st->vtx[i].cn[0];
    v[k].ny = (int8_t)st->vtx[i].cn[1];
    v[k].nz = (int8_t)st->vtx[i].cn[2];
    memcpy(v[k].color, st->vtx[i].cn, 4);
    if (ctx->material.ignore_vertex_alpha) v[k].color[3] = 255;
  }
  if (!host_mesh_builder_add_triangle(ctx->builder, (uint32_t)material, v)) {
    ctx->error = "sf64_gfx: triangle append failed";
    return 0;
  }
  ctx->stats.triangles++;
  return 1;
}

static int run_list(DecodeState *st, uint32_t seg_addr) {
  Sf64GfxContext *ctx = st->ctx;
  if (st->depth >= kMaxNesting) {
    ctx->error = "sf64_gfx: display list nesting too deep";
    return 0;
  }
  st->depth++;
  uint32_t pc = seg_addr;
  for (int guard = 0; guard < 65536; guard++) {
    const uint8_t *cmd = sf64_segment_ptr(ctx, pc, 8);
    if (!cmd) {
      ctx->error = "sf64_gfx: display list ran out of segment";
      return 0;
    }
    const uint32_t w0 = sf64_be32(cmd), w1 = sf64_be32(cmd + 4);
    const uint8_t op = (uint8_t)(w0 >> 24);
    pc += 8;
    switch (op) {
    case G_ENDDL:
      st->depth--;
      return 1;
    case G_DL: {
      const int branch = (w0 >> 16) & 0xff; /* 1 = jump, no return */
      ctx->stats.nested_lists++;
      if (!run_list(st, w1)) return 0;
      if (branch) {
        st->depth--;
        return 1;
      }
      break;
    }
    case G_VTX: {
      const int v0 = ((w0 >> 16) & 0xff) / 2;
      const int n = (w0 >> 10) & 0x3f;
      if (n == 0 || v0 + n > kVertexCache) {
        ctx->error = "sf64_gfx: vertex load out of cache";
        return 0;
      }
      const uint8_t *src = sf64_segment_ptr(ctx, w1, (size_t)n * 16u);
      if (!src) {
        ctx->error = "sf64_gfx: vertex data out of segment";
        return 0;
      }
      for (int i = 0; i < n; i++) {
        const uint8_t *p = src + i * 16;
        st->vtx[v0 + i].x = sf64_bes16(p);
        st->vtx[v0 + i].y = sf64_bes16(p + 2);
        st->vtx[v0 + i].z = sf64_bes16(p + 4);
        st->vtx[v0 + i].s = sf64_bes16(p + 8);
        st->vtx[v0 + i].t = sf64_bes16(p + 10);
        memcpy(st->vtx[v0 + i].cn, p + 12, 4);
      }
      ctx->stats.vertex_loads++;
      break;
    }
    case G_TRI1: {
      const int a = ((w1 >> 16) & 0xff) / 2, b = ((w1 >> 8) & 0xff) / 2,
                c = (w1 & 0xff) / 2;
      if (!emit_triangle(st, a, b, c)) return 0;
      break;
    }
    case G_TRI2: {
      const int a = ((w0 >> 16) & 0xff) / 2, b = ((w0 >> 8) & 0xff) / 2,
                c = (w0 & 0xff) / 2;
      const int d = ((w1 >> 16) & 0xff) / 2, e = ((w1 >> 8) & 0xff) / 2,
                f = (w1 & 0xff) / 2;
      if (!emit_triangle(st, a, b, c) || !emit_triangle(st, d, e, f)) return 0;
      break;
    }
    case G_TEXTURE: {
      st->tex_scale_s = (w1 >> 16) & 0xffff;
      st->tex_scale_t = w1 & 0xffff;
      st->texture_on = (w0 & 0xff) != 0;
      st->bound_material = -1;
      break;
    }
    case G_SETTIMG: {
      st->timg_fmt = (w0 >> 21) & 7;
      st->timg_siz = (w0 >> 19) & 3;
      st->timg_width = (w0 & 0xfff) + 1;
      st->timg_addr = w1;
      st->bound_material = -1;
      break;
    }
    case G_SETTILE: {
      const int tile = (w1 >> 24) & 7;
      Tile *t = &st->tile[tile];
      t->fmt = (w0 >> 21) & 7;
      t->siz = (w0 >> 19) & 3;
      t->line = (w0 >> 9) & 0x1ff;
      t->tmem = w0 & 0x1ff;
      t->palette = (w1 >> 20) & 0xf;
      t->cmt = (w1 >> 18) & 3;
      t->maskt = (w1 >> 14) & 0xf;
      t->shiftt = (w1 >> 10) & 0xf;
      t->cms = (w1 >> 8) & 3;
      t->masks = (w1 >> 4) & 0xf;
      t->shifts = w1 & 0xf;
      st->bound_material = -1;
      break;
    }
    case G_SETTILESIZE: {
      const int tile = (w1 >> 24) & 7;
      Tile *t = &st->tile[tile];
      t->uls = (w0 >> 12) & 0xfff;
      t->ult = w0 & 0xfff;
      t->lrs = (w1 >> 12) & 0xfff;
      t->lrt = w1 & 0xfff;
      st->bound_material = -1;
      break;
    }
    case G_LOADTLUT: {
      st->tlut_addr = st->timg_addr;
      st->tlut_count = (((w1 >> 14) & 0x3ff) >> 2) + 1;
      st->bound_material = -1;
      break;
    }
    case G_LOADBLOCK:
    case G_LOADTILE:
    case G_RDPLOADSYNC:
    case G_RDPTILESYNC:
    case G_RDPPIPESYNC:
    case G_RDPFULLSYNC:
    case G_NOOP:
      break;
    case G_SETPRIMCOLOR:
      ctx->material.prim[0] = (uint8_t)(w1 >> 24);
      ctx->material.prim[1] = (uint8_t)(w1 >> 16);
      ctx->material.prim[2] = (uint8_t)(w1 >> 8);
      ctx->material.prim[3] = (uint8_t)w1;
      st->bound_material = -1;
      break;
    case G_SETENVCOLOR:
      ctx->material.env[0] = (uint8_t)(w1 >> 24);
      ctx->material.env[1] = (uint8_t)(w1 >> 16);
      ctx->material.env[2] = (uint8_t)(w1 >> 8);
      ctx->material.env[3] = (uint8_t)w1;
      st->bound_material = -1;
      break;
    case G_SETGEOMETRYMODE:
    case G_CLEARGEOMETRYMODE:
    case G_SETCOMBINE:
    case G_SETOTHERMODE_H:
    case G_SETOTHERMODE_L:
    case G_CULLDL:
    case G_MTX:
    case G_POPMTX:
    case G_MOVEMEM:
    case G_MOVEWORD:
    case G_SETPRIMDEPTH:
    case G_SETSCISSOR:
    case G_SETFOGCOLOR:
    case G_SETBLENDCOLOR:
    case G_SETFILLCOLOR:
    case G_RDPHALF_1:
    case G_RDPHALF_2:
      /* State the importer's material template already models, or matrix
       * ops the vehicle lists never use. Tolerated, but counted so a
       * surprising list is visible in the stats. */
      ctx->stats.unknown_opcodes++;
      ctx->stats.last_unknown_opcode = op;
      break;
    default:
      ctx->stats.unknown_opcodes++;
      ctx->stats.last_unknown_opcode = op;
      ctx->error = "sf64_gfx: unsupported display list opcode";
      return 0;
    }
  }
  ctx->error = "sf64_gfx: display list did not terminate";
  return 0;
}

int sf64_gfx_import_display_list(Sf64GfxContext *ctx, uint32_t seg_addr,
                                 const char *name) {
  if (!ctx || !ctx->builder || !name) return -1;
  ctx->error = NULL;
  const int list = host_mesh_builder_begin_display_list(ctx->builder, name);
  if (list < 0) {
    ctx->error = "sf64_gfx: duplicate display list name";
    return -1;
  }
  DecodeState st;
  memset(&st, 0, sizeof(st));
  st.ctx = ctx;
  st.tex_scale_s = st.tex_scale_t = 0xffff;
  st.texture_on = 1;
  st.bound_material = -1;
  if (!run_list(&st, seg_addr)) return -1;
  return list;
}

/* ---- skeleton / animation ---------------------------------------------- */

static int find_limb(const Sf64Skeleton *sk, uint32_t addr) {
  if (addr == 0) return -1;
  for (int i = 0; i < sk->count; i++)
    if (sk->limb_addr[i] == addr) return i;
  return -1;
}

static int assign_parents(Sf64Skeleton *sk, int limb, int parent, int depth,
                          const char **error) {
  if (depth > SF64_MAX_LIMBS) {
    if (error) *error = "sf64_skel: limb graph too deep (cycle?)";
    return 0;
  }
  if (sk->limbs[limb].parent != -2) {
    if (error) *error = "sf64_skel: limb reached twice";
    return 0;
  }
  sk->limbs[limb].parent = parent;
  const int child = find_limb(sk, sk->limbs[limb].child);
  if (sk->limbs[limb].child && child < 0) {
    if (error) *error = "sf64_skel: child not in skeleton array";
    return 0;
  }
  if (child >= 0 && !assign_parents(sk, child, limb, depth + 1, error)) return 0;
  const int sibling = find_limb(sk, sk->limbs[limb].sibling);
  if (sk->limbs[limb].sibling && sibling < 0) {
    if (error) *error = "sf64_skel: sibling not in skeleton array";
    return 0;
  }
  if (sibling >= 0 && !assign_parents(sk, sibling, parent, depth + 1, error))
    return 0;
  return 1;
}

int sf64_read_skeleton(const Sf64GfxContext *ctx, uint32_t seg_addr,
                       Sf64Skeleton *out, const char **error) {
  if (error) *error = NULL;
  if (!ctx || !out) return 0;
  memset(out, 0, sizeof(*out));
  for (int i = 0; i <= SF64_MAX_LIMBS; i++) {
    const uint8_t *p = sf64_segment_ptr(ctx, seg_addr + (uint32_t)i * 4u, 4);
    if (!p) {
      if (error) *error = "sf64_skel: skeleton array out of segment";
      return 0;
    }
    const uint32_t limb_addr = sf64_be32(p);
    if (limb_addr == 0) break;
    if (i == SF64_MAX_LIMBS) {
      if (error) *error = "sf64_skel: too many limbs";
      return 0;
    }
    const uint8_t *l = sf64_segment_ptr(ctx, limb_addr, 0x20);
    if (!l) {
      if (error) *error = "sf64_skel: limb out of segment";
      return 0;
    }
    Sf64Limb *limb = &out->limbs[i];
    limb->dlist = sf64_be32(l);
    limb->trans[0] = sf64_bef32(l + 4);
    limb->trans[1] = sf64_bef32(l + 8);
    limb->trans[2] = sf64_bef32(l + 12);
    limb->rot[0] = sf64_bes16(l + 16);
    limb->rot[1] = sf64_bes16(l + 18);
    limb->rot[2] = sf64_bes16(l + 20);
    limb->sibling = sf64_be32(l + 24);
    limb->child = sf64_be32(l + 28);
    limb->parent = -2;
    if (!isfinite(limb->trans[0]) || !isfinite(limb->trans[1]) ||
        !isfinite(limb->trans[2])) {
      if (error) *error = "sf64_skel: non-finite limb translation";
      return 0;
    }
    out->limb_addr[i] = limb_addr;
    out->count = i + 1;
  }
  if (out->count == 0) {
    if (error) *error = "sf64_skel: empty skeleton";
    return 0;
  }
  if (!assign_parents(out, 0, -1, 0, error)) return 0;
  for (int i = 0; i < out->count; i++) {
    if (out->limbs[i].parent == -2) {
      if (error) *error = "sf64_skel: limb not reachable from root";
      return 0;
    }
  }
  return 1;
}

int sf64_read_animation_frame(const Sf64GfxContext *ctx, uint32_t seg_addr,
                              int frame, int limb_count,
                              HostMeshVec3 *root_trans, HostMeshVec3 *rot_deg,
                              int *frame_count, const char **error) {
  if (error) *error = NULL;
  const uint8_t *a = sf64_segment_ptr(ctx, seg_addr, 12);
  if (!a) {
    if (error) *error = "sf64_anim: animation out of segment";
    return 0;
  }
  const int anim_frames = sf64_bes16(a);
  const int anim_limbs = sf64_bes16(a + 2);
  const uint32_t frame_data = sf64_be32(a + 4);
  const uint32_t joint_key = sf64_be32(a + 8);
  if (anim_frames <= 0 || anim_limbs <= 0 || anim_limbs != limb_count) {
    if (error) *error = "sf64_anim: limb count does not match skeleton";
    return 0;
  }
  if (frame < 0 || frame >= anim_frames) frame = 0;
  if (frame_count) *frame_count = anim_frames;
  /* Determine the frame-data pool size from the keys so we can bounds check:
   * each key references frameData[key.x .. key.x + len). */
  const uint8_t *keys =
      sf64_segment_ptr(ctx, joint_key, (size_t)(anim_limbs + 1) * 12u);
  if (!keys) {
    if (error) *error = "sf64_anim: joint keys out of segment";
    return 0;
  }
  for (int i = 0; i <= anim_limbs; i++) {
    const uint8_t *k = keys + i * 12;
    int v[3];
    for (int axis = 0; axis < 3; axis++) {
      const int len = sf64_be16(k + axis * 4);
      const int start = sf64_be16(k + axis * 4 + 2);
      const int index = frame < len ? start + frame : start;
      const uint8_t *d = sf64_segment_ptr(ctx, frame_data + (uint32_t)index * 2u, 2);
      if (!d) {
        if (error) *error = "sf64_anim: frame data out of segment";
        return 0;
      }
      v[axis] = sf64_bes16(d);
    }
    if (i == 0) {
      if (root_trans) {
        root_trans->x = (float)v[0];
        root_trans->y = (float)v[1];
        root_trans->z = (float)v[2];
      }
    } else if (rot_deg) {
      rot_deg[i - 1].x = (float)v[0] * 360.0f / 65536.0f;
      rot_deg[i - 1].y = (float)v[1] * 360.0f / 65536.0f;
      rot_deg[i - 1].z = (float)v[2] * 360.0f / 65536.0f;
    }
  }
  return 1;
}
