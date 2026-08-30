import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class StarFoxConfigSurfaceTests(unittest.TestCase):
    def test_enhanced_display_mode_aliases_are_wired(self):
        config_c = (ROOT / "src" / "config.c").read_text(encoding="utf-8")

        self.assertIn("ParseEnhancedDisplayMode", config_c)
        for alias in (
            '"0"',
            '"4:3"',
            '"standard_4_3"',
            '"1"',
            '"16:9"',
            '"widescreen_16_9"',
            '"2"',
            '"16:10"',
            '"widescreen_16_10"',
            '"3"',
            '"21:9"',
            '"ultrawide_21_9"',
            '"4"',
            '"32:9"',
            '"super_ultrawide_32_9"',
        ):
            self.assertIn(alias, config_c)
        self.assertIn("g_config.widescreen_extra = 132", config_c)
        self.assertIn("g_config.widescreen_extra = 272", config_c)
        self.assertIn("extra <= kWsExtraMax", config_c)
        config_h = (ROOT / "src" / "config.h").read_text(encoding="utf-8")
        self.assertIn("uint16 widescreen_extra", config_h)
        self.assertIn("bool enhanced_renderer", config_h)
        snes_widescreen_h = (
            ROOT / "snesrecomp" / "runner" / "src" / "widescreen.h"
        ).read_text(encoding="utf-8")
        self.assertIn("kWsExtraMax = 272", snes_widescreen_h)
        snes_ppu_h = (
            ROOT / "snesrecomp" / "runner" / "src" / "snes" / "ppu.h"
        ).read_text(encoding="utf-8")
        self.assertIn("kPpuExtraLeftRight = 272", snes_ppu_h)
        snes_superfx_c = (
            ROOT / "snesrecomp" / "runner" / "src" / "snes" / "superfx.c"
        ).read_text(encoding="utf-8")
        snes_superfx_h = (
            ROOT / "snesrecomp" / "runner" / "src" / "snes" / "superfx.h"
        ).read_text(encoding="utf-8")
        self.assertIn("kSuperFxWsMaxExtra = 288", snes_superfx_c)
        self.assertIn("kSuperFxWsMaxWidth = 800", snes_superfx_c)
        self.assertNotIn("SuperFxTaskObserver", snes_superfx_h)
        self.assertNotIn("superfx_set_task_observer", snes_superfx_h)
        self.assertNotIn("kSuperFxTaskEvent_Start", snes_superfx_c)
        self.assertNotIn("kSuperFxTaskEvent_Stop", snes_superfx_c)
        self.assertIn("ParsePresentationFps", config_c)
        self.assertIn("fps == 90", config_c)
        self.assertIn("fps == 480", config_c)
        main_c = (ROOT / "src" / "main.c").read_text(encoding="utf-8")
        self.assertIn("ExtraPresentationsAfterFrame", main_c)
        self.assertIn("g_config.presentation_fps > 60 ? 0 : 1", (ROOT / "src" / "opengl.c").read_text(encoding="utf-8"))
        self.assertIn("g_config.presentation_fps <= 60", main_c)

    def test_launcher_exposes_enhanced_widescreen_mod(self):
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        main_c = (ROOT / "src" / "main.c").read_text(encoding="utf-8")
        mods_c = (ROOT / "src" / "starfox_mods.c").read_text(encoding="utf-8")

        self.assertIn("RECOMP_UI_ENABLE_MODS ON", cmake)
        self.assertIn("src/starfox_mods.c", cmake)
        self.assertIn("third_party/starfox-enhanced/src/render/software_renderer.cpp", cmake)
        self.assertNotIn("src/starfox_native_shape.c", cmake)
        self.assertIn("StarFoxLauncherModsProvider", main_c)
        self.assertIn("StarFoxLauncherModsProvider(&settings,\n                                                   game_info.config_path)", main_c)
        self.assertIn("game_info.mods", main_c)
        self.assertIn("game_info.widescreen_supported = 0", main_c)
        self.assertIn("settings.widescreen_hud = 0", main_c)
        self.assertIn("Enhanced Widescreen", mods_c)
        self.assertIn('"enhanced_widescreen"', mods_c)
        self.assertIn("WriteConfigFile(mod_ctx ? mod_ctx->config_path : NULL)", mods_c)
        self.assertIn("config_path && *config_path ? config_path : \"config.ini\"", mods_c)
        self.assertNotIn('"enhanced_renderer"', mods_c)
        self.assertNotIn('{ "off", "Original 4:3" }', mods_c)
        self.assertNotIn("Widescreen HUD", mods_c)
        self.assertNotIn("Enhanced Renderer", mods_c)
        self.assertIn("Crosshair Color", mods_c)
        self.assertIn("God Mode", mods_c)
        self.assertIn("God Nuke", mods_c)
        self.assertIn("Presentation FPS", mods_c)
        self.assertIn("Show FPS", mods_c)
        self.assertIn("max_frames", main_c)
        self.assertIn("ShouldPresentFrame", main_c)
        self.assertIn("DrawPpuFrameWithoutPresent", main_c)
        self.assertIn("g_fps_sample_presentations++", main_c)
        self.assertIn("freq / 4", main_c)
        self.assertIn("PresentationHistoryRecord", main_c)
        self.assertIn("PresentationDebugPresentCurrent", main_c)

    def test_enhanced_renderer_is_explicitly_opt_in(self):
        config_ini = (ROOT / "config.ini").read_text(encoding="utf-8")
        config_c = (ROOT / "src" / "config.c").read_text(encoding="utf-8")
        main_c = (ROOT / "src" / "main.c").read_text(encoding="utf-8")
        game_info_c = (ROOT / "src" / "starfox_cpu_infra.c").read_text(encoding="utf-8")
        rtl_c = (ROOT / "src" / "starfox_rtl.c").read_text(encoding="utf-8")
        renderer_c = (
            ROOT / "src" / "starfox_enhanced_renderer.c"
        ).read_text(encoding="utf-8")
        native_h = (
            ROOT / "src" / "starfox_enhanced_native.h"
        ).read_text(encoding="utf-8")
        native_cpp = (
            ROOT / "src" / "starfox_enhanced_native.cpp"
        ).read_text(encoding="utf-8")
        infra_h = (
            ROOT / "snesrecomp" / "runner" / "src" / "common_cpu_infra.h"
        ).read_text(encoding="utf-8")

        self.assertIn("EnhancedRenderer = 0", config_ini)
        self.assertIn('"EnhancedRenderer"', config_c)
        self.assertIn('"NativeRenderer"', config_c)
        self.assertIn('"EnhancedRenderer" },', config_c)
        self.assertIn("g_config.enhanced_renderer ? 1 : 0", config_c)
        self.assertIn("RtlEnhancedRendererFrame", infra_h)
        self.assertIn("enhanced_render_frame", infra_h)
        self.assertIn(".enhanced_render_frame = &StarFoxEnhancedRenderFrame", game_info_c)
        self.assertIn("g_config.enhanced_renderer", main_c)
        self.assertIn("The generic field is hidden for\n         * this title", main_c)
        self.assertIn("g_config.enhanced_renderer = g_config.widescreen_extra != 0", main_c)
        self.assertIn("g_config.enhanced_renderer\n                   ? IntMin(g_config.widescreen_extra, kWsExtraMax)\n                   : 0", main_c)
        self.assertIn("RtlDrawDefaultPpuFrame", main_c)
        self.assertIn("StarFoxEnhancedRenderFrame", renderer_c)
        self.assertIn("default_renderer_done", renderer_c)
        self.assertIn("StarFoxDrawPpuFrame();", renderer_c)
        self.assertIn("copy_stock_center", renderer_c)
        self.assertIn("StarFoxEnhancedDrawNativePpuLayers", renderer_c)
        self.assertIn("native_shape_overlay_enabled", renderer_c)
        self.assertIn("SNESRECOMP_ENHANCED_NATIVE_SHAPES", renderer_c)
        self.assertIn('#include "config.h"', renderer_c)
        self.assertIn("!g_config.enhanced_renderer || !native_shape_overlay_enabled()", renderer_c)
        self.assertIn("source_snapshot_current", renderer_c)
        self.assertIn("StarFoxEnhancedLatchSourceFrame", renderer_c)
        self.assertIn("StarFoxEnhancedLatchSourceFrame();", rtl_c)
        self.assertIn("kRamAllst = 0x121d", rtl_c)
        self.assertIn("kRamAlFreeLst = 0x121f", rtl_c)
        self.assertIn("kObjBase = 0x0336", rtl_c)
        self.assertIn("kObjSize = 0x36", rtl_c)
        self.assertNotIn("StarFoxEnhancedSuperFxTaskEvent", renderer_c)
        self.assertNotIn("superfx_set_task_observer", rtl_c)
        self.assertIn("declined_native_ppu", renderer_c)
        self.assertIn("StarFoxEnhancedDrawNativePpuLayers", native_h)
        self.assertIn("StarFoxEnhancedDrawNativeShape", native_h)
        self.assertIn("StarFoxEnhancedDrawProjectedText", native_h)
        self.assertIn("StarFoxEnhancedDrawGameplayHudMeters", native_h)
        self.assertIn("StarFoxEnhancedDrawCockpitHud", native_h)
        self.assertIn("StarFoxEnhancedDrawCommsHud", native_h)
        self.assertIn("starfox/render/background_renderer.hpp", native_cpp)
        self.assertIn("starfox/render/scaled_text_renderer.hpp", native_cpp)
        self.assertIn("starfox/render/software_renderer.hpp", native_cpp)
        self.assertIn("starfox/render/sprite_renderer.hpp", native_cpp)
        self.assertIn("starfox/simulation/game_simulation.hpp", native_cpp)
        self.assertIn("starfox/assets/shape_decoder.hpp", native_cpp)
        self.assertIn("starfox/simulation/snes_ppu.hpp", native_cpp)
        self.assertIn("BackgroundRenderer", native_cpp)
        self.assertIn("SoftwareRenderer", native_cpp)
        self.assertIn("draw_meters", native_cpp)
        self.assertIn("draw_cockpit_hud", native_cpp)
        self.assertIn("draw_game_text", native_cpp)
        self.assertIn("draw_face", native_cpp)
        self.assertIn("ShapeDecoder", native_cpp)
        self.assertIn("SpriteRenderer", native_cpp)
        self.assertIn("copy_vram_bytes", native_cpp)
        self.assertIn("copy_oam_bytes", native_cpp)
        self.assertIn("copy_mode2_horizontal_offsets", native_cpp)
        self.assertIn("out.bg2_vertical_offsets_enabled = out.background_mode == 2u", native_cpp)
        self.assertNotIn("if (g_ram[kRamDoVofs])", native_cpp)
        self.assertIn("viewport_origin", native_cpp)
        self.assertIn("widescreen_extra != 0 &&", native_cpp)
        self.assertIn("(ppu.background_mode == 2u || suppress_superfx_world_bg1)", native_cpp)
        self.assertIn("PPU_bgTileAdr", native_cpp)
        self.assertIn("PPU_bgTilemapAdr", native_cpp)
        self.assertIn("kRamAllst = 0x121d", renderer_c)
        self.assertIn("kRamAlFreeLst = 0x121f", renderer_c)
        self.assertIn("kRamViewPosX = 0x00c1", renderer_c)
        self.assertIn("kRamViewPosY = 0x00c3", renderer_c)
        self.assertIn("kRamViewPosZ = 0x00c5", renderer_c)
        self.assertIn("kRamVanishX = 0x00ca", renderer_c)
        self.assertIn("kRamVanishY = 0x00cc", renderer_c)
        self.assertIn("kRamDepthTabPtr = 0x1259", renderer_c)
        self.assertIn("kRamMat11W = 0x15d7", renderer_c)
        self.assertIn("kRamWmat11W = 0x161b", renderer_c)
        self.assertIn("kRamGameFrame = 0x15bb", renderer_c)
        self.assertIn("kRamPlayerFlyMode = 0x1565", renderer_c)
        self.assertIn("kRamWhichFriend = 0x191f", renderer_c)
        self.assertIn("kRamFriendsMsg = 0x1920", renderer_c)
        self.assertIn("kRamMsgCount1 = 0x1922", renderer_c)
        self.assertIn("kRamMsgCount2 = 0x1923", renderer_c)
        self.assertIn("kRamShieldUp = 0x1752", renderer_c)
        self.assertIn("kRamShadowHeight = 0x19dc", renderer_c)
        self.assertIn("kGsuFacePtr = 0x0018", renderer_c)
        self.assertIn("kGsuPlayerFlyMode = 0x0174", renderer_c)
        self.assertIn("kGsuShadowHeight = 0x0204", renderer_c)
        self.assertIn("kObjBase = 0x0336", renderer_c)
        self.assertIn("kObjSize = 0x36", renderer_c)
        self.assertIn("kObjAuxDepthOffset = 0x1cdf", renderer_c)
        self.assertIn("kObjAuxColourFrame = 0x1ce6", renderer_c)
        self.assertIn("kObjAuxAnimationFrame = 0x1ce7", renderer_c)
        self.assertIn("kObjAuxColourTable = 0x1cea", renderer_c)
        self.assertIn("kObjAuxTextureScrollX = 0x1cf4", renderer_c)
        self.assertIn("kObjAuxTextureScrollY = 0x1cf5", renderer_c)
        self.assertIn("kSourceVanishDefaultX = 112", renderer_c)
        self.assertIn("kSourceVanishDefaultY = 96", renderer_c)
        self.assertIn("kRomDepthThresholdDefault = 0x8faa", renderer_c)
        self.assertIn("kShapeNull = 0xaca1", renderer_c)
        self.assertIn("!object->shape || object->shape == kShapeNull", renderer_c)
        self.assertIn("kRamHudRotation = 0x154e", renderer_c)
        self.assertIn("kGsuHudColour = 0x3512", renderer_c)
        self.assertIn("kGsuHudDamageFlags = 0x3514", renderer_c)
        self.assertIn("g_source_snapshot.hud_rotation & 0x8000u", renderer_c)
        self.assertIn("kPfmShadows = 0x08", renderer_c)
        self.assertIn("kShadowForcedColour = 0x09", renderer_c)
        self.assertIn("kNativeWorldMinActiveObjects = 8", renderer_c)
        self.assertIn("kNativeWorldHoldMinActiveObjects = 6", renderer_c)
        self.assertNotIn("kNativeWorldMinDrawObjects", renderer_c)
        self.assertIn("kNativeWorldMinDrawnShapes = 2", renderer_c)
        self.assertIn("kNativeWorldMinVisiblePixels = 4096", renderer_c)
        self.assertIn("kNativeWorldHoldMinVisiblePixels = 2048", renderer_c)
        self.assertIn("update_native_world_replacement(native_world_ready, &stats)", renderer_c)
        self.assertIn("object.object_depth_offset = ram_byte(pointer + kObjAuxDepthOffset)", renderer_c)
        self.assertIn("object.colour_frame = ram_byte(pointer + kObjAuxColourFrame)", renderer_c)
        self.assertIn("object.animation_frame = ram_byte(pointer + kObjAuxAnimationFrame)", renderer_c)
        self.assertIn("object.colour_pointer = ram_word(pointer + kObjAuxColourTable)", renderer_c)
        self.assertIn("static int8_t ram_i8(uint32_t address)", renderer_c)
        self.assertIn("object.texture_scroll_x = ram_i8(pointer + kObjAuxTextureScrollX)", renderer_c)
        self.assertIn("object.texture_scroll_y = ram_i8(pointer + kObjAuxTextureScrollY)", renderer_c)
        self.assertIn("pose->simple_scaled_sprite = 1", renderer_c)
        self.assertIn("pose->simple_sprite_colour = object->object_depth_offset", renderer_c)
        self.assertNotIn(
            "(kAsfPartObj | kAsfScaledSprite | kAsfTextObj)", renderer_c
        )
        self.assertIn("ram_word(kRamDepthTabPtr)", renderer_c)
        self.assertIn("gsu_depth_thresholds != 0", renderer_c)
        self.assertIn("latch_allst_objects", renderer_c)
        self.assertNotIn("latch_active_pool_complement", renderer_c)
        self.assertIn("source_snapshot_insert_drawable", renderer_c)
        self.assertIn("draw_source_snapshot_shapes", renderer_c)
        self.assertIn("snapshot.player_fly_mode = ram_byte(kRamPlayerFlyMode)", renderer_c)
        self.assertIn("snapshot.shadow_height = ram_i16(kRamShadowHeight)", renderer_c)
        self.assertIn("snapshot.player_fly_mode = (uint8_t)gsu_word(kGsuPlayerFlyMode)", renderer_c)
        self.assertIn("snapshot.shadow_height = (int16_t)gsu_word(kGsuShadowHeight)", renderer_c)
        self.assertIn("source_object_has_shadow_shape", renderer_c)
        self.assertIn("/* Shadow pass */", renderer_c)
        self.assertIn("/* Normal object pass */", renderer_c)
        self.assertLess(renderer_c.index("/* Shadow pass */"), renderer_c.index("/* Normal object pass */"))
        self.assertIn("g_source_snapshot.player_fly_mode & kPfmShadows", renderer_c)
        self.assertIn("world_to_camera_f64", renderer_c)
        self.assertIn("world_x, g_source_snapshot.shadow_height", renderer_c)
        self.assertIn("pose->use_shadow_shape = 1", renderer_c)
        self.assertIn("pose->flatten_shadow_matrix = 1", renderer_c)
        self.assertIn("pose->force_colour = 1", renderer_c)
        self.assertIn("pose->forced_colour = kShadowForcedColour", renderer_c)
        self.assertIn("if (!source_snapshot_current() || !cart", renderer_c)
        self.assertNotIn("if (!ws_extra || !source_snapshot_current()", renderer_c)
        self.assertIn("unsupported_shadow", renderer_c)
        self.assertNotIn("kGsuDrawList", renderer_c)
        self.assertNotIn("capture_gsu_draw_list", renderer_c)
        self.assertIn("native_frame_looks_suspect", renderer_c)
        self.assertNotIn("!frame->widescreen_extra || source_snapshot_current()", renderer_c)
        self.assertNotIn("kRamXalBlks", renderer_c)
        self.assertNotIn("xal_address", renderer_c)
        self.assertNotIn("kXalColourTable", renderer_c)
        self.assertIn("StarFoxEnhancedDrawNativeShape", renderer_c)
        self.assertIn("source_view_matrix", native_h)
        self.assertIn("source_depth_z", native_h)
        self.assertIn("use_source_depth_z", native_h)
        self.assertIn("StarFoxEnhancedInterpolateMatrixQ15", native_h)
        self.assertIn("use_interpolated_object_matrix", native_h)
        self.assertIn("use_shadow_shape", native_h)
        self.assertIn("flatten_shadow_matrix", native_h)
        self.assertIn("force_colour", native_h)
        self.assertIn("forced_colour", native_h)
        self.assertIn("simple_scaled_sprite", native_h)
        self.assertIn("simple_sprite_colour", native_h)
        self.assertIn("decoded_vertices", native_h)
        self.assertIn("decoded_faces", native_h)
        self.assertIn("selected_lod", native_h)
        self.assertIn("SNESRECOMP_ENHANCED_NATIVE_SHAPE_DIAGNOSTICS", renderer_c)
        self.assertIn("SNESRECOMP_ENHANCED_NATIVE_SHAPE_DIAGNOSTICS_FRAME", renderer_c)
        self.assertIn("log_native_shape_diagnostic", renderer_c)
        self.assertIn("stats->decoded_vertices", native_cpp)
        self.assertIn("stats->decoded_faces", native_cpp)
        self.assertIn("stats->selected_lod", native_cpp)
        self.assertIn("renderer_stats->vertices += stats.decoded_vertices", renderer_c)
        self.assertIn("renderer_stats->faces += stats.decoded_faces", renderer_c)
        self.assertIn("multiply_matrix_q15", native_cpp)
        self.assertIn("pose->use_shadow_shape != 0", native_cpp)
        self.assertIn("base->second.header.shadow_pointer", native_cpp)
        self.assertIn("render_pose.force_colour = pose->force_colour != 0", native_cpp)
        self.assertIn("render_pose.forced_colour = pose->forced_colour", native_cpp)
        self.assertIn("render_pose.simple_scaled_sprite = true", native_cpp)
        self.assertIn("render_pose.simple_sprite_colour = pose->simple_sprite_colour", native_cpp)
        self.assertIn("render_pose.simple_sprite_world_size", native_cpp)
        self.assertIn("object_matrix[1] = 0", native_cpp)
        self.assertIn("object_matrix[4] = 0", native_cpp)
        self.assertIn("object_matrix[7] = 0", native_cpp)
        for retail_symbol in (
            '"SHADESTAB2_0 $038b2a\\n"',
            '"SHADESTAB2_1 $038b42\\n"',
            '"SHADESTAB2_2 $038b5a\\n"',
            '"SHADESTAB2_3 $038b72\\n"',
            '"DEPTHTABLES $038f9a\\n"',
            '"SINTAB $0098a5\\n"',
            '"COSTAB $0098e5\\n"',
            '"SINTAB16 $0099e5\\n"',
            '"NULLSHAPE $00aca1\\n"',
        ):
            self.assertIn(retail_symbol, native_cpp)
        for enhanced_build_symbol in (
            "$038b0a",
            "$038b22",
            "$038b3a",
            "$038b52",
            "$038f7a",
            "$008b62",
            "$008ba2",
            "$008ca2",
            "$009500",
        ):
            self.assertNotIn(enhanced_build_symbol, native_cpp)
        self.assertIn("kRomDepthTables = 0x038f9a", native_cpp)
        self.assertIn("stbi_write_png", renderer_c)
        self.assertIn("SNESRECOMP_ENHANCED_FRAME_BMP_DIR", renderer_c)
        self.assertIn("SNESRECOMP_ENHANCED_FRAME_BMP_START", renderer_c)
        self.assertIn("SNESRECOMP_ENHANCED_FRAME_BMP_END", renderer_c)
        self.assertIn("SNESRECOMP_ENHANCED_FRAME_BMP_STEP", renderer_c)
        self.assertIn("SNESRECOMP_ENHANCED_RENDERER_STATS", renderer_c)
        self.assertIn("SNESRECOMP_ENHANCED_NATIVE_WORLD_GATE_LOG", renderer_c)
        self.assertIn("log_native_world_gate_transition", renderer_c)
        self.assertIn("allocate_bgra_scratch", renderer_c)
        self.assertIn("composite_bgra_nonzero", renderer_c)
        self.assertIn("native_world_replacement_ready", renderer_c)
        self.assertIn("source_snapshot_is_gameplay_training_world_frame", renderer_c)
        self.assertIn("StarFoxEnhancedDrawProjectedText", renderer_c)
        self.assertIn("source_object_has_native_text", renderer_c)
        self.assertIn("stats->drawn >= kNativeWorldMinDrawnShapes", renderer_c)
        self.assertIn("stats->filled_pixels >= kNativeWorldMinVisiblePixels", renderer_c)
        self.assertIn("native_world_ready = native_world_replacement_ready(&stats)", renderer_c)
        self.assertIn("update_native_world_replacement(native_world_ready, &stats)", renderer_c)
        self.assertIn("source_snapshot_can_hold_native_world", renderer_c)
        self.assertIn("stats->filled_pixels >= kNativeWorldHoldMinVisiblePixels", renderer_c)
        self.assertIn("g_native_world_replacement_active = false", renderer_c)
        self.assertIn("frame->widescreen_extra, suppress_superfx_world_bg1 ? 1 : 0", renderer_c)
        self.assertIn("mode2_scene_frame", renderer_c)
        self.assertIn("mode2_transition_frame", renderer_c)
        self.assertIn("frame->widescreen_extra != 0 && g_ppu && PPU_mode(g_ppu) == 2", renderer_c)
        self.assertIn("source_snapshot_is_gameplay_training_world_frame()", renderer_c)
        self.assertIn("source_snapshot_has_live_mode2_context", renderer_c)
        self.assertIn("g_gameplay_hud_hold_until_frame", renderer_c)
        self.assertIn("native_world_ready || suppress_superfx_world_bg1", renderer_c)
        self.assertIn("snes_frame_counter + 90", renderer_c)
        self.assertIn("kGsuDamage = 0x018c", renderer_c)
        self.assertIn("kGsuBoostAnim = 0x018e", renderer_c)
        self.assertIn("kGsuShieldUp = 0x0190", renderer_c)
        self.assertIn("kGsuMeters = 0x0200", renderer_c)
        self.assertIn("draw_gameplay_hud_meters", renderer_c)
        self.assertIn("overlay_stock_superfx_hud_meters", renderer_c)
        self.assertIn("meter_pixels=%u", renderer_c)
        self.assertIn("meter_pixels < 32u", renderer_c)
        self.assertIn("!mode2_scene_frame", renderer_c)
        self.assertIn("if (!native_ppu_done && !suppress_superfx_world_bg1 && !mode2_scene_frame)", renderer_c)
        self.assertIn("mode2_native_overlay", renderer_c)
        self.assertIn("mode2_scene_frame && drawn != 0", renderer_c)
        self.assertIn("if (suppress_superfx_world_bg1 || mode2_native_overlay)", renderer_c)
        self.assertIn("draw_comms_hud(frame->pixels", renderer_c)
        self.assertIn("overlay_stock_superfx_comms_region", renderer_c)
        self.assertIn("declined_ppu=%u", renderer_c)
        self.assertIn("free(native_world)", renderer_c)
        self.assertNotIn("SNESRECOMP_ENHANCED_NATIVE_SCENE_REPLACEMENT_DIAGNOSTIC", renderer_c)
        self.assertIn("suppress_superfx_world_bg1", native_h)
        self.assertIn("suppress_superfx_world_bg1", native_cpp)
        self.assertIn("if (!suppress_superfx_world_bg1)", native_cpp)
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("third_party/starfox-enhanced/src/render/software_renderer.cpp", cmake)
        self.assertIn("third_party/starfox-enhanced/src/render/scaled_text_renderer.cpp", cmake)
        self.assertIn("third_party/starfox-enhanced/src/assets/shape_decoder.cpp", cmake)
        self.assertIn("${CMAKE_SOURCE_DIR}/third_party/stb", cmake)
        self.assertIn("${CMAKE_SOURCE_DIR}/recomp-ui/src/third_party", cmake)
        self.assertNotIn("src/starfox_native_shape.c", cmake)
        self.assertIn("focal", (ROOT / "third_party" / "starfox-enhanced" / "include" / "starfox" / "render" / "software_renderer.hpp").read_text(encoding="utf-8"))

    def test_starfox_stock_renderer_has_no_widescreen_hacks(self):
        rtl_c = (ROOT / "src" / "starfox_rtl.c").read_text(encoding="utf-8")
        renderer_c = (
            ROOT / "src" / "starfox_enhanced_renderer.c"
        ).read_text(encoding="utf-8")

        self.assertIn("PpuSetExtraSpace(g_ppu, 0)", rtl_c)
        self.assertIn("PpuSetWidescreenLineEnhancer(g_ppu, NULL, NULL)", rtl_c)
        self.assertNotIn("kSuperFxEnhancement_WidescreenLinearProjection", rtl_c)
        self.assertNotIn("starfox_widescreen_line_enhancer", rtl_c)
        self.assertNotIn("PpuSetExtraSpace(g_ppu, (uint16_t)g_ws_extra)", rtl_c)
        self.assertNotIn("PpuSetMode2LayerCapture(g_ppu, g_ws_extra", rtl_c)
        self.assertNotIn("superfx_latch_widescreen_frame", rtl_c)
        self.assertNotIn("superfx_set_task_observer", rtl_c)
        self.assertNotIn("clear_side_margins", renderer_c)
        self.assertNotIn("SNESRECOMP_ENHANCED_NATIVE_SCENE_REPLACEMENT_DIAGNOSTIC", rtl_c)

    def test_presentation_debugger_keys_are_wired(self):
        config_h = (ROOT / "src" / "config.h").read_text(encoding="utf-8")
        config_c = (ROOT / "src" / "config.c").read_text(encoding="utf-8")
        main_c = (ROOT / "src" / "main.c").read_text(encoding="utf-8")

        for name in (
            "PresentationDebug",
            "PresentationStepForward",
            "PresentationStepBack",
        ):
            self.assertIn(f"kKeys_{name}", config_h)
            self.assertIn(f"S({name})", config_c)
            self.assertIn(f"kKeys_{name}", main_c)
        self.assertIn("kPresentationHistoryFrames = 120", main_c)

    def test_legacy_widescreen_hud_keys_are_parse_only(self):
        config_h = (ROOT / "src" / "config.h").read_text(encoding="utf-8")
        config_c = (ROOT / "src" / "config.c").read_text(encoding="utf-8")
        rtl_c = (ROOT / "src" / "starfox_rtl.c").read_text(encoding="utf-8")
        config_ini = (ROOT / "config.ini").read_text(encoding="utf-8")
        mods_c = (ROOT / "src" / "starfox_mods.c").read_text(encoding="utf-8")

        self.assertIn("bool widescreen_hud", config_h)
        self.assertIn('"WidescreenHud"', config_c)
        self.assertNotIn("WidescreenHud =", config_ini)
        self.assertNotIn("Widescreen HUD", mods_c)
        self.assertNotIn("g_config.widescreen_hud", rtl_c)
        self.assertNotIn("g_config.widescreen_hud_oam", rtl_c)
        self.assertNotIn("PpuSetWsHudOamBand(g_ppu, g_config.widescreen_hud_oam_height", rtl_c)


if __name__ == "__main__":
    unittest.main()
