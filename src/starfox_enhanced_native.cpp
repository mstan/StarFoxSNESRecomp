#include "starfox_enhanced_native.h"

extern "C" {
#include "common_rtl.h"
#include "snes/ppu.h"
}

#include "starfox/assets/rom.hpp"
#include "starfox/assets/shape_decoder.hpp"
#include "starfox/render/background_renderer.hpp"
#include "starfox/render/framebuffer.hpp"
#include "starfox/render/scaled_text_renderer.hpp"
#include "starfox/render/software_renderer.hpp"
#include "starfox/render/sprite_renderer.hpp"
#include "starfox/simulation/game_simulation.hpp"
#include "starfox/simulation/math.hpp"
#include "starfox/simulation/snes_ppu.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

enum {
  kRamBg2XScroll = 0x1723,
  kRamHdmbg2Hofs2 = 0x1934,
  kRamBg2Scroll = 0x19ca,
  kRamDoHofs = 0x19d0,
  kRomDepthTables = 0x038f9a,
};

using ShapeCacheKey = std::uint32_t;

struct NativeShapeAssets {
  const std::uint8_t *rom_pointer{};
  std::size_t rom_size{};
  std::unique_ptr<starfox::assets::RomImage> rom;
  std::unique_ptr<starfox::assets::ShapeDecoder> decoder;
  std::unique_ptr<starfox::render::ScaledTextRenderer> text_renderer;
  std::unique_ptr<starfox::simulation::TrigTables> trigonometry;
  std::unordered_map<ShapeCacheKey, starfox::assets::Shape> shapes;
};

static starfox::assets::SymbolMap native_symbol_map() {
  return starfox::assets::SymbolMap::parse("SHADESTAB2_0 $038b2a\n"
                                           "SHADESTAB2_1 $038b42\n"
                                           "SHADESTAB2_2 $038b5a\n"
                                           "SHADESTAB2_3 $038b72\n"
                                           "DEPTHTABLES $038f9a\n"
                                           "SINTAB $0098a5\n"
                                           "COSTAB $0098e5\n"
                                           "SINTAB16 $0099e5\n"
                                           "TEXTUREADDRTAB $038918\n"
                                           "TEXTUREXYTAB $038a3e\n"
                                           "FONT0WID $01d91a\n"
                                           "FONT0FON $01d9a6\n"
                                           "FONT0TRN $01e6c6\n"
                                           "MSCALECHARS $14beda\n"
                                           "MARIOMSGS $14c3fa\n"
                                           "FACEDATA $17b5f4\n"
                                           "ID_0_C $038213\n"
                                           "NULLSHAPE $00aca1\n");
}

static NativeShapeAssets &shape_assets_for_rom(const std::uint8_t *rom,
                                               std::size_t rom_size) {
  static NativeShapeAssets assets;
  if (assets.rom_pointer == rom && assets.rom_size == rom_size && assets.rom &&
      assets.decoder) {
    return assets;
  }

  std::vector<std::uint8_t> bytes(rom, rom + rom_size);
  assets = {};
  assets.rom_pointer = rom;
  assets.rom_size = rom_size;
  assets.rom = std::make_unique<starfox::assets::RomImage>(std::move(bytes));
  auto symbols = native_symbol_map();
  assets.decoder =
      std::make_unique<starfox::assets::ShapeDecoder>(*assets.rom, symbols);
  assets.text_renderer =
      std::make_unique<starfox::render::ScaledTextRenderer>(*assets.rom,
                                                            symbols);
  assets.trigonometry = std::make_unique<starfox::simulation::TrigTables>(
      starfox::simulation::TrigTables::load(*assets.rom, symbols));
  return assets;
}

static const starfox::assets::Shape *cached_shape(NativeShapeAssets &assets,
                                                  std::uint16_t shape_address,
                                                  std::uint16_t colour_pointer,
                                                  double source_depth,
                                                  bool shadow_shape,
                                                  starfox::assets::ShapeHeader *base_header) {
  if (!assets.decoder)
    return nullptr;
  const auto base_key =
      (static_cast<ShapeCacheKey>(shape_address) << 16u) | colour_pointer;
  auto base = assets.shapes.find(base_key);
  if (base == assets.shapes.end()) {
    base = assets.shapes
               .emplace(base_key, assets.decoder->decode(shape_address, {},
                                                         colour_pointer))
               .first;
  }
  if (base_header)
    *base_header = base->second.header;
  const auto selected = shadow_shape
                            ? base->second.header.shadow_pointer
                            : starfox::assets::ShapeDecoder::select_lod_pointer(
                                  base->second.header, source_depth);
  if (selected == 0u)
    return nullptr;
  const auto shape_key =
      (static_cast<ShapeCacheKey>(selected) << 16u) | colour_pointer;
  auto shape = assets.shapes.find(shape_key);
  if (shape == assets.shapes.end()) {
    shape = assets.shapes
                .emplace(shape_key,
                         assets.decoder->decode_lod(base->second.header,
                                                    selected, colour_pointer))
                .first;
  }
  return &shape->second;
}

static std::uint16_t ram_word(std::uint32_t address) {
  return static_cast<std::uint16_t>(g_ram[address]) |
         static_cast<std::uint16_t>(g_ram[(address + 1u) & 0x1ffffu] << 8);
}

