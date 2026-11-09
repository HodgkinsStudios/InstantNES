#include "cpu_internal.h"
#include <stddef.h>
#ifdef IN_MMC5_CHR_WRITE_TRACE
#include <stdio.h>
#endif
#ifdef IN_MMC3_TRACE
#include <stdio.h>
#endif
/* Bank wrapping is on the PPU/CPU hot path. Power-of-two cartridge
   capacities use bit masking; irregular capacities retain exact modulo. */
static size_t wrap(size_t n,size_t total){
 if(!total)return 0;
 if((total&(total-1u))==0)return n&(total-1u);
 return n%total;
}
uint8_t in_mapper_prg_read(in_system *s,uint16_t a){
 size_t off=(size_t)(a-0x8000);
 if(s->mapper==5){
  unsigned slot=(unsigned)(off/8192), mode=s->mmc5_prg_mode;
  unsigned reg, width, within;
  if(mode==0){reg=4;width=4;within=slot;}
  else if(mode==1){reg=slot<2?2:4;width=2;within=slot&1;}
  else if(mode==2){reg=slot<2?2:slot==2?3:4;width=slot<2?2:1;within=slot<2?slot:0;}
  else {reg=slot+1;width=1;within=0;}
  unsigned raw=s->mmc5_prg_regs[reg];
  unsigned bank=((raw&0x7f)&~(width-1u))+within;
  if(reg!=4 && !(raw&0x80))
   return s->mmc5_prg_ram[(bank*8192u+(off&8191u))%sizeof(s->mmc5_prg_ram)];
  return s->prg[wrap((size_t)bank*8192+(off&8191),s->prg_size)];
 }
 if(s->mapper==10){
  size_t count=s->prg_size/16384u;
  size_t bank=off<16384u?s->mmc2_prg:count-1u;
  return s->prg[wrap(bank,count)*16384u+(off&16383u)];
 }
 if(s->mapper==180){
  size_t count=s->prg_size/16384u;
  size_t bank=off<16384u?0u:s->simple_prg_bank;
  return s->prg[wrap(bank,count)*16384u+(off&16383u)];
 }
 if(s->mapper==140){
  size_t count=s->prg_size/32768u;
  return s->prg[wrap(s->simple_prg_bank,count)*32768u+(off&32767u)];
 }
 if(s->mapper==9){
  size_t count=s->prg_size/8192,slot=off/8192;
  size_t bank=slot==0?s->mmc2_prg:(count>=3?count-3+slot-1:slot);
  return s->prg[wrap(bank,count)*8192+(off&8191)];
 }
 if(s->mapper==4){
  size_t count=s->prg_size/8192,bank=0,slot=off/8192;
  size_t last=count-1,prev=count-2;
  switch(slot){case 0:bank=(s->bank_select&0x40)?prev:s->bank_regs[6];break;
  case 1:bank=s->bank_regs[7];break;
  case 2:bank=(s->bank_select&0x40)?s->bank_regs[6]:prev;break;
  default:bank=last;break;}
  return s->prg[wrap(bank,count)*8192+(off&8191)];
 }
 /* Discrete-logic cartridge banking. All banks wrap to installed ROM size. */
 if(s->mapper==2 || s->mapper==71){
  size_t count=s->prg_size/16384;
  size_t bank=off<16384?s->simple_prg_bank:count-1;
  return s->prg[wrap(bank,count)*16384+(off&16383)];
 }
 if(s->mapper==7 || s->mapper==11 || s->mapper==34 || s->mapper==66){
  /* NINA-001 uses the same 32 KiB PRG bank register as BNROM. */
  size_t count=s->prg_size/32768;
  return s->prg[wrap(s->simple_prg_bank,count)*32768+(off&32767)];
 }
 if(s->mapper==1){
  size_t banks=s->prg_size/16384,mode=(s->mmc1_control>>2)&3,bank=s->mmc1_prg&15;
  if(mode<=1)off=wrap((bank&~1u)*16384+(off&32767),s->prg_size);
  else if(mode==2)off=wrap((off<16384?0:bank)*16384+(off&16383),s->prg_size);
  else off=wrap((off<16384?bank:banks-1)*16384+(off&16383),s->prg_size);
  return s->prg[off];
 }
 return s->prg[wrap(off,s->prg_size)];
}
static size_t chr_offset(in_system *s,uint16_t a){
 if(s->mapper==5){
  /* MMC5 has independent sprite ($5120-$5127) and background
     ($5128-$512B) CHR register sets. In 8x8 mode, the background
     four-register set repeats across both pattern tables. */
  /* ExRAM mode 1 supplies a per-background-tile CHR bank, with the
     upper two bits selecting that tile's palette. The bank applies to
     the two pattern bytes fetched for the tile. */
  if(!s->mmc5_chr_fetch_sprite && s->mmc5_extended_active && s->mmc5_exram_mode==1){
   unsigned bank=(s->mmc5_extended_tile&63u)|((unsigned)s->mmc5_chr_upper<<6);
   return wrap((size_t)bank*4096u+(a&4095u),s->chr_size?s->chr_size:8192);
  }
  unsigned sprite=s->mmc5_chr_fetch_sprite;
  unsigned slot=(a/1024)%(sprite?8u:(s->mmc5_chr_mode==0?8u:4u));
  unsigned mode=s->mmc5_chr_mode;
  unsigned group=1u<<(3-mode);
  unsigned reg=sprite?(slot/group)*group+group-1u:
   (mode==0?11u:8u+(slot/group)*group+group-1u);
  unsigned bank=s->mmc5_chr_regs[reg];
  /* MMC5 CHR registers index banks in units of the selected CHR mode.
     A register value of 3 in 4 KiB mode means the fourth 4 KiB bank,
     not a 1 KiB index rounded down to a 4 KiB boundary. */
  return wrap(((size_t)(bank&~(group-1u))+(slot&(group-1u)))*1024+(a&1023),s->chr_size?s->chr_size:8192);
 }
 if(s->mapper==9 || s->mapper==10){
  unsigned bank=a<0x1000?(s->mmc2_latch0?s->mmc2_chr_fe0:s->mmc2_chr_fd0):(s->mmc2_latch1?s->mmc2_chr_fe1:s->mmc2_chr_fd1);
  return wrap((size_t)bank*4096+(a&4095),s->chr_size?s->chr_size:8192);
 }
 if(s->mapper==4){
  unsigned slot=a/1024,bank;
  if(!(s->bank_select&0x80)){
   if(slot<2)bank=(s->bank_regs[0]&~1u)+slot;
   else if(slot<4)bank=(s->bank_regs[1]&~1u)+(slot-2);
   else bank=s->bank_regs[slot-2];
  }else{
   if(slot<4)bank=s->bank_regs[slot+2];
   else if(slot<6)bank=(s->bank_regs[0]&~1u)+(slot-4);
   else bank=(s->bank_regs[1]&~1u)+(slot-6);
  }
  return wrap((size_t)bank*1024+(a&1023),s->chr_size?s->chr_size:8192);
 }
 if(s->mapper==34 && s->mapper34_nina)
  return wrap((size_t)(a<4096?s->mmc1_chr0:s->mmc1_chr1)*4096+(a&4095),s->chr_size?s->chr_size:8192);
 if(s->mapper==3 || s->mapper==11 || s->mapper==66 || s->mapper==87 || s->mapper==140){
  /* Decode whole 8 KiB CHR banks, preserving the 13-bit PPU offset.
     A CHR-RAM board has one 8 KiB bank; oversized register values alias
     installed banks rather than producing partial-bank addresses. */
  size_t count=(s->chr_size?s->chr_size:8192u)/8192u;
  size_t bank=s->mapper==3?s->cnrom_bank:s->simple_chr_bank;
  return wrap(bank,count)*8192u+(a&8191u);
 }
 if(s->mapper==1){
  unsigned bank=(s->mmc1_control&0x10)?(a<4096?s->mmc1_chr0:s->mmc1_chr1):(s->mmc1_chr0&~1u);
  size_t off=(s->mmc1_control&0x10)?((size_t)bank*4096+(a&4095)):((size_t)bank*4096+a);
  return wrap(off,s->chr_size?s->chr_size:8192);
 }
 return wrap(a,s->chr_size?s->chr_size:8192);
}
uint8_t in_mapper_chr_read(in_system *s,uint16_t a){
 uint8_t v=s->chr[chr_offset(s,a)];
 /* MMC2 and MMC4 have different latch address decoding.
    Latches update after the triggering pattern byte is returned. */
 if(s->mapper==9){
  if(a==0x0fd8)s->mmc2_latch0=0;
  else if(a==0x0fe8)s->mmc2_latch0=1;
  else if(a>=0x1fd8 && a<=0x1fdf)s->mmc2_latch1=0;
  else if(a>=0x1fe8 && a<=0x1fef)s->mmc2_latch1=1;
 }else if(s->mapper==10){
  if(a>=0x0fd8 && a<=0x0fdf)s->mmc2_latch0=0;
  else if(a>=0x0fe8 && a<=0x0fef)s->mmc2_latch0=1;
  else if(a>=0x1fd8 && a<=0x1fdf)s->mmc2_latch1=0;
  else if(a>=0x1fe8 && a<=0x1fef)s->mmc2_latch1=1;
 }
 return v;
}
void in_mapper_chr_write(in_system *s,uint16_t a,uint8_t v){if(s->chr_ram)s->chr[chr_offset(s,a)]=v;}
/* MMC5 PRG-RAM mapping is shared by $6000-$7FFF and RAM-mapped
   $8000-$DFFF windows. The final $E000-$FFFF window is ROM-only. */
