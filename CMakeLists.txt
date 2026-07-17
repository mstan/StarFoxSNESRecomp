cmake_minimum_required(VERSION 3.16)
project(StarFoxSNESRecomp C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(SNESRECOMP_ROOT ${CMAKE_SOURCE_DIR}/snesrecomp)
include(${SNESRECOMP_ROOT}/runner/runner.cmake)

# Launcher UI backend: the SHARED recomp-ui launcher (F:\Projects\recomp-ui,
# consumed as the ./recomp-ui submodule/junction) so every recomp game drives
# ONE launcher repo. The former in-tree launcher_ng has been retired from the
# engine in favour of this shared, console-agnostic launcher.
include(${CMAKE_SOURCE_DIR}/recomp-ui/recomp_ui.cmake)

find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)

set(SNESRECOMP_BUILD_VERSION "dev" CACHE STRING
    "Version recorded in Star Fox host and crash reports")

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
  ${STARFOX_GEN_SOURCES}
)

target_include_directories(StarFoxSNESRecomp PRIVATE
  ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/recomp
  ${SNESRECOMP_RUNNER_INCLUDE_DIRS} ${SDL2_INCLUDE_DIRS})
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

# Dear ImGui pre-boot launcher (GUI) from the shared recomp-ui repo. One call
# wires sources/includes/RECOMP_LAUNCHER define/CXX/asset-staging (same Star
# Fox boxart; recomp-ui's default pad.tga is already the SNES pad).
recomp_target_launcher_ui(StarFoxSNESRecomp
  BOXART ${CMAKE_SOURCE_DIR}/recomp/launcher/boxart.tga)