static std::uint16_t ram_word16(std::uint16_t address) {
  return static_cast<std::uint16_t>(g_ram[address]) |
         static_cast<std::uint16_t>(
             static_cast<std::uint16_t>(
                 g_ram[static_cast<std::uint16_t>(address + 1u)])
             << 8u);
}

static void copy_vram_bytes(starfox::simulation::SnesPpuState &out,
                            const Ppu *ppu) {
  for (std::size_t i = 0; i < 0x8000u; i++) {
    const std::uint16_t word = ppu->vram[i];
    out.vram[i * 2u] = static_cast<std::uint8_t>(word);
    out.vram[i * 2u + 1u] = static_cast<std::uint8_t>(word >> 8);
  }
}

static void copy_oam_bytes(starfox::simulation::SnesPpuState &out,
                           const Ppu *ppu) {
  for (std::size_t i = 0; i < 0x100u; i++) {
    const std::uint16_t word = ppu->oam[i];
    out.oam[i * 2u] = static_cast<std::uint8_t>(word);
    out.oam[i * 2u + 1u] = static_cast<std::uint8_t>(word >> 8);
  }
  std::memcpy(out.oam.data() + 0x200u, ppu->highOam, 0x20u);
}

static void
copy_mode2_horizontal_offsets(starfox::simulation::SnesPpuState &out) {
  if (!g_ram[kRamDoHofs])
    return;
  out.bg2_horizontal_offsets_enabled = true;
  const std::uint16_t source = ram_word(kRamHdmbg2Hofs2);
  for (std::size_t line = 0; line < out.bg2_horizontal_offsets.size(); line++) {
    const std::uint16_t record = static_cast<std::uint16_t>(source + line * 3u);
    out.bg2_horizontal_offsets[line] = static_cast<std::int16_t>(
        ram_word16(static_cast<std::uint16_t>(record + 1u)));
  }
}

static starfox::simulation::SnesPpuState make_ppu_state(const Ppu *ppu) {
  starfox::simulation::SnesPpuState out;
  copy_vram_bytes(out, ppu);
  std::copy(std::begin(ppu->cgram), std::end(ppu->cgram), out.cgram.begin());
  copy_oam_bytes(out, ppu);

  out.background_mode = static_cast<std::uint8_t>(PPU_mode(ppu));
  out.bg3_high_priority = PPU_bg3priority(ppu) != 0;
  out.mosaic = ppu->mosaic;
  out.object_select = ppu->obsel;
  out.bg1_character_base = static_cast<std::uint16_t>(PPU_bgTileAdr(ppu, 0));
  out.bg1_screen_base = static_cast<std::uint16_t>(PPU_bgTilemapAdr(ppu, 0));
  out.bg1_screen_size = static_cast<std::uint8_t>(ppu->bgXsc[0] & 3u);
  out.bg1_scroll_x = static_cast<std::int16_t>(ppu->hScroll[0]);
  out.bg1_scroll_y = static_cast<std::int16_t>(ppu->vScroll[0]);
  out.bg2_character_base = static_cast<std::uint16_t>(PPU_bgTileAdr(ppu, 1));
  out.bg2_screen_base = static_cast<std::uint16_t>(PPU_bgTilemapAdr(ppu, 1));
  out.bg2_screen_size = static_cast<std::uint8_t>(ppu->bgXsc[1] & 3u);
  out.bg3_character_base = static_cast<std::uint16_t>(PPU_bgTileAdr(ppu, 2));
  out.bg3_screen_base = static_cast<std::uint16_t>(PPU_bgTilemapAdr(ppu, 2));
  out.bg3_screen_size = static_cast<std::uint8_t>(ppu->bgXsc[2] & 3u);
  out.bg3_scroll_x = static_cast<std::int16_t>(ppu->hScroll[2]);
  out.bg3_scroll_y = static_cast<std::int16_t>(ppu->vScroll[2]);
  out.main_screen = ppu->screenEnabled[0];

  // Offset-per-tile lookup is a property of SNES Mode 2. DOVOFS controls
  // whether the game refreshes its table, but is already clear by the
  // post-frame presentation latch while the uploaded BG3 entries remain live.
  // Their validity bits decide which columns actually override BG2.
  out.bg2_vertical_offsets_enabled = out.background_mode == 2u;
  copy_mode2_horizontal_offsets(out);
  return out;
}

static std::uint8_t brightness(const Ppu *ppu) {
  static std::uint8_t last_visible_brightness = 15;
  const std::uint8_t level = static_cast<std::uint8_t>(ppu->inidisp & 0x0fu);
  if ((ppu->inidisp & 0x80u) == 0) {
    last_visible_brightness = level;
    return level;
  }
  return last_visible_brightness;
}

static std::uint8_t expand5(std::uint16_t value) {
  const std::uint8_t five = static_cast<std::uint8_t>(value & 0x1fu);
  return static_cast<std::uint8_t>((five << 3u) | (five >> 2u));
}

