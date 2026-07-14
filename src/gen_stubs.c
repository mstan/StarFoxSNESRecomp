#include <stdio.h>
#include <string.h>

#include "common_cpu_infra.h"
#include "common_rtl.h"
#include "cpu_state.h"
#include "snes/apu.h"
#include "snes/cart.h"
#include "snes/snes.h"
#include "snes/spc.h"

/* Optional optimization for the ROM's standard SPC IPL block uploader at
 * $03:B109 (StarFoxDisassembly: LoadAudio). The authoritative path remains
 * executable by setting SNESRECOMP_LLE_BOUNCE=0. This body performs the same
 * packet-table walk and materializes the resulting SPC image atomically. */
static uint8_t rom8(uint8_t bank, uint16_t address) {
  return cart_read(g_snes->cart, bank, address);
}
static uint16_t rom16(uint8_t bank, uint16_t address) {
  return (uint16_t)rom8(bank,address)|((uint16_t)rom8(bank,address+1)<<8);
}
static uint8_t stream8(uint8_t *bank, uint16_t *address) {
  uint8_t v=rom8(*bank,*address);
  (*address)++;
  if(!*address) (*bank)++;
  return v;
}
static uint16_t stream16(uint8_t *bank, uint16_t *address) {
  uint16_t lo=stream8(bank,address); return lo|((uint16_t)stream8(bank,address)<<8);
}

RecompReturn HleStarFoxLoadAudio(CpuState *cpu) {
  const uint32_t table=0x03AEE9u;
  uint16_t x=cpu->X;
  g_ram[0x1f47]=rom8(3,(uint16_t)(table+x));
  x++;
  g_ram[0x1f65]=1;

  RtlApuLock();
  apu_clearPortQueue(g_snes->apu);
  unsigned packets=0, blocks=0; size_t copied=0;
  for (;;) {
    uint16_t offset=rom16(3,(uint16_t)(table+x));
    uint8_t bank=rom8(3,(uint16_t)(table+x+2));
    uint16_t declared=rom16(3,(uint16_t)(table+x+3));
    (void)declared;
    x=(uint16_t)(x+5);
    if(!offset) break;
    if(++packets>128) goto bad;
    for (;;) {
      uint16_t length=stream16(&bank,&offset);
      if(!length) break;
      uint16_t target=stream16(&bank,&offset);
      if(++blocks>2048 || copied+length>8*1024*1024u) goto bad;
      for(uint32_t i=0;i<length;i++)
        g_snes->apu->ram[(uint16_t)(target+i)]=stream8(&bank,&offset);
      copied+=length;
    }
  }

  memset(g_snes->apu->inPorts,0,sizeof(g_snes->apu->inPorts));
  memset(g_snes->apu->outPorts,0,sizeof(g_snes->apu->outPorts));
  g_snes->apu->romReadable=false;
  g_snes->apuCatchupCycles=0;
  g_snes->apu->cpuCyclesLeft=0;
  g_snes->apu->spc->a=g_snes->apu->spc->x=g_snes->apu->spc->y=0;
  if(!g_snes->apu->spc->sp) g_snes->apu->spc->sp=0xef;
  g_snes->apu->spc->pc=0x0400;
  RtlApuUnlock();

  g_ram[0x1f63]=(uint8_t)x; g_ram[0x1f64]=(uint8_t)(x>>8);
  g_ram[0x1f46]=0;
  cpu->A=0; cpu->X=(uint8_t)x; cpu->Y=(uint8_t)cpu->Y;
  cpu->P|=0x30; cpu_p_to_mirrors(cpu);
  cpu->_flag_I=0; cpu->P&=(uint8_t)~0x04;
  cpu->_flag_Z=1; cpu->_flag_N=0; cpu->P=(cpu->P&~0x82)|0x02;
  cpu->S=(uint16_t)(cpu->S+2); /* real LoadAudio ends in RTS */
  fprintf(stderr,"[starfox] accelerated SPC IPL upload: %u packets, %u blocks, %zu bytes\n",
          packets,blocks,copied);
  return RECOMP_RETURN_NORMAL;
bad:
  RtlApuUnlock();
  fprintf(stderr,"[starfox] invalid SPC packet stream; leaving LLE optimization\n");
  cpu->S=(uint16_t)(cpu->S+2);
  return RECOMP_RETURN_NORMAL;
}
