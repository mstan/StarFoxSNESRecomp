#pragma once
#include <stdbool.h>
#include <stdint.h>

void StarFoxStateMenuInit(void);
void StarFoxStateMenuOpenSave(void);
void StarFoxStateMenuOpenRewind(void);
bool StarFoxStateMenuIsOpen(void);
void StarFoxStateMenuKey(int key, int repeat);
/* Poll even while paused. Consume the closing iteration as well. */
bool StarFoxStateMenuPoll(uint32_t inputs, uint32_t ticks);
uint32_t StarFoxStateMenuGuestInput(uint32_t inputs);
void StarFoxStateMenuNoteFrame(const uint32_t *pixels, int w, int h);
void StarFoxStateMenuDraw(uint8_t *pixels, int pitch, int w, int h);
void StarFoxStateMenuShutdown(void);