void in_mmc5_prg_write(in_system *s,uint16_t a,uint8_t v){
 if(s->mmc5_ram_protect1!=2 || s->mmc5_ram_protect2!=1)return;
 unsigned bank;
 if(a<0x8000){bank=s->mmc5_prg_regs[0]&15;}
 else {
  unsigned slot=(a-0x8000)/8192,mode=s->mmc5_prg_mode;
  unsigned reg,width,within;
  if(slot==3)return;
  if(mode==0){return;}
  else if(mode==1){reg=slot<2?2:4;width=2;within=slot&1;}
  else if(mode==2){reg=slot<2?2:slot==2?3:4;width=slot<2?2:1;within=slot<2?slot:0;}
  else {reg=slot+1;width=1;within=0;}
  unsigned raw=s->mmc5_prg_regs[reg];
  if(raw&0x80)return;
  bank=((raw&0x7f)&~(width-1u))+within;
 }
 s->mmc5_prg_ram[((size_t)bank*8192+(a&8191))&(sizeof(s->mmc5_prg_ram)-1u)]=v;
}

/* NES 2.0 submapper 1 identifies the bus-conflict discrete boards for
   UxROM/CNROM. The CPU and ROM simultaneously drive the data bus on writes;
   the value captured by the latch is their bitwise AND. Keep legacy iNES
   behavior unchanged because its header cannot distinguish board variants. */