static void write_bgra(const starfox::render::Framebuffer &source,
                       const starfox::simulation::SnesPpuState &ppu_state,
                       const Ppu *ppu, std::uint8_t *pixels,
                       std::size_t pitch) {
  const std::uint8_t level = brightness(ppu);
  for (std::uint32_t y = 0; y < source.height(); y++) {
    std::uint8_t *row = pixels + static_cast<std::size_t>(y) * pitch;
    for (std::uint32_t x = 0; x < source.width(); x++) {
      const std::uint16_t cgram = ppu_state.cgram[source.get(x, y)];
      const std::uint8_t r = static_cast<std::uint8_t>(
          (static_cast<std::uint16_t>(expand5(cgram)) * level) / 15u);
      const std::uint8_t g = static_cast<std::uint8_t>(
          (static_cast<std::uint16_t>(expand5(cgram >> 5u)) * level) / 15u);
      const std::uint8_t b = static_cast<std::uint8_t>(
          (static_cast<std::uint16_t>(expand5(cgram >> 10u)) * level) / 15u);
      std::uint8_t *dst = row + static_cast<std::size_t>(x) * 4u;
      dst[0] = b;
      dst[1] = g;
      dst[2] = r;
      dst[3] = 0xff;
    }
  }
}

static std::size_t overlay_bgra_nonzero(
    const starfox::render::Framebuffer &source,
    const starfox::simulation::SnesPpuState &ppu_state, const Ppu *ppu,
    std::uint8_t *pixels, std::size_t pitch) {
  std::size_t visible = 0;
  const std::uint8_t level = brightness(ppu);
  for (std::uint32_t y = 0; y < source.height(); y++) {
    std::uint8_t *row = pixels + static_cast<std::size_t>(y) * pitch;
    for (std::uint32_t x = 0; x < source.width(); x++) {
      const auto palette_index = source.get(x, y);
      if (palette_index == 0u)
        continue;
      const std::uint16_t cgram = ppu_state.cgram[palette_index];
      std::uint8_t *dst = row + static_cast<std::size_t>(x) * 4u;
      dst[2] = static_cast<std::uint8_t>(
          (static_cast<std::uint16_t>(expand5(cgram)) * level) / 15u);
      dst[1] = static_cast<std::uint8_t>(
          (static_cast<std::uint16_t>(expand5(cgram >> 5u)) * level) / 15u);
      dst[0] = static_cast<std::uint8_t>(
          (static_cast<std::uint16_t>(expand5(cgram >> 10u)) * level) / 15u);
      dst[3] = 0xff;
      visible++;
    }
  }
  return visible;
}

static std::size_t overlay_bgra_nonzero_at(
    const starfox::render::Framebuffer &source,
    const starfox::simulation::SnesPpuState &ppu_state, const Ppu *ppu,
    std::uint8_t *pixels, std::size_t pitch, std::int32_t offset_x,
    std::int32_t offset_y, std::int32_t target_width,
    std::int32_t target_height) {
  std::size_t visible = 0;
  const std::uint8_t level = brightness(ppu);
  for (std::uint32_t y = 0; y < source.height(); y++) {
    const auto target_y = static_cast<std::int32_t>(y) + offset_y;
    if (target_y < 0 || target_y >= target_height)
      continue;
    std::uint8_t *row = pixels + static_cast<std::size_t>(target_y) * pitch;
    for (std::uint32_t x = 0; x < source.width(); x++) {
      const auto palette_index = source.get(x, y);
      if (palette_index == 0u)
        continue;
      const auto target_x = static_cast<std::int32_t>(x) + offset_x;
      if (target_x < 0 || target_x >= target_width)
        continue;
      const std::uint16_t cgram = ppu_state.cgram[palette_index];
      std::uint8_t *dst = row + static_cast<std::size_t>(target_x) * 4u;
      dst[2] = static_cast<std::uint8_t>(
          (static_cast<std::uint16_t>(expand5(cgram)) * level) / 15u);
      dst[1] = static_cast<std::uint8_t>(
          (static_cast<std::uint16_t>(expand5(cgram >> 5u)) * level) / 15u);
      dst[0] = static_cast<std::uint8_t>(
          (static_cast<std::uint16_t>(expand5(cgram >> 10u)) * level) / 15u);
      dst[3] = 0xff;
      visible++;
    }
  }
  return visible;
}

static std::size_t
count_visible_pixels(const starfox::render::Framebuffer &framebuffer) {
  std::size_t count = 0;
  for (std::uint32_t y = 0; y < framebuffer.height(); y++) {
    for (std::uint32_t x = 0; x < framebuffer.width(); x++) {
      if (framebuffer.get(x, y) != 0)
        count++;
    }
  }
  return count;
}

static std::size_t count_visible_pixels(const std::uint8_t *pixels,
                                        std::size_t pitch, int width,
                                        int height) {
  std::size_t count = 0;
  for (int y = 0; y < height; y++) {
    const auto *row = pixels + static_cast<std::size_t>(y) * pitch;
    for (int x = 0; x < width; x++) {
      const auto *p = row + static_cast<std::size_t>(x) * 4u;
      if (p[0] || p[1] || p[2])
        count++;
    }
  }
  return count;
}

static starfox::simulation::MatrixQ15
matrix_from_pose(const StarFoxEnhancedNativeShapePose *pose) {
  starfox::simulation::MatrixQ15 matrix{};
  for (std::size_t i = 0; i < matrix.size(); i++)
    matrix[i] = pose->source_view_matrix[i];
  return matrix;
}

static std::int16_t negated_source_angle(std::uint16_t angle) {
  return starfox::simulation::wrap16(-static_cast<std::int32_t>(angle));
}

