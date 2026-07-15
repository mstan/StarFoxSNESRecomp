cmake_minimum_required(VERSION 3.16)
project(StarFoxSNESRecomp C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(SNESRECOMP_ROOT ${CMAKE_SOURCE_DIR}/snesrecomp)
include(${SNESRECOMP_ROOT}/runner/runner.cmake)
find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)

option(STARFOX_ENABLE_LAUNCHER "Build the RmlUi pre-game launcher" ON)
set(SNESRECOMP_BUILD_VERSION "dev" CACHE STRING
    "Version recorded in Star Fox host and crash reports")
set(STARFOX_LAUNCHER_SOURCES)
if(STARFOX_ENABLE_LAUNCHER)
  # Keep the release self-contained: build the same vendored FreeType/RmlUi
  # stack used by Super Mario World's launcher instead of requiring system
  # packages or copying DLLs from another project.
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
  set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
  set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
  set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
  set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
  add_subdirectory(${SNESRECOMP_ROOT}/lib/freetype
                   ${CMAKE_BINARY_DIR}/freetype EXCLUDE_FROM_ALL)
  if(NOT TARGET Freetype::Freetype)
    add_library(Freetype::Freetype ALIAS freetype)
  endif()

  set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
  set(RMLUI_FONT_ENGINE freetype CACHE STRING "" FORCE)
  set(RMLUI_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)
  # RmlUi 6.0's bundled robin_hood header mis-detects bitness under the
  # current MinGW GCC. The standard containers are fully supported and avoid
  # carrying a compiler-specific patch in the pinned submodule.
  set(RMLUI_THIRDPARTY_CONTAINERS OFF CACHE BOOL "" FORCE)
  add_subdirectory(${SNESRECOMP_ROOT}/lib/RmlUi
                   ${CMAKE_BINARY_DIR}/rmlui EXCLUDE_FROM_ALL)

  list(APPEND STARFOX_LAUNCHER_SOURCES
    ${SNESRECOMP_ROOT}/runner/src/launcher/launcher_gui.cpp
    ${SNESRECOMP_ROOT}/lib/RmlUi/Backends/RmlUi_Renderer_GL3.cpp
    ${SNESRECOMP_ROOT}/lib/RmlUi/Backends/RmlUi_Platform_SDL.cpp)

  # RmlUi 6.0's GL3 backend uses std::all_of without including <algorithm>.
  # Force the missing standard header for compilers which do not happen to
  # pull it in transitively, while leaving the pinned dependency untouched.
  set_property(SOURCE
    ${SNESRECOMP_ROOT}/lib/RmlUi/Backends/RmlUi_Renderer_GL3.cpp
    APPEND PROPERTY COMPILE_OPTIONS
    "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-include>"
    "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:algorithm>")
endif()

file(GLOB STARFOX_GEN_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/gen/*.c)
if(NOT STARFOX_GEN_SOURCES)
  message(FATAL_ERROR "src/gen is empty; run tools/regen.sh with the verified ROM")
endif()

add_executable(StarFoxSNESRecomp
  ${SNESRECOMP_RUNNER_SOURCES}
  src/config.c
  src/fiber_compat.c
  src/gen_stubs.c
  src/glsl_shader.c
  src/main.c
  src/opengl.c
  src/post_mortem.c
  src/starfox_cpu_infra.c
  src/starfox_rtl.c
  src/starfox_spc_player.c
  third_party/gl_core/gl_core_3_1.c
  ${STARFOX_LAUNCHER_SOURCES}
  ${STARFOX_GEN_SOURCES}
)

target_include_directories(StarFoxSNESRecomp PRIVATE
  ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/recomp
  ${SNESRECOMP_RUNNER_INCLUDE_DIRS} ${SDL2_INCLUDE_DIRS})
if(STARFOX_ENABLE_LAUNCHER)
  target_include_directories(StarFoxSNESRecomp PRIVATE
    ${SNESRECOMP_ROOT}/runner/src/launcher
    ${SNESRECOMP_ROOT}/lib/RmlUi/Backends)
  target_compile_definitions(StarFoxSNESRecomp PRIVATE SNES_LAUNCHER=1)
  target_link_libraries(StarFoxSNESRecomp PRIVATE RmlUi::RmlUi)
endif()
target_compile_definitions(StarFoxSNESRecomp PRIVATE
  SMW_DISABLE_LM=1 SYSTEM_VOLUME_MIXER_AVAILABLE=0
  SNESRECOMP_BUILD_VERSION="${SNESRECOMP_BUILD_VERSION}"
  SNESRECOMP_TRACE=$<BOOL:${SNESRECOMP_ENABLE_TRACE}>
  SNESRECOMP_REVERSE_DEBUG=0)
if(NOT MSVC)
  target_compile_options(StarFoxSNESRecomp PRIVATE
    "$<$<COMPILE_LANGUAGE:C>:-w>"
    "$<$<COMPILE_LANGUAGE:C>:-Wno-implicit-function-declaration>"
    "$<$<COMPILE_LANGUAGE:CXX>:-w>")
endif()
target_link_libraries(StarFoxSNESRecomp PRIVATE
  ${SNESRECOMP_RUNNER_LIBRARIES} SDL2::SDL2 OpenGL::GL)
if(WIN32)
  target_link_libraries(StarFoxSNESRecomp PRIVATE dbghelp)
endif()

if(STARFOX_ENABLE_LAUNCHER)
  # Shared launcher markup/fonts first, then the game's optional skin so
  # theme.rcss can override the neutral snesrecomp defaults.
  add_custom_command(TARGET StarFoxSNESRecomp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf
            $<TARGET_FILE_DIR:StarFoxSNESRecomp>/launcher
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${SNESRECOMP_ROOT}/runner/src/launcher/assets
            $<TARGET_FILE_DIR:StarFoxSNESRecomp>/launcher
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/recomp/launcher
            $<TARGET_FILE_DIR:StarFoxSNESRecomp>/launcher
    COMMENT "Staging Star Fox launcher assets")
endif()