static uint8_t discrete_write_value(in_system *s,uint16_t a,uint8_t v){
 if(s->nes2 && s->submapper==1 && (s->mapper==2 || s->mapper==3))
  return (uint8_t)(v & in_mapper_prg_read(s,a));
 return v;
}
void in_mapper_write(in_system *s,uint16_t a,uint8_t v){
 if(s->mapper==9 || s->mapper==10){
  switch(a&0xf000){
  case 0xa000:s->mmc2_prg=v&15;break;
  case 0xb000:s->mmc2_chr_fd0=v&31;break;
  case 0xc000:s->mmc2_chr_fe0=v&31;break;
  case 0xd000:s->mmc2_chr_fd1=v&31;break;
  case 0xe000:s->mmc2_chr_fe1=v&31;break;
  case 0xf000:if(s->mirroring!=6)s->mirroring=(v&1)?0:1;break;
  }return;
 }
 if(s->mapper==180){s->simple_prg_bank=v;return;}
 if(s->mapper==140){s->simple_prg_bank=(v>>4)&3;s->simple_chr_bank=v&15;return;}
 if(s->mapper==2){s->simple_prg_bank=discrete_write_value(s,a,v);return;}
 if(s->mapper==7){s->simple_prg_bank=v&15;s->mirroring=(v&0x10)?3:2;return;}
 if(s->mapper==11){s->simple_prg_bank=v&3;s->simple_chr_bank=(v>>4)&15;return;}
 if(s->mapper==34){if(!s->mapper34_nina)s->simple_prg_bank=v;return;} /* BNROM */
 if(s->mapper==66){s->simple_prg_bank=(v>>4)&3;s->simple_chr_bank=v&3;return;}
 if(s->mapper==71){if(a>=0xc000)s->simple_prg_bank=v&15;
  else if(a>=0x9000 && a<0xa000 && s->mapper71_mirror)s->mirroring=(v&0x10)?3:2;return;}
 if(s->mapper==3){s->cnrom_bank=discrete_write_value(s,a,v);return;}
 if(s->mapper==1){
  /* MMC1 ignores immediately consecutive CPU-cycle writes (RMW second write). */
  if(s->mmc1_last_write_valid && s->cpu_bus_clock_active &&
     s->master_cpu_cycles==s->mmc1_last_write_cycle+1)return;
  s->mmc1_last_write_cycle=s->master_cpu_cycles;
  s->mmc1_last_write_valid=s->cpu_bus_clock_active;
  if(v&0x80){s->mmc1_shift=0x10;s->mmc1_control|=0x0c;return;}
  unsigned complete=s->mmc1_shift&1;
  s->mmc1_shift=(uint8_t)((s->mmc1_shift>>1)|((v&1)<<4));
  if(complete){unsigned reg=(a>>13)&3;
   if(reg==0)s->mmc1_control=s->mmc1_shift;
   if(reg==1)s->mmc1_chr0=s->mmc1_shift;
   if(reg==2)s->mmc1_chr1=s->mmc1_shift;
   if(reg==3)s->mmc1_prg=s->mmc1_shift;
   s->mmc1_shift=0x10;
   switch(s->mmc1_control&3){case 0:s->mirroring=2;break;case 1:s->mirroring=3;break;case 2:s->mirroring=4;break;case 3:s->mirroring=5;break;}
  }return;
 }
 if(s->mapper!=4)return;
#ifdef IN_MMC3_TRACE
 if((a&0xe001)>=0xc000)fprintf(stderr,"MMC3_IRQ_WRITE cpu=%llu scan=%u dot=%u reg=%04X value=%u counter=%u latch=%u reload=%u enabled=%u pending=%u\n",(unsigned long long)s->master_cpu_cycles,s->ppu_scanline,s->ppu_dot,a&0xe001,v,s->mmc3_irq_counter,s->mmc3_irq_latch,s->mmc3_irq_reload,s->mmc3_irq_enable,s->mmc3_irq_pending);
#endif
 switch(a&0xe001){
 case 0x8000:s->bank_select=v;break;
 case 0x8001:s->bank_regs[s->bank_select&7]=v;break;
 case 0xa000:if(s->mirroring!=6)s->mirroring=(v&1)?0:1;break;
 case 0xa001:s->prg_ram_enable=(v>>7)&1;s->prg_ram_protect=(v>>6)&1;break;
 case 0xc000:s->mmc3_irq_latch=v;break;
 case 0xc001:s->mmc3_irq_reload=1;break;
 case 0xe000:s->mmc3_irq_enable=0;s->mmc3_irq_pending=0;s->irq_line=0;break;
 case 0xe001:s->mmc3_irq_enable=1;break;
 }
}
void in_mapper_ppu_address_source(in_system *s,uint16_t a,unsigned source){
 if(s->mapper!=4)return;
 unsigned high=(a&0x1000)!=0;
#ifdef IN_MMC3_TRACE
 const char *origin=source==1?"PPUADDR":source==2?"PPUDATA_READ":source==3?"PPUDATA_WRITE":"RENDER";
 const uint64_t low_before=s->ppu_a12_low_ticks;
 const unsigned previous=s->ppu_a12_last;
 const unsigned before=s->mmc3_irq_counter;
 const unsigned reload=s->mmc3_irq_reload;
#endif
 if(!high){
  /* Start a fresh low-period measurement on the falling edge. */
  if(s->ppu_a12_last)s->ppu_a12_low_ticks=0;
  s->ppu_a12_last=0;
#ifdef IN_MMC3_TRACE
  if(previous)fprintf(stderr,"MMC3_A12 FALL source=%s cpu=%llu scan=%u dot=%u addr=%04X low=%llu irq=%u\n",origin,(unsigned long long)s->master_cpu_cycles,s->ppu_scanline,s->ppu_dot,a,(unsigned long long)low_before,before);
#endif
  return;
 }
 if(!s->ppu_a12_last && s->ppu_a12_low_ticks>=8){
  const unsigned natural_zero=(s->mmc3_irq_counter==0 && !s->mmc3_irq_reload);
  if(s->mmc3_irq_counter==0 || s->mmc3_irq_reload)s->mmc3_irq_counter=s->mmc3_irq_latch;
  else s->mmc3_irq_counter--;
  s->mmc3_irq_reload=0;
  if(s->mmc3_irq_counter==0 && s->mmc3_irq_enable && !(s->mmc3_revision==IN_MMC3_REV_A && natural_zero)){s->mmc3_irq_pending=1;s->irq_line=1;}
#ifdef IN_MMC3_TRACE
  fprintf(stderr,"MMC3_IRQ_CLOCK source=%s cpu=%llu scan=%u dot=%u addr=%04X low=%llu before=%u after=%u latch=%u reload=%u enabled=%u pending=%u\n",origin,(unsigned long long)s->master_cpu_cycles,s->ppu_scanline,s->ppu_dot,a,(unsigned long long)low_before,before,s->mmc3_irq_counter,s->mmc3_irq_latch,reload,s->mmc3_irq_enable,s->mmc3_irq_pending);
#endif
 }
#ifdef IN_MMC3_TRACE
 if(!previous)fprintf(stderr,"MMC3_A12 RISE source=%s cpu=%llu scan=%u dot=%u addr=%04X low=%llu qualify=%u irq_before=%u irq_after=%u reload=%u pending=%u\n",origin,(unsigned long long)s->master_cpu_cycles,s->ppu_scanline,s->ppu_dot,a,(unsigned long long)low_before,(unsigned)(low_before>=8),before,s->mmc3_irq_counter,reload,s->mmc3_irq_pending);
#endif
 s->ppu_a12_last=1;s->ppu_a12_low_ticks=0;
}