static void maybe_log_native_ppu(const Ppu *ppu,
                                 const starfox::simulation::SnesPpuState &state,
                                 std::uint16_t widescreen_extra,
                                 std::size_t visible_pixels) {
  static int enabled = -1;
  static unsigned calls;
  if (enabled < 0) {
    const char *env = std::getenv("SNESRECOMP_ENHANCED_NATIVE_STATS");
    enabled = env && *env ? 1 : 0;
  }
  if (!enabled || (++calls % 30u) != 0u)
    return;
  std::fprintf(stderr,
               "[starfox-native-ppu] call=%u mode=%u main=%02x inidisp=%02x "
               "obsel=%02x ws_extra=%u visible=%zu bg2=(%d,%d) "
               "vofs=%u hofs=%u\n",
               calls, static_cast<unsigned>(state.background_mode),
               static_cast<unsigned>(state.main_screen),
               static_cast<unsigned>(ppu->inidisp),
               static_cast<unsigned>(state.object_select),
               static_cast<unsigned>(widescreen_extra), visible_pixels,
               static_cast<int>(ram_word(kRamBg2XScroll)),
               static_cast<int>(ram_word(kRamBg2Scroll)),
               state.bg2_vertical_offsets_enabled ? 1u : 0u,
               state.bg2_horizontal_offsets_enabled ? 1u : 0u);
}

static void draw_mode_layers(const starfox::simulation::SnesPpuState &ppu,
                             starfox::render::Framebuffer &framebuffer,
                             std::uint16_t widescreen_extra,
                             bool suppress_superfx_world_bg1,
                             bool anchor_edge_hud) {
  const starfox::render::BackgroundRenderer background_renderer;
  const starfox::render::SpriteRenderer sprite_renderer;
  const auto all = starfox::render::TilePriorityPass::all;
  const auto low = starfox::render::TilePriorityPass::low;
  const auto high = starfox::render::TilePriorityPass::high;
  const std::int32_t viewport_origin =
      static_cast<std::int32_t>(widescreen_extra);
  const bool extend_scene =
      widescreen_extra != 0 &&
      (ppu.background_mode == 2u || suppress_superfx_world_bg1);
  const auto bg2_scroll_x = static_cast<std::int16_t>(ram_word(kRamBg2XScroll));
  const auto bg2_scroll_y = static_cast<std::int16_t>(ram_word(kRamBg2Scroll));

  if (ppu.background_mode == 1u) {
    background_renderer.draw_bg3(ppu, framebuffer, low, viewport_origin, false);
    sprite_renderer.draw_objects(ppu, framebuffer, 0u, viewport_origin, false,
                                 anchor_edge_hud);
    if (!ppu.bg3_high_priority) {
      background_renderer.draw_bg3(ppu, framebuffer, high, viewport_origin,
                                   false);
    }
    sprite_renderer.draw_objects(ppu, framebuffer, 1u, viewport_origin, false,
                                 anchor_edge_hud);
    background_renderer.draw_bg2(ppu, bg2_scroll_x, bg2_scroll_y, framebuffer,
                                 low, viewport_origin, false);
    sprite_renderer.draw_objects(ppu, framebuffer, 2u, viewport_origin, false,
                                 anchor_edge_hud);
    background_renderer.draw_bg2(ppu, bg2_scroll_x, bg2_scroll_y, framebuffer,
                                 high, viewport_origin, false);
  } else if (ppu.background_mode == 2u) {
    background_renderer.draw_bg2(ppu, bg2_scroll_x, bg2_scroll_y, framebuffer,
                                 low, viewport_origin, extend_scene);
    sprite_renderer.draw_objects(ppu, framebuffer, 0u, viewport_origin,
                                 extend_scene, anchor_edge_hud);
    sprite_renderer.draw_objects(ppu, framebuffer, 1u, viewport_origin,
                                 extend_scene, anchor_edge_hud);
    background_renderer.draw_bg2(ppu, bg2_scroll_x, bg2_scroll_y, framebuffer,
                                 high, viewport_origin, extend_scene);
    sprite_renderer.draw_objects(ppu, framebuffer, 2u, viewport_origin,
                                 extend_scene, anchor_edge_hud);
  } else if (ppu.background_mode == 3u) {
    background_renderer.draw_bg2(ppu, bg2_scroll_x, bg2_scroll_y, framebuffer,
                                 low, viewport_origin, extend_scene);
    sprite_renderer.draw_objects(ppu, framebuffer, 0u, viewport_origin,
                                 extend_scene, anchor_edge_hud);
    // Diagnostic native scene replacement removes only the SuperFX world plane;
    // BG2 and OAM stay in the same order as Enhanced's PPU presenter.
    if (!suppress_superfx_world_bg1)
      background_renderer.draw_bg1(ppu, framebuffer, low, viewport_origin,
                                   extend_scene);
    sprite_renderer.draw_objects(ppu, framebuffer, 1u, viewport_origin,
                                 extend_scene, anchor_edge_hud);
    background_renderer.draw_bg2(ppu, bg2_scroll_x, bg2_scroll_y, framebuffer,
                                 high, viewport_origin, extend_scene);
    sprite_renderer.draw_objects(ppu, framebuffer, 2u, viewport_origin,
                                 extend_scene, anchor_edge_hud);
    if (!suppress_superfx_world_bg1)
      background_renderer.draw_bg1(ppu, framebuffer, high, viewport_origin,
                                   extend_scene);
  } else {
    background_renderer.draw_bg2(ppu, bg2_scroll_x, bg2_scroll_y, framebuffer,
                                 all, viewport_origin, extend_scene);
    background_renderer.draw_bg3(ppu, framebuffer, all, viewport_origin, false);
    for (std::uint8_t priority = 0; priority < 3u; priority++) {
      sprite_renderer.draw_objects(ppu, framebuffer, priority, viewport_origin,
                                   extend_scene, anchor_edge_hud);
    }
  }
}

