#pragma once
#include "starfox_enhanced_native.h"

void arwing64_picture_reset(void);
void arwing64_picture_pre_frame(void);
void arwing64_picture_begin_draw(void);
int arwing64_picture_pose(unsigned extra, StarFoxEnhancedNativeShapePose *pose);
unsigned arwing64_picture_draw(uint8_t *pixels, size_t pitch, int width, int height,
                               const uint8_t *rom, size_t rom_size, unsigned extra);
void arwing64_picture_debug(void (*send_line)(const char *));