void in_mapper_ppu_address(in_system *s,uint16_t a){in_mapper_ppu_address_source(s,a,0);}

/* MMC5 register handling, including expansion pulse and PCM channels. */
uint8_t in_mmc5_read(in_system *s,uint16_t a){
 if(a==0x5015)return (uint8_t)((s->mmc5_audio_length[0]?1:0)|(s->mmc5_audio_length[1]?2:0));
 if(a==0x5010){uint8_t v=(uint8_t)((s->mmc5_pcm_control&1)|(s->mmc5_pcm_irq?0x80:0));s->mmc5_pcm_irq=0;s->irq_line=(s->mmc5_irq_enabled && s->mmc5_irq_pending);return v;}
 /* CPU cannot read ExRAM in modes 0/1: those reads leave the bus undriven. */
 if(a>=0x5c00 && a<=0x5fff)return s->mmc5_exram_mode>=2?s->mmc5_exram[a-0x5c00]:s->cpu_open_bus;
 if(a==0x5204){uint8_t v=(s->mmc5_irq_pending?0x80:0)|(s->mmc5_in_frame?0x40:0);s->mmc5_irq_pending=0;s->irq_line=(s->mmc5_pcm_irq && (s->mmc5_pcm_control&0x80));return v;}
 if(a==0x5205)return (uint8_t)(s->mmc5_mul_a*s->mmc5_mul_b);
 if(a==0x5206)return (uint8_t)(((unsigned)s->mmc5_mul_a*s->mmc5_mul_b)>>8);
 return 0;
}
void in_mmc5_write(in_system *s,uint16_t a,uint8_t v){
 static const uint8_t lengths[32]={10,254,20,2,40,4,80,6,160,8,60,10,14,12,26,14,12,16,24,18,48,20,96,22,192,24,72,26,16,28,32,30};
 if((a>=0x5000&&a<=0x5003)||(a>=0x5004&&a<=0x5007)){
  unsigned c=(a-0x5000)/4,r=(a-0x5000)&3;
  s->mmc5_audio_regs[c*4+r]=v;
  if(r==2)s->mmc5_audio_period[c]=(uint16_t)((s->mmc5_audio_period[c]&0x700)|v);
  if(r==3){s->mmc5_audio_period[c]=(uint16_t)((s->mmc5_audio_period[c]&255)|((v&7)<<8));if(s->mmc5_audio_enable&(1u<<c))s->mmc5_audio_length[c]=lengths[v>>3];s->mmc5_audio_env_start[c]=1;s->mmc5_audio_phase[c]=0;}
  return;
 }
 if(a==0x5015){s->mmc5_audio_enable=v&3;for(unsigned c=0;c<2;c++)if(!(v&(1u<<c)))s->mmc5_audio_length[c]=0;return;}
 if(a==0x5010){s->mmc5_pcm_control=v&0x81;s->mmc5_pcm_irq=0;s->irq_line=(s->mmc5_irq_enabled && s->mmc5_irq_pending);return;}
 if(a==0x5011){if(!(s->mmc5_pcm_control&1))s->mmc5_pcm_output=v;return;}

 if(a>=0x5c00 && a<=0x5fff){
  /* In modes 0/1 the PPU owns ExRAM during active rendering. CPU
     writes are ignored until the rendering interval ends. */
  /* Modes 0/1 are PPU-owned during the detected rendering interval.
     Modes 2/3 are CPU RAM and CPU read-only RAM respectively, and
     must not inherit that rendering lockout. */
  const unsigned ppu_owned=s->mmc5_exram_mode<=1;
  const unsigned active_rendering=(s->ppu_mask&0x18)!=0 &&
    s->ppu_scanline<240 && s->mmc5_in_frame;
  if(s->mmc5_exram_mode!=3 && !(ppu_owned && active_rendering))
   s->mmc5_exram[a-0x5c00]=v;
  return;
 }
 if(a>=0x5113 && a<=0x5117){s->mmc5_prg_regs[a-0x5113]=v;return;}
#ifdef IN_MMC5_CHR_WRITE_TRACE
 if(a>=0x5100 && a<=0x5130 && (a==0x5101 || a==0x5130 || (a>=0x5120 && a<=0x512b))){
  static unsigned trace_count=0;
  if(trace_count++<500)fprintf(stderr,"MMC5_CHR_WRITE cycle=%llu pc=%04x address=%04x value=%02x scan=%u dot=%u\n",(unsigned long long)s->master_cpu_cycles,s->cpu.pc,a,v,s->ppu_scanline,s->ppu_dot);
 }
#endif
 if(a>=0x5120 && a<=0x512b){
  /* $512x is a 10-bit latch. $5130 contributes bits when this register
     is written, not continuously during rendering. Bank units are
     determined by CHR mode at write time. */
  unsigned mode=s->mmc5_chr_mode;
  unsigned shift=3u-mode;
  unsigned bank=(unsigned)v<<shift;
  if(mode==3)bank|=(unsigned)s->mmc5_chr_upper<<8;
  else if(mode==2)bank|=(unsigned)(s->mmc5_chr_upper&1u)<<9;
  s->mmc5_chr_regs[a-0x5120]=(uint16_t)(bank&1023u);
  return;
 }
 switch(a){
 case 0x5100:s->mmc5_prg_mode=v&3;break;
 case 0x5130:s->mmc5_chr_upper=v&3;break;
 case 0x5101:s->mmc5_chr_mode=v&3;break;
 case 0x5102:s->mmc5_ram_protect1=v&3;break;
 case 0x5103:s->mmc5_ram_protect2=v&3;break;
 case 0x5104:s->mmc5_exram_mode=v&3;break;
 case 0x5105:s->mmc5_nt_map=v;break;
 case 0x5106:s->mmc5_fill_tile=v;break;
 case 0x5107:s->mmc5_fill_attr=v&3;break;
 case 0x5200:s->mmc5_split_control=v;break;
 case 0x5201:s->mmc5_split_scroll=v;break;
 case 0x5202:s->mmc5_split_bank=v;break;
 case 0x5203:s->mmc5_irq_target=v;break;
 case 0x5204:s->mmc5_irq_enabled=(v>>7)&1;s->irq_line=(s->mmc5_irq_enabled && s->mmc5_irq_pending)||(s->mmc5_pcm_irq && (s->mmc5_pcm_control&0x80));break;
 case 0x5205:s->mmc5_mul_a=v;break;
 case 0x5206:s->mmc5_mul_b=v;break;
 }
}
