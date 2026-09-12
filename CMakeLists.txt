cmake_minimum_required(VERSION 3.20)
project(StarFoxSNESRecomp C CXX)
set(CMAKE_C_STANDARD 11)

# Based on snesrecomp/tools/new_project/templates/CMakeLists.txt.in, as used
# by GundamWingEndlessDuelSNESRecomp. Keep Star Fox-specific targets below.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" _GAME_VER_RAW)
string(STRIP "${_GAME_VER_RAW}" GAME_RELEASE_VERSION)
set(SNESRECOMP_BUILD_VERSION "" CACHE STRING
    "Build version (empty = Release uses VERSION, else dev)")
if(SNESRECOMP_BUILD_VERSION STREQUAL "")
  get_property(_is_multi GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
  if(_is_multi)
    set(SNESRECOMP_BUILD_VERSION "$<IF:$<CONFIG:Release>,${GAME_RELEASE_VERSION},dev>")
  elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(SNESRECOMP_BUILD_VERSION "${GAME_RELEASE_VERSION}")
  else()
    set(SNESRECOMP_BUILD_VERSION "dev")
  endif()
endif()

set(SNESRECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/snesrecomp" CACHE PATH
    "Path to the snesrecomp checkout used by this game")
if(NOT EXISTS "${SNESRECOMP_ROOT}/runner/runner.cmake")
  message(FATAL_ERROR
      "snesrecomp submodule missing at ${SNESRECOMP_ROOT}.\n"
      "Run: git submodule update --init --recursive")
endif()
include("${SNESRECOMP_ROOT}/runner/runner.cmake")

set(RECOMP_UI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/recomp-ui" CACHE PATH
    "Path to the recomp-ui checkout used by this game")
if(NOT EXISTS "${RECOMP_UI_ROOT}/recomp_ui.cmake")
  message(FATAL_ERROR
      "recomp-ui submodule missing at ${RECOMP_UI_ROOT}.\n"
      "Run: git submodule update --init --recursive recomp-ui")
endif()
# These built-in features use a game-owned provider, so only the launcher's
# Mods view is needed; the engine's package loader remains opt-in.
set(RECOMP_UI_ENABLE_MODS ON CACHE BOOL
    "Enable recomp-ui Mods view for Star Fox built-in Enhanced features" FORCE)
include("${RECOMP_UI_ROOT}/recomp_ui.cmake")

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/starfox-enhanced/include/starfox/assets/rom.hpp")
  message(FATAL_ERROR
      "Star Fox Enhanced reference sources missing.\n"
      "Run: git submodule update --init --recursive third_party/starfox-enhanced")
endif()

# CI compiles the same host and shared sources without ROM-derived code.
# Its static archive checks compilation only; it is not a playable game.
option(STARFOX_HOST_CHECK_ONLY "Compile a ROM-free host archive for CI" OFF)
if(STARFOX_HOST_CHECK_ONLY)
  set(STARFOX_GEN_SOURCES)
  add_library(StarFoxSNESRecomp STATIC)
  message(STATUS "Star Fox: host-check archive only (no playable executable)")
else()
  file(GLOB STARFOX_GEN_SOURCES CONFIGURE_DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/src/gen/*.c")
  if(NOT STARFOX_GEN_SOURCES)
    message(FATAL_ERROR "src/gen is empty; run tools/regen.sh with the verified ROM")
  endif()
  add_executable(StarFoxSNESRecomp)
endif()

set(STARFOX_REFERENCE_SOURCES
  third_party/starfox-enhanced/src/assets/rom.cpp
  third_party/starfox-enhanced/src/assets/shape_decoder.cpp
  third_party/starfox-enhanced/src/render/background_renderer.cpp
  third_party/starfox-enhanced/src/render/scaled_text_renderer.cpp
  third_party/starfox-enhanced/src/render/software_renderer.cpp
  third_party/starfox-enhanced/src/render/sprite_renderer.cpp
  third_party/starfox-enhanced/src/simulation/math.cpp
)
add_library(StarFoxEnhancedNativeRenderer OBJECT
  src/starfox_enhanced_native.cpp
  ${STARFOX_REFERENCE_SOURCES})
target_include_directories(StarFoxEnhancedNativeRenderer PRIVATE
  ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/recomp
  ${CMAKE_SOURCE_DIR}/third_party/starfox-enhanced/include
  ${SNESRECOMP_RUNNER_INCLUDE_DIRS})
target_compile_features(StarFoxEnhancedNativeRenderer PRIVATE cxx_std_20)
if(NOT MSVC)
  set_source_files_properties(${STARFOX_REFERENCE_SOURCES} PROPERTIES
      COMPILE_OPTIONS "-w")
endif()

target_sources(StarFoxSNESRecomp PRIVATE
  ${SNESRECOMP_RUNNER_SOURCES}
  src/config.c
  src/gen_stubs.c
  src/main.c
  src/opengl.c
  src/starfox_enhanced_renderer.c
  src/starfox_presentation.c
  src/starfox_mods.c
  src/starfox_cpu_infra.c
  src/starfox_rtl.c
  src/starfox_state_menu.c
  src/starfox_spc_player.c
  src/mods/arwing64/arwing64.c
  src/mods/arwing64/arwing64_picture.c
  src/mods/arwing64/arwing64_audio.c
  src/mods/arwing64/sf64_audio_render.c
  src/mods/arwing64/arwing64_extract.c
  src/mods/arwing64/sf64_assets.c
  src/mods/arwing64/sf64_rom.c
  src/mods/arwing64/sha1.c
  third_party/gl_core/gl_core_3_1.c
  $<TARGET_OBJECTS:StarFoxEnhancedNativeRenderer>
  ${STARFOX_GEN_SOURCES}
)

target_include_directories(StarFoxSNESRecomp PRIVATE
  ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/recomp
  ${CMAKE_SOURCE_DIR}/src/mods/arwing64
  ${CMAKE_SOURCE_DIR}/third_party/stb
  ${RECOMP_UI_ROOT}/src/third_party
  ${CMAKE_SOURCE_DIR}/third_party/starfox-enhanced/include
  ${SNESRECOMP_RUNNER_INCLUDE_DIRS})
target_compile_definitions(StarFoxSNESRecomp PRIVATE
  SMW_DISABLE_LM=1 SYSTEM_VOLUME_MIXER_AVAILABLE=0
  SNESRECOMP_BUILD_VERSION="${SNESRECOMP_BUILD_VERSION}"
  SNESRECOMP_TRACE=$<BOOL:${SNESRECOMP_ENABLE_TRACE}>
  SNESRECOMP_REVERSE_DEBUG=0)
if(NOT MSVC AND STARFOX_GEN_SOURCES)
  set_source_files_properties(${STARFOX_GEN_SOURCES} PROPERTIES
      COMPILE_OPTIONS "-w;-Wno-implicit-function-declaration")
endif()
target_link_libraries(StarFoxSNESRecomp PRIVATE
  ${SNESRECOMP_RUNNER_LIBRARIES})
snesrecomp_target_fiber_compat(StarFoxSNESRecomp)
snesrecomp_target_glsl_shader(StarFoxSNESRecomp)

# The shared runner and launcher agree on SDL3 by default, SDL2 via
# -DSNESRECOMP_SDL_BACKEND=SDL2. They also own OpenGL and crash-report linking.
snesrecomp_target_sdl(StarFoxSNESRecomp)
if(NOT SNESRECOMP_ENABLE_TRACE)
  snesrecomp_target_post_mortem(StarFoxSNESRecomp TIER2)
endif()

# Dear ImGui pre-boot launcher (GUI) from the shared recomp-ui repo. One call
# wires sources/includes/RECOMP_LAUNCHER define/CXX/asset-staging (same Star
# Fox boxart; recomp-ui's default pad.tga is already the SNES pad).
recomp_target_launcher_ui(StarFoxSNESRecomp
  CONSOLE snes
  BOXART ${CMAKE_SOURCE_DIR}/recomp/launcher/boxart.tga)

option(STARFOX_STATE_TESTS "Build ROM-dependent save/rewind integration checks" OFF)
if(STARFOX_STATE_TESTS AND NOT STARFOX_HOST_CHECK_ONLY)
  get_target_property(_state_sources StarFoxSNESRecomp SOURCES)
  list(REMOVE_ITEM _state_sources src/main.c)
  add_executable(starfox_state_tests tests/starfox_state_runtime.c ${_state_sources})
  foreach(_property INCLUDE_DIRECTORIES COMPILE_DEFINITIONS LINK_LIBRARIES LINK_OPTIONS)
    get_target_property(_value StarFoxSNESRecomp ${_property})
    if(_value)
      set_property(TARGET starfox_state_tests PROPERTY ${_property} "${_value}")
    endif()
  endforeach()
endif()

# Arwing64 developer tool: extracts the Star Fox 64 Arwing from an owner ROM
# into the N64MESHB blob and renders preview PNGs through host_mesh. Shares the
# exact decoder sources the game links, so what the tool shows is what the
# game draws. Not shipped in release zips.
add_executable(arwing64_tool
  tools/arwing64_tool.c
  src/mods/arwing64/sf64_audio_render.c
  ${SNESRECOMP_ROOT}/runner/src/sha256.c
  src/mods/arwing64/sha1.c
  src/mods/arwing64/sf64_rom.c
  src/mods/arwing64/sf64_assets.c
  src/mods/arwing64/arwing64_extract.c
  ${SNESRECOMP_ROOT}/runner/src/host_mesh.c
  ${SNESRECOMP_ROOT}/runner/src/host_mesh_builder.c)
target_include_directories(arwing64_tool PRIVATE
  ${CMAKE_SOURCE_DIR}/src/mods/arwing64
  ${SNESRECOMP_ROOT}/runner/src
  ${RECOMP_UI_ROOT}/src/third_party)
if(NOT MSVC)
  target_link_libraries(arwing64_tool PRIVATE m)
endif()