static bool oam_object_is_gameplay_hud(
    const starfox::simulation::SnesPpuState &ppu, std::size_t object) {
  const auto low = object * 4u;
  const auto high = 512u + object / 4u;
  const auto high_bits =
      static_cast<std::uint8_t>(ppu.oam[high] >> ((object & 3u) * 2u));
  if (ppu.oam[low] == 0u && ppu.oam[low + 1u] == 0u &&
      ppu.oam[low + 2u] == 0u && ppu.oam[low + 3u] == 0u) {
    return false;
  }

  auto x = static_cast<std::int32_t>(ppu.oam[low]) |
           (static_cast<std::int32_t>(high_bits & 1u) << 8u);
  if (x >= 256)
    x -= 512;
  const auto y = ppu.oam[low + 1u];
  return (y < 32u && x < 128) || y >= 168u || (y >= 128u && x < 128);
}

static starfox::simulation::SnesPpuState gameplay_hud_oam_only(
    starfox::simulation::SnesPpuState ppu) {
  for (std::size_t object = 0; object < 128u; object++) {
    if (oam_object_is_gameplay_hud(ppu, object))
      continue;
    const auto low = object * 4u;
    ppu.oam[low] = 0u;
    ppu.oam[low + 1u] = 0u;
    ppu.oam[low + 2u] = 0u;
    ppu.oam[low + 3u] = 0u;
  }
  return ppu;
}

} // namespace

extern "C" int StarFoxEnhancedDrawNativePpuLayers(uint8_t *pixels, size_t pitch,
                                                  int width, int height,
                                                  uint16_t widescreen_extra,
                                                  int suppress_superfx_world_bg1,
                                                  int anchor_edge_hud) {
  if (!g_ppu || !pixels || pitch < static_cast<size_t>(width) * 4u ||
      width <= 0 || height <= 0)
    return 0;

  const auto ppu_state = make_ppu_state(g_ppu);
  const auto target_height =
      suppress_superfx_world_bg1 != 0 || anchor_edge_hud != 0
          ? static_cast<std::uint32_t>(height)
          : 192u;
  starfox::render::Framebuffer framebuffer(static_cast<std::uint32_t>(width),
                                           target_height);
  framebuffer.clear(0);
  draw_mode_layers(ppu_state, framebuffer, widescreen_extra,
                   suppress_superfx_world_bg1 != 0, anchor_edge_hud != 0);
  const auto visible_pixels = count_visible_pixels(framebuffer);
  maybe_log_native_ppu(g_ppu, ppu_state, widescreen_extra, visible_pixels);
  if (visible_pixels == 0)
    return 0;
  write_bgra(framebuffer, ppu_state, g_ppu, pixels, pitch);
  return 1;
}

extern "C" unsigned StarFoxEnhancedDrawGameplayHudSprites(
    uint8_t *pixels, size_t pitch, int width, int height,
    uint16_t widescreen_extra) {
  if (!g_ppu || !pixels || pitch < static_cast<size_t>(width) * 4u ||
      width <= 0 || height <= 0 || widescreen_extra == 0)
    return 0;

  const auto ppu_state = gameplay_hud_oam_only(make_ppu_state(g_ppu));
  starfox::render::Framebuffer framebuffer(static_cast<std::uint32_t>(width),
                                           static_cast<std::uint32_t>(height));
  const starfox::render::SpriteRenderer sprite_renderer;
  const auto viewport_origin = static_cast<std::int32_t>(widescreen_extra);
  framebuffer.clear(0);
  for (std::uint8_t priority = 0; priority < 4u; priority++) {
    sprite_renderer.draw_objects(ppu_state, framebuffer, priority,
                                 viewport_origin, true, true);
  }
  return static_cast<unsigned>(
      overlay_bgra_nonzero(framebuffer, ppu_state, g_ppu, pixels, pitch));
}

extern "C" unsigned StarFoxEnhancedDrawGameplayHudMeters(
    uint8_t *pixels, size_t pitch, int width, int height,
    uint16_t widescreen_extra, uint8_t damage, uint8_t boost, int shield_up,
    int enabled, uint8_t boss_health, uint8_t boss_max_health) {
  if (!g_ppu || !pixels || pitch < static_cast<size_t>(width) * 4u ||
      width <= 0 || height <= 0 || widescreen_extra == 0 || !enabled)
    return 0;

  const auto ppu_state = make_ppu_state(g_ppu);
  starfox::render::Framebuffer framebuffer(static_cast<std::uint32_t>(width),
                                           static_cast<std::uint32_t>(height));
  const starfox::render::SpriteRenderer sprite_renderer;
  starfox::simulation::MeterState meters{};
  meters.damage = damage;
  meters.boost = boost;
  meters.shield_up = shield_up != 0;
  meters.enabled = enabled != 0;
  meters.boss_health = boss_health;
  meters.boss_max_health = boss_max_health;
  framebuffer.clear(0);
  sprite_renderer.draw_meters(meters, framebuffer, true);
  return static_cast<unsigned>(
      overlay_bgra_nonzero_at(framebuffer, ppu_state, g_ppu, pixels, pitch, 0,
                              16, width, height));
}

