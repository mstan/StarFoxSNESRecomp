cmake_minimum_required(VERSION 3.16)
project(StarFoxSNESRecomp C)
set(CMAKE_C_STANDARD 11)

set(SNESRECOMP_ROOT ${CMAKE_SOURCE_DIR}/snesrecomp)
include(${SNESRECOMP_ROOT}/runner/runner.cmake)
find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)

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
  SNESRECOMP_TRACE=$<BOOL:${SNESRECOMP_ENABLE_TRACE}>
  SNESRECOMP_REVERSE_DEBUG=0)
if(NOT MSVC)
  target_compile_options(StarFoxSNESRecomp PRIVATE -w -Wno-implicit-function-declaration)
endif()
target_link_libraries(StarFoxSNESRecomp PRIVATE
  ${SNESRECOMP_RUNNER_LIBRARIES} SDL2::SDL2 OpenGL::GL)
if(WIN32)
  target_link_libraries(StarFoxSNESRecomp PRIVATE dbghelp)
endif()
