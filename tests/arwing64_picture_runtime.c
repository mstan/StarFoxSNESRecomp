#include "mods/arwing64/arwing64_picture.h"
#include "snes/superfx.h"
#include "snes/snes.h"
#include "snes/cart.h"
#include "snes/ppu.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

uint8_t g_ram[0x20000];
Ppu *g_ppu;
Snes *g_snes;
static int enabled = 1;
static uint64_t generation;
int arwing64_active(void) { return enabled; }
uint64_t snes_mod_audio_reset_generation(void) { return generation; }
uint32_t arwing64_draw_player(uint8_t *pixels, size_t pitch, int width, int height,
    const uint8_t *rom, size_t size, const StarFoxEnhancedNativeShapePose *pose, int black) {
  (void)rom; (void)size; (void)pose; (void)black;
  for (int y=0; y<height; y++) memset(pixels+y*pitch,0xff,(size_t)width*4);
  return (unsigned)width*height;
}

static void put(uint8_t *p, unsigned a, unsigned value) {
  p[a] = (uint8_t)value; p[a+1] = (uint8_t)(value >> 8);
}

int main(void) {
  static Snes snes;
  static Cart cart;
  static Ppu ppu;
  static uint8_t rom[65536], ram[65536];
  const uint8_t program[] = {0x43,0x4e,0x4c,0xe1,0x3d,0x4c,0x00};
  memcpy(rom + 0xac1d, program, sizeof(program));
  g_snes=&snes; snes.cart=&cart; g_ppu=&ppu;
  cart.superfx = superfx_create(rom,sizeof(rom),ram,sizeof(ram));
  assert(cart.superfx);
  const unsigned shapes[] = {0xd320,0xd374,0xd3ac,0xd3e4};
  StarFoxEnhancedNativeShapePose pose;
  for (unsigned scenario=0; scenario<7; scenario++) {
    SuperFx *fx=cart.superfx;
    superfx_reset(fx); arwing64_picture_reset();
    memset(ram,0,sizeof(ram)); memset(g_ram,0,sizeof(g_ram));
    memset(&ppu,0,sizeof(ppu));
    const unsigned shape=shapes[scenario%4];
    put(g_ram,0x1238,0x336); put(g_ram,0x33a,shape);
    put(ram,0x21e,0x1000); put(ram,0x1008,shape);
    put(ram,0x1010,9); put(ram,0x1012,7); put(ram,0x1014,400);
    put(ram,0x34,112); put(ram,0x36,96);
    if (scenario==4) { put(ram,0x1000,0x1040); put(ram,0x1048,shape); }
    if (scenario==5) put(ram,0x1000,0x1000); /* cycle */
    if (scenario==6) g_ram[0x14db]=3; /* cockpit */
    fx->r[3].data=0x1008; fx->r[1].data=4; fx->r[2].data=3;
    fx->scmr=0x39; fx->scbr=0x0b;
    arwing64_picture_pre_frame();
    superfx_cpu_write_io(fx,0x3034,1);
    superfx_cpu_write_io(fx,0x301e,0x1d);
    superfx_cpu_write_io(fx,0x301f,0xac);
    superfx_sync(fx,10000);
    assert(ram[0x1008]==(uint8_t)shape && ram[0x1009]==(shape>>8));
    memcpy(ppu.vram,ram+0x2c00,0x5400); ppu.bgmode=2;
    arwing64_picture_begin_draw();
    assert(arwing64_picture_pose(32,&pose)==(scenario<4));
    if (scenario>=4) { assert(ppu.renderVram==NULL); continue; }
    assert(pose.x==7 && pose.y==9 && pose.z==400 && pose.widescreen_extra==32);
    uint32_t image[256*224]={0};
    assert(arwing64_picture_draw((uint8_t *)image,256*4,256,224,rom,sizeof(rom),0));
    assert(image[19*256+20]==0); /* later private PLOT stays in front */
    assert(image[20*256+20]==0xffffffffu); /* unoccluded mesh remains */
    assert(image[180*256+100]==0); /* HUD stays in front */
    uint16_t saved=ppu.vram[19*16]; ppu.vram[19*16]^=0x5555;
    arwing64_picture_begin_draw();
    assert(arwing64_picture_pose(0,&pose));
    assert(PpuRenderVram(&ppu)[19*16]==ppu.vram[19*16]); /* new HUD retained */
    ppu.vram[19*16]=saved; ppu.vram[1]^=1;
    arwing64_picture_begin_draw();
    assert(!arwing64_picture_pose(0,&pose) && !ppu.renderVram); /* changed world */
    ppu.vram[1]^=1;
    generation++; arwing64_picture_pre_frame(); arwing64_picture_begin_draw();
    assert(!arwing64_picture_pose(0,&pose)); /* load/reset invalidates history */
  }
  enabled=0; arwing64_picture_pre_frame(); arwing64_picture_begin_draw();
  assert(superfx_get_enhancement_mode(cart.superfx)==kSuperFxEnhancement_None);
  assert(!ppu.renderVram);
  superfx_destroy(cart.superfx);
  puts("arwing64_picture_runtime: PASS");
}