extern "C" unsigned StarFoxEnhancedDrawCockpitHud(
    uint8_t *pixels, size_t pitch, int width, int height, const uint8_t *rom,
    size_t rom_size, uint8_t rotation, uint8_t colour, uint8_t damage_flags,
    int horizontal_origin, int vertical_origin,
    uint8_t normal_colour_override) {
  if (!pixels || pitch < static_cast<std::size_t>(width) * 4u || width <= 0 ||
      height <= 0 || !rom || rom_size == 0 || vertical_origin >= height)
    return 0;

  try {
    auto &assets = shape_assets_for_rom(rom, rom_size);
    if (!assets.trigonometry)
      return 0;
    starfox::render::RenderSettings settings;
    settings.colour_index_base = 7u * 16u;
    const starfox::render::SoftwareRenderer renderer{settings};
    starfox::render::Framebuffer hud(static_cast<std::uint32_t>(width), 192u);
    hud.clear(0);
    renderer.draw_cockpit_hud(*assets.trigonometry, rotation, colour,
                              damage_flags, horizontal_origin, hud,
                              normal_colour_override);

    unsigned visible = 0;
    const auto palette_level = brightness(g_ppu);
    for (std::uint32_t y = 0; y < hud.height(); y++) {
      const int target_y = vertical_origin + static_cast<int>(y);
      if (target_y < 0 || target_y >= height)
        continue;
      auto *row = pixels + static_cast<std::size_t>(target_y) * pitch;
      for (std::uint32_t x = 0; x < hud.width(); x++) {
        const auto palette_index = hud.get(x, y);
        if (palette_index == 0u)
          continue;
        const auto cgram = g_ppu ? g_ppu->cgram[palette_index]
                                 : static_cast<std::uint16_t>(0);
        auto *dst = row + static_cast<std::size_t>(x) * 4u;
        dst[2] = static_cast<std::uint8_t>(
            static_cast<std::uint16_t>(expand5(cgram)) * palette_level / 15u);
        dst[1] = static_cast<std::uint8_t>(
            static_cast<std::uint16_t>(expand5(cgram >> 5u)) * palette_level /
            15u);
        dst[0] = static_cast<std::uint8_t>(
            static_cast<std::uint16_t>(expand5(cgram >> 10u)) * palette_level /
            15u);
        dst[3] = 0xff;
        visible++;
      }
    }
    return visible;
  } catch (const std::exception &) {
    return 0;
  }
}

extern "C" unsigned StarFoxEnhancedDrawProjectedText(
    uint8_t *pixels, size_t pitch, int width, int height, const uint8_t *rom,
    size_t rom_size, uint16_t message_pointer, uint8_t colour,
    int8_t size_adjustment, const StarFoxEnhancedNativeShapePose *pose) {
  if (!pixels || pitch < static_cast<std::size_t>(width) * 4u || width <= 0 ||
      height <= 0 || !rom || rom_size == 0 || !pose)
    return 0;

  try {
    auto &assets = shape_assets_for_rom(rom, rom_size);
    if (!assets.text_renderer)
      return 0;
    const auto ppu_state = g_ppu ? make_ppu_state(g_ppu)
                                 : starfox::simulation::SnesPpuState{};
    starfox::render::Framebuffer text_frame(
        static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
    text_frame.clear(0);
    starfox::render::RenderPose render_pose;
    render_pose.x = pose->x;
    render_pose.y = pose->y;
    render_pose.z = pose->z;
    render_pose.pitch = pose->pitch;
    render_pose.yaw = pose->yaw;
    render_pose.roll = pose->roll;
    render_pose.vanish_x =
        static_cast<double>(pose->vanish_x) + pose->widescreen_extra;
    render_pose.vanish_y = pose->vanish_y;
    assets.text_renderer->draw(message_pointer, colour, size_adjustment,
                               render_pose, text_frame);
    return static_cast<unsigned>(
        overlay_bgra_nonzero(text_frame, ppu_state, g_ppu, pixels, pitch));
  } catch (const std::exception &) {
    return 0;
  }
}

extern "C" unsigned StarFoxEnhancedDrawCommsHud(
    uint8_t *pixels, size_t pitch, int width, int height, const uint8_t *rom,
    size_t rom_size, uint8_t open_count, uint8_t animation_count,
    uint8_t friend_id, uint16_t face_pointer, uint32_t text_address) {
  if (!pixels || pitch < static_cast<std::size_t>(width) * 4u || width <= 0 ||
      height <= 0 || !rom || rom_size == 0 ||
      (open_count == 0 && animation_count == 0))
    return 0;

  try {
    auto &assets = shape_assets_for_rom(rom, rom_size);
    if (!assets.text_renderer)
      return 0;
    const auto ppu_state = g_ppu ? make_ppu_state(g_ppu)
                                 : starfox::simulation::SnesPpuState{};
    starfox::render::Framebuffer comms(224u, 192u);
    comms.clear(0);
    constexpr std::uint16_t face_base = 0xb5f4u;
    std::uint8_t portrait_frame = 0;
    if (face_pointer >= face_base) {
      portrait_frame =
          static_cast<std::uint8_t>((face_pointer - face_base) / 640u);
    }
    assets.text_renderer->draw_face(portrait_frame, 48, 152, comms, 7u * 16u,
                                    false);
    if (open_count != 0 && animation_count >= 5u) {
      const bool three_lines =
          (friend_id & 0x80u) != 0 || (friend_id & 0x7fu) == 5u;
      const auto text_y = three_lines ? 153 : 169;
      assets.text_renderer->draw_game_text(text_address, 83, text_y + 1,
                                           comms, 7u * 16u, 9u, 175);
      assets.text_renderer->draw_game_text(text_address, 82, text_y, comms,
                                           7u * 16u, std::nullopt, 174);
    }

    unsigned visible = 0;
    const int origin_x = (width - 224) / 2;
    const int origin_y = 16;
    const auto palette_level = brightness(g_ppu);
    for (std::uint32_t y = 0; y < comms.height(); y++) {
      const int target_y = origin_y + static_cast<int>(y);
      if (target_y < 0 || target_y >= height)
        continue;
      auto *row = pixels + static_cast<std::size_t>(target_y) * pitch;
      for (std::uint32_t x = 0; x < comms.width(); x++) {
        const auto palette_index = comms.get(x, y);
        if (palette_index == 0u)
          continue;
        const int target_x = origin_x + static_cast<int>(x);
        if (target_x < 0 || target_x >= width)
          continue;
        const auto cgram = ppu_state.cgram[palette_index];
        auto *dst = row + static_cast<std::size_t>(target_x) * 4u;
        dst[2] = static_cast<std::uint8_t>(
            static_cast<std::uint16_t>(expand5(cgram)) * palette_level / 15u);
        dst[1] = static_cast<std::uint8_t>(
            static_cast<std::uint16_t>(expand5(cgram >> 5u)) * palette_level /
            15u);
        dst[0] = static_cast<std::uint8_t>(
            static_cast<std::uint16_t>(expand5(cgram >> 10u)) * palette_level /
            15u);
        dst[3] = 0xff;
        visible++;
      }
    }
    return visible;
  } catch (const std::exception &) {
    return 0;
  }
}

extern "C" void StarFoxEnhancedInterpolateMatrixQ15(const int16_t previous[9],
                                                    const int16_t current[9],
                                                    uint16_t alpha_q8,
                                                    int16_t out[9]) {
  if (!out)
    return;
  if (!previous || !current || alpha_q8 >= 256u) {
    if (current)
      std::memcpy(out, current, sizeof(std::int16_t) * 9u);
    else
      std::memset(out, 0, sizeof(std::int16_t) * 9u);
    return;
  }
  if (alpha_q8 == 0u) {
    std::memcpy(out, previous, sizeof(std::int16_t) * 9u);
    return;
  }

  starfox::simulation::MatrixQ15 previous_matrix{};
  starfox::simulation::MatrixQ15 current_matrix{};
  for (std::size_t i = 0; i < previous_matrix.size(); i++) {
    previous_matrix[i] = previous[i];
    current_matrix[i] = current[i];
  }
  const auto interpolated = starfox::simulation::interpolate_rotation_matrix_q15(
      previous_matrix, current_matrix,
      static_cast<double>(alpha_q8) / 256.0);
  for (std::size_t i = 0; i < interpolated.size(); i++)
    out[i] = interpolated[i];
}

extern "C" int
StarFoxEnhancedDrawNativeShape(uint8_t *pixels, size_t pitch, int width,
                               int height, const uint8_t *rom, size_t rom_size,
                               uint16_t shape_address,
                               const StarFoxEnhancedNativeShapePose *pose,
                               StarFoxEnhancedNativeShapeStats *stats) {
  if (stats)
    std::memset(stats, 0, sizeof(*stats));
  if (!pixels || pitch < static_cast<std::size_t>(width) * 4u || width <= 0 ||
      height <= 0 || !rom || rom_size == 0 || !pose || shape_address == 0u)
    return 0;

  try {
    auto &assets = shape_assets_for_rom(rom, rom_size);
    starfox::assets::ShapeHeader base_header;
    const auto source_depth =
        pose->use_source_depth_z ? pose->source_depth_z : pose->z;
    const auto *shape = cached_shape(
        assets, shape_address, pose->colour_pointer, source_depth,
        pose->use_shadow_shape != 0, &base_header);
    if (!shape)
      return 0;
    if (stats) {
      stats->decoded_vertices =
          static_cast<unsigned>(std::min<std::size_t>(shape->vertices.size(),
                                                      UINT32_MAX));
      stats->decoded_faces =
          static_cast<unsigned>(std::min<std::size_t>(shape->faces.size(),
                                                      UINT32_MAX));
      stats->selected_lod = static_cast<std::uint16_t>(shape->header.address);
    }

    starfox::render::RenderSettings settings;
    settings.colour_index_base = 7u * 16u;
    const starfox::render::SoftwareRenderer renderer{settings};
    starfox::render::Framebuffer shape_frame(
        static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
    shape_frame.clear(0);

    starfox::render::RenderPose render_pose;
    render_pose.x = pose->x;
    render_pose.y = pose->y;
    render_pose.z = pose->z;
    render_pose.pitch = pose->pitch;
    render_pose.yaw = pose->yaw;
    render_pose.roll = pose->roll;
    render_pose.vanish_x =
        static_cast<double>(pose->vanish_x) + pose->widescreen_extra;
    render_pose.vanish_y = pose->vanish_y;
    render_pose.animation_frame = pose->animation_frame;
    render_pose.colour_frame = pose->colour_frame;
    render_pose.texture_scroll_x = pose->texture_scroll_x;
    render_pose.texture_scroll_y = pose->texture_scroll_y;
    render_pose.explosion_progress = pose->explosion_progress;
    render_pose.force_colour = pose->force_colour != 0;
    render_pose.forced_colour = pose->forced_colour;
    if (pose->simple_scaled_sprite) {
      auto size_adjustment = static_cast<std::int16_t>(pose->texture_scroll_x);
      for (std::uint8_t shift = 0; shift < base_header.shift; ++shift) {
        size_adjustment =
            starfox::simulation::add16(size_adjustment, size_adjustment);
      }
      auto diameter =
          starfox::simulation::add16(base_header.size, size_adjustment);
      diameter = starfox::simulation::add16(diameter, diameter);
      render_pose.simple_scaled_sprite = true;
      render_pose.simple_sprite_colour = pose->simple_sprite_colour;
      render_pose.simple_sprite_world_size = diameter != 0 ? diameter : 1;
    }
    if (pose->use_source_view_matrix && assets.trigonometry) {
      const auto current_pitch = pose->use_interpolated_object_matrix
                                     ? pose->source_pitch
                                     : pose->pitch;
      const auto current_yaw =
          pose->use_interpolated_object_matrix ? pose->source_yaw : pose->yaw;
      const auto current_roll = pose->use_interpolated_object_matrix
                                    ? pose->source_roll
                                    : pose->roll;
      const auto current_object_matrix = starfox::simulation::transpose_q15(
          starfox::simulation::rotation_matrix_q15(
              *assets.trigonometry, negated_source_angle(current_pitch),
              negated_source_angle(current_yaw),
              negated_source_angle(current_roll)));
      auto object_matrix = current_object_matrix;
      if (pose->use_interpolated_object_matrix) {
        const auto previous_object_matrix = starfox::simulation::transpose_q15(
            starfox::simulation::rotation_matrix_q15(
                *assets.trigonometry,
                negated_source_angle(pose->previous_source_pitch),
                negated_source_angle(pose->previous_source_yaw),
                negated_source_angle(pose->previous_source_roll)));
        object_matrix = starfox::simulation::interpolate_rotation_matrix_q15(
            previous_object_matrix, current_object_matrix,
            static_cast<double>(std::min<std::uint16_t>(
                pose->object_matrix_alpha_q8, 256u)) /
                256.0);
      }
      if (pose->flatten_shadow_matrix) {
        object_matrix[1] = 0;
        object_matrix[4] = 0;
        object_matrix[7] = 0;
      }
      render_pose.rotation_matrix = starfox::simulation::multiply_matrix_q15(
          object_matrix, matrix_from_pose(pose));
      render_pose.use_rotation_matrix = true;
    }
    starfox::render::apply_source_depth_tables(
        *assets.rom, kRomDepthTables, pose->depth_thresholds,
        pose->depth_colours, pose->object_depth_offset, render_pose);

    renderer.draw(*shape, render_pose, shape_frame, false);

    std::size_t visible = 0;
    const auto palette_level = brightness(g_ppu);
    for (std::uint32_t y = 0; y < shape_frame.height(); y++) {
      auto *row = pixels + static_cast<std::size_t>(y) * pitch;
      for (std::uint32_t x = 0; x < shape_frame.width(); x++) {
        const auto colour = shape_frame.get(x, y);
        if (colour == 0u)
          continue;
        const auto cgram =
            g_ppu ? g_ppu->cgram[colour] : static_cast<std::uint16_t>(0);
        const std::uint8_t r = static_cast<std::uint8_t>(
            (static_cast<std::uint16_t>(expand5(cgram)) * palette_level) / 15u);
        const std::uint8_t g = static_cast<std::uint8_t>(
            (static_cast<std::uint16_t>(expand5(cgram >> 5u)) * palette_level) /
            15u);
        const std::uint8_t b = static_cast<std::uint8_t>(
            (static_cast<std::uint16_t>(expand5(cgram >> 10u)) *
             palette_level) /
            15u);
        auto *dst = row + static_cast<std::size_t>(x) * 4u;
        dst[0] = b;
        dst[1] = g;
        dst[2] = r;
        dst[3] = 0xff;
        visible++;
      }
    }
    if (stats)
      stats->visible_pixels =
          static_cast<unsigned>(std::min<std::size_t>(visible, UINT32_MAX));
    return visible != 0 ? 1 : 0;
  } catch (const std::exception &) {
    if (stats)
      stats->decode_failures++;
    return 0;
  }
}
