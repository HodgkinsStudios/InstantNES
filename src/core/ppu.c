/* Portable NTSC PPU: per-dot pixel output and v/t scrolling transfers.
   Sprite fetch/evaluation and CPU register access timing remain approximate. */
#include "cpu_internal.h"
#ifdef IN_MMC5_FETCH_TRACE
#include <stdio.h>
#endif
#ifdef IN_MMC3_TRACE
#include <stdio.h>
#endif
/* NTSC palette: fixed RGB approximation, not composite-video simulation. */
static const uint32_t nes_rgb[64]={
0x666666,0x002a88,0x1412a7,0x3b00a4,0x5c007e,0x6e0040,0x6c0600,0x561d00,
0x333500,0x0b4800,0x005200,0x004f08,0x00404d,0x000000,0x000000,0x000000,
0xadadad,0x155fd9,0x4240ff,0x7527fe,0xa01acc,0xb71e7b,0xb53120,0x994e00,
0x6b6d00,0x388700,0x0c9300,0x008f32,0x007c8d,0x000000,0x000000,0x000000,
0xfffeff,0x64b0ff,0x9290ff,0xc676ff,0xf36aff,0xfe6ecc,0xfe8170,0xea9e22,
0xbcbe00,0x88d800,0x5ce430,0x45e082,0x48cdde,0x4f4f4f,0x000000,0x000000,
0xfffeff,0xc0dfff,0xd3d2ff,0xe8c8ff,0xfbc2ff,0xfec4ea,0xfeccc5,0xf7d8a5,
0xe4e594,0xcfef96,0xbdf4ab,0xb3f3cc,0xb5ebf2,0xb8b8b8,0x000000,0x000000};
static uint16_t nt_index(const in_system *s,uint16_t a){
 uint16_t x=(uint16_t)((a-0x2000)&0x0fff);
 unsigned table=x>>10;
 unsigned bank=s->mirroring==6?table:s->mirroring==2?0:s->mirroring==3?1:s->mirroring==4?(table&1):s->mirroring==5?(table>>1):s->mirroring?(table&1):(table>>1);
 return (uint16_t)(bank*1024+(x&1023));
}
static uint8_t ppu_read_source(in_system *s,uint16_t a,unsigned source){
 a&=0x3fff;
#ifdef IN_MMC3_TRACE
 if(s->mapper==4 && source!=0)fprintf(stderr,"MMC3_PPU_ACCESS source=%s cpu=%llu scan=%u dot=%u addr=%04X a12=%u low=%llu irq=%u\n",source==1?"PPUADDR":source==2?"PPUDATA_READ":"PPUDATA_WRITE",(unsigned long long)s->master_cpu_cycles,s->ppu_scanline,s->ppu_dot,a,(unsigned)((a&0x1000)!=0),(unsigned long long)s->ppu_a12_low_ticks,s->mmc3_irq_counter);
#endif
 in_mapper_ppu_address_source(s,a,source);
 if(a<0x2000){
  /* Extended-attribute CHR overrides belong only to background pattern
     fetches, never CPU-driven PPUDATA accesses. Preserve the in-flight
     background tile latch while servicing the CPU access. */
  if(s->mapper==5 && source!=0){
   uint8_t active=s->mmc5_extended_active;
   s->mmc5_extended_active=0;
   uint8_t value=in_mapper_chr_read(s,a);
   s->mmc5_extended_active=active;
   return value;
  }
  return in_mapper_chr_read(s,a);
 }
 if(a<0x3f00){
  if(s->mapper==5){unsigned x=(a-0x2000)&0xfff,nt=(s->mmc5_nt_map>>(2*(x>>10)))&3;
   if(nt==2)return s->mmc5_exram_mode==0?s->mmc5_exram[x&1023]:0;
   if(nt==3)return (x&0x3ff)<960?s->mmc5_fill_tile:(uint8_t)(s->mmc5_fill_attr*0x55);
   return s->ppu_vram[nt*1024+(x&1023)];}
  return s->ppu_vram[nt_index(s,a)];}
 a=(uint16_t)((a-0x3f00)&31);
 if((a&3)==0)a&=15;
 return s->ppu_palette[a];
}
static void ppu_write_source(in_system *s,uint16_t a,uint8_t v,unsigned source){
 a&=0x3fff;
#ifdef IN_MMC3_TRACE
 if(s->mapper==4 && source!=0)fprintf(stderr,"MMC3_PPU_ACCESS source=PPUDATA_WRITE cpu=%llu scan=%u dot=%u addr=%04X a12=%u low=%llu irq=%u\n",(unsigned long long)s->master_cpu_cycles,s->ppu_scanline,s->ppu_dot,a,(unsigned)((a&0x1000)!=0),(unsigned long long)s->ppu_a12_low_ticks,s->mmc3_irq_counter);
#endif
 in_mapper_ppu_address_source(s,a,source);
 if(a<0x2000){in_mapper_chr_write(s,a,v);return;}
 if(a<0x3f00){
  if(s->mapper==5){unsigned x=(a-0x2000)&0xfff,nt=(s->mmc5_nt_map>>(2*(x>>10)))&3;
   if(nt==2){if(s->mmc5_exram_mode==0)s->mmc5_exram[x&1023]=v;}
   else if(nt!=3)s->ppu_vram[nt*1024+(x&1023)]=v;
   return;}
  s->ppu_vram[nt_index(s,a)]=v;return;}
 a=(uint16_t)((a-0x3f00)&31);
 if((a&3)==0)a&=15;
 s->ppu_palette[a]=v&0x3f;
}
static uint8_t ppu_read(in_system *s,uint16_t a){return ppu_read_source(s,a,0);}
/* PPU I/O data latch is analog: undriven bits eventually discharge.
   Approximate each bit's lifetime deterministically at 20 NTSC frames.
   Write-only register reads do not drive or refresh the latch. */
#define PPU_IO_DECAY_CPU_CYCLES (20u * 29781u)
static void ppu_io_decay(in_system *s){
 unsigned bit;
 for(bit=0;bit<8;bit++)
  if((s->ppu_open_bus&(1u<<bit)) &&
     s->master_cpu_cycles-s->ppu_bus_bit_last_driven[bit] >= PPU_IO_DECAY_CPU_CYCLES)
   s->ppu_open_bus=(uint8_t)(s->ppu_open_bus&~(1u<<bit));
}
static void ppu_io_drive(in_system *s,uint8_t v,uint8_t mask){
 unsigned bit;
 s->ppu_open_bus=(uint8_t)((s->ppu_open_bus&~mask)|(v&mask));
 for(bit=0;bit<8;bit++)if(mask&(1u<<bit))
  s->ppu_bus_bit_last_driven[bit]=s->master_cpu_cycles;
}
uint8_t in_ppu_register_read(in_system *s,uint16_t a){
 ppu_io_decay(s);
 uint8_t v=s->ppu_open_bus;uint16_t address;
 switch(a&7){
 case 2:
  v=(uint8_t)((s->ppu_status&0xe0)|(s->ppu_open_bus&0x1f));
  /* Reading status on the dot immediately preceding the vblank set
     suppresses that frame's vblank flag and NMI. The PPU status
     register is cleared on every read, as on the 2C02. */
  if(s->ppu_scanline==241 && s->ppu_dot==0)
   s->ppu_vblank_suppressed=1;
  s->ppu_status&=(uint8_t)~0x80;
  /* A status read deasserts vblank and suppresses an unserviced NMI. */
  s->nmi_pending=0;
  s->ppu_latch=0;
  break;
 case 4:v=s->ppu_oam[s->ppu_oam_addr];break;
 case 7:
  address=s->ppu_v&0x3fff;
  if(address>=0x3f00){v=(uint8_t)(((ppu_read_source(s,address,2)&(s->ppu_mask&1?0x30:0x3f)))|(s->ppu_open_bus&0xc0));s->ppu_read_buffer=ppu_read_source(s,(uint16_t)(address-0x1000),2);}
  else{v=s->ppu_read_buffer;s->ppu_read_buffer=ppu_read_source(s,address,2);}
  s->ppu_v=(uint16_t)((s->ppu_v+((s->ppu_ctrl&4)?32:1))&0x7fff);
  in_mapper_ppu_address_source(s,(uint16_t)(s->ppu_v&0x3fff),2);
  break;
 default:break; /* write-only registers return the PPU I/O latch */
 }
 if((a&7)==2)ppu_io_drive(s,v,0xe0);
 else if((a&7)==4 || (a&7)==7)ppu_io_drive(s,v,0xff);
 return v;
}
void in_ppu_register_write(in_system *s,uint16_t a,uint8_t v){
 ppu_io_drive(s,v,0xff);
 switch(a&7){
 case 0:
  if(!(s->ppu_ctrl&0x80)&&(v&0x80)&&(s->ppu_status&0x80))s->nmi_pending=1;
  if((s->ppu_ctrl&0x80)&&!(v&0x80))s->nmi_pending=0;
  s->ppu_ctrl=v;s->ppu_t=(uint16_t)((s->ppu_t&~0x0c00)|((v&3)<<10));break;
 case 1:s->ppu_mask=v;break;
 case 3:s->ppu_oam_addr=v;break;
 case 4:s->ppu_oam[s->ppu_oam_addr++]=v;break;
 case 5:
  if(!s->ppu_latch){s->ppu_scroll_x=v;s->ppu_fine_x=v&7;s->ppu_t=(uint16_t)((s->ppu_t&~0x001f)|(v>>3));s->ppu_latch=1;}
  else{s->ppu_scroll_y=v;s->ppu_t=(uint16_t)((s->ppu_t&~0x73e0)|((v&7)<<12)|((v&0xf8)<<2));s->ppu_latch=0;}
  break;
 case 6:
  if(!s->ppu_latch){s->ppu_t=(uint16_t)((s->ppu_t&0x00ff)|((v&0x3f)<<8));s->ppu_latch=1;}
  else{s->ppu_t=(uint16_t)((s->ppu_t&0x7f00)|v);s->ppu_v=s->ppu_t;s->ppu_latch=0;
   /* CPU-driven PPUADDR transitions also reach the MMC3 A12 detector. */
#ifdef IN_MMC3_TRACE
   if(s->mapper==4)fprintf(stderr,"MMC3_PPU_ACCESS source=PPUADDR cpu=%llu scan=%u dot=%u addr=%04X a12=%u low=%llu irq=%u\n",(unsigned long long)s->master_cpu_cycles,s->ppu_scanline,s->ppu_dot,s->ppu_v&0x3fff,(unsigned)((s->ppu_v&0x1000)!=0),(unsigned long long)s->ppu_a12_low_ticks,s->mmc3_irq_counter);
#endif
   in_mapper_ppu_address_source(s,(uint16_t)(s->ppu_v&0x3fff),1);}
  break;
 case 7:ppu_write_source(s,s->ppu_v,v,3);s->ppu_v=(uint16_t)((s->ppu_v+((s->ppu_ctrl&4)?32:1))&0x7fff);in_mapper_ppu_address_source(s,(uint16_t)(s->ppu_v&0x3fff),3);break;
 default:break;
 }
}
static void render_pixel(in_system *s,unsigned y,unsigned x);
static void inc_x(in_system *s){
 if((s->ppu_v&0x001f)==31)s->ppu_v=(uint16_t)((s->ppu_v&~0x001f)^0x0400);
 else s->ppu_v++;
}
static void inc_y(in_system *s){
 if((s->ppu_v&0x7000)!=0x7000){s->ppu_v+=0x1000;return;}
 s->ppu_v&=(uint16_t)~0x7000;
 unsigned y=(s->ppu_v&0x03e0)>>5;
 if(y==29){y=0;s->ppu_v^=0x0800;}
 else if(y==31)y=0;
 else y++;
 s->ppu_v=(uint16_t)((s->ppu_v&~0x03e0)|(y<<5));
}
static void copy_x(in_system *s){s->ppu_v=(uint16_t)((s->ppu_v&~0x041f)|(s->ppu_t&0x041f));}
static void copy_y(in_system *s){s->ppu_v=(uint16_t)((s->ppu_v&~0x7be0)|(s->ppu_t&0x7be0));}

/* Shift registers are sampled before shifting each visible dot. A new tile
   enters the low byte after its fetch completes at dot 8 of each group. */
static void bg_fetch(in_system *s,unsigned dot){
 unsigned phase=(dot-1u)&7u;
 uint16_t v=s->ppu_v;
 switch(phase){
 case 1:
  s->ppu_next_nt=ppu_read(s,(uint16_t)(0x2000u|(v&0x0fffu)));
  /* MMC5 observes repeated nametable fetches to recognize an active
     rendering scanline. CPU $2007 accesses do not clock this detector. */
  if(s->mapper==5 && s->ppu_scanline<240 && dot<=256){
   if(s->mmc5_nt_fetch_count<3)++s->mmc5_nt_fetch_count;
   if(s->mmc5_nt_fetch_count==3 && dot==18){
    s->mmc5_in_frame=1;
    if(s->ppu_scanline==s->mmc5_irq_target){
     s->mmc5_irq_pending=1;
     if(s->mmc5_irq_enabled)s->irq_line=1;
    }
   }
  }
  break;
 case 3:{
  uint16_t addr=(uint16_t)(0x23c0u|(v&0x0c00u)|((v>>4)&0x38u)|((v>>2)&7u));
  unsigned shift=((v>>4)&4u)|(v&2u);
  s->ppu_next_at=(uint8_t)((ppu_read(s,addr)>>shift)&3u);
  if(s->mapper==5 && s->mmc5_exram_mode==1 && (s->ppu_mask&0x08)){
   unsigned tile=(unsigned)(v&0x03ffu);
   s->mmc5_extended_tile=s->mmc5_exram[tile];
   s->mmc5_extended_active=1;
   s->ppu_next_at=(uint8_t)(s->mmc5_extended_tile>>6);
  }else s->mmc5_extended_active=0;
  break;
 }
 case 5:{
  uint16_t addr=(uint16_t)(((s->ppu_ctrl&0x10)?0x1000u:0u)+s->ppu_next_nt*16u+((v>>12)&7u));
  s->mmc5_chr_fetch_sprite=0;s->ppu_next_low=ppu_read(s,addr);break;
 }
 case 7:{
  uint16_t addr=(uint16_t)(((s->ppu_ctrl&0x10)?0x1000u:0u)+s->ppu_next_nt*16u+((v>>12)&7u)+8u);
  s->mmc5_chr_fetch_sprite=0;s->ppu_next_high=ppu_read(s,addr);break;
 }
 }
#ifdef IN_MMC5_FETCH_TRACE
 if(s->mapper==5 && s->frames==1199 && s->ppu_scanline==80 && dot<=64 && phase==7){
  fprintf(stderr,"MMC5_BG frame=%llu scan=%u dot=%u v=%04x tile=%02x attr=%u exmode=%u exraw=%02x active=%u chrlo=%02x chrhi=%02x ntmap=%02x\n",
   (unsigned long long)s->frames,s->ppu_scanline,dot,v,s->ppu_next_nt,s->ppu_next_at,
   s->mmc5_exram_mode,s->mmc5_extended_tile,s->mmc5_extended_active,
   s->ppu_next_low,s->ppu_next_high,s->mmc5_nt_map);
 }
#endif
 if(phase==7){
  s->ppu_bg_pattern_low=(uint16_t)((s->ppu_bg_pattern_low&0xff00u)|s->ppu_next_low);
  s->ppu_bg_pattern_high=(uint16_t)((s->ppu_bg_pattern_high&0xff00u)|s->ppu_next_high);
  s->ppu_bg_attr_low=(uint16_t)((s->ppu_bg_attr_low&0xff00u)|((s->ppu_next_at&1u)?0xffu:0));
  s->ppu_bg_attr_high=(uint16_t)((s->ppu_bg_attr_high&0xff00u)|((s->ppu_next_at&2u)?0xffu:0));
 }
}
/* Secondary OAM is cleared during dots 1-64, then searched on dots
   65-256. This models the eight-sprite limit and evaluation cadence;
   the 2C02's overflow bug and individual bus reads are not yet emulated. */
static void sprite_evaluate(in_system *s,unsigned dot,unsigned next_line){
 if(dot>=1 && dot<=64 && !(dot&1))s->secondary_oam[(dot/2)-1]=0xff;
 if(dot==65){s->eval_oam_index=0;s->eval_count=0;}
 if(dot>=65 && dot<=256 && ((dot-65)%3)==0 && s->eval_oam_index<64){
  unsigned i=s->eval_oam_index++,o=i*4u;
  unsigned top=((unsigned)s->ppu_oam[o]+1u)&255u;
  unsigned height=(s->ppu_ctrl&0x20)?16u:8u;
  if(next_line>=top && next_line<top+height){
   if(s->eval_count<8){
    unsigned j=s->eval_count++;
    for(unsigned k=0;k<4;k++)s->secondary_oam[j*4+k]=s->ppu_oam[o+k];
    s->next_sprite_indices[j]=(uint8_t)i;
   }else s->ppu_status|=0x20;
  }
 }
 if(dot==257){
  s->next_sprite_count=s->eval_count;
  for(unsigned i=0;i<s->next_sprite_count;i++){
   unsigned o=i*4u;
   s->next_sprite_attr[i]=s->secondary_oam[o+2];
   s->next_sprite_x[i]=s->secondary_oam[o+3];
  }
 }
 /* Sprite pattern reads occur at separate dots within each eight-dot slot.
    Keep A12 transitions visible at their real relative fetch phases. */
 if(dot>=257 && dot<=320){
  unsigned phase=(dot-257u)&7u;
  /* Each sprite slot also performs two nametable fetches before
     its pattern bytes. These accesses matter to mapper A12 timing. */
  if(phase==1u || phase==3u){
   (void)ppu_read(s,(uint16_t)(0x2000u|(s->ppu_v&0x0fffu)));
   return;
  }
  if(phase!=5u && phase!=6u && phase!=7u)return;
  unsigned i=(dot-257u)/8u;
  unsigned addr;
  if(i>=s->next_sprite_count){
   addr=(s->ppu_ctrl&0x20)?0x1ff0u:((s->ppu_ctrl&8)?0x1ff0u:0x0ff0u);
  }else{
   unsigned o=i*4u,top=((unsigned)s->secondary_oam[o]+1u)&255u;
   unsigned height=(s->ppu_ctrl&0x20)?16u:8u;
   unsigned row=(next_line-top)&255u,tile=s->secondary_oam[o+1];
   if(s->next_sprite_attr[i]&0x80)row=height-1-row;
   if(height==16)addr=((tile&1u)?0x1000u:0u)+((tile&0xfeu)+(row/8u))*16u+(row&7u);
   else addr=((s->ppu_ctrl&8)?0x1000u:0u)+tile*16u+row;
  }
  /* Keep the CHR byte read at phase 5, but expose its pattern-table
     address to the mapper on the following dot. This is an experimental
     bus-phase refinement; scanline-zero timing remains incomplete. */
  if(phase==6u){in_mapper_ppu_address_source(s,(uint16_t)addr,0);return;}
  if(phase==7u)addr+=8u;
  s->mmc5_chr_fetch_sprite=1;
  uint8_t value=phase==5u?in_mapper_chr_read(s,(uint16_t)addr):ppu_read(s,(uint16_t)addr);
  s->mmc5_chr_fetch_sprite=0;
  if(i<s->next_sprite_count){
   if(phase==5u)s->next_sprite_low[i]=value;
   else s->next_sprite_high[i]=value;
  }
 }

}


static void activate_next_sprites(in_system *s){
 s->active_sprite_count=s->next_sprite_count;
 for(unsigned i=0;i<s->active_sprite_count;i++){
  s->sprite_indices[i]=s->next_sprite_indices[i];
  s->sprite_attr[i]=s->next_sprite_attr[i];
  s->sprite_x[i]=s->next_sprite_x[i];
  s->sprite_low[i]=s->next_sprite_low[i];
  s->sprite_high[i]=s->next_sprite_high[i];
 }
}
void in_ppu_tick(in_system *s,uint32_t cycles){
 uint32_t ticks=cycles*3;
 while(ticks--){
  /* Qualify MMC3 A12 low time in PPU dots, not number of CHR reads. */
  if(s->mapper==4 && !s->ppu_a12_last && s->ppu_a12_low_ticks<1000000)
   s->ppu_a12_low_ticks++;
  /* NTSC odd frames omit the final pre-render dot when rendering is enabled. */
  if(s->ppu_scanline==261 && s->ppu_dot==339 && s->ppu_odd_frame && (s->ppu_mask&0x18)){
   s->ppu_dot=0;s->ppu_scanline=0;s->ppu_odd_frame=0;s->frames++;
   continue;
  }
  if(s->mapper==5 && s->ppu_dot==0){
   s->mmc5_nt_fetch_count=0;
   if(s->ppu_scanline>=240)s->mmc5_in_frame=0;
  }
  if(s->ppu_dot==0 && s->ppu_scanline<240)activate_next_sprites(s);
  unsigned rendering=(s->ppu_mask&0x18)!=0;
  if(s->ppu_scanline<240 && s->ppu_dot>=1 && s->ppu_dot<=256)
   render_pixel(s,s->ppu_scanline,s->ppu_dot-1);
  if(rendering && (s->ppu_scanline<240 || s->ppu_scanline==261)){
   if((s->ppu_dot>=1 && s->ppu_dot<=256)||(s->ppu_dot>=321 && s->ppu_dot<=336)){
    /* Visible pixel is sampled first, then the shifters advance. */
    s->ppu_bg_pattern_low<<=1;s->ppu_bg_pattern_high<<=1;
    s->ppu_bg_attr_low<<=1;s->ppu_bg_attr_high<<=1;
    bg_fetch(s,s->ppu_dot);
   }
   if(((s->ppu_dot>=1 && s->ppu_dot<=256) || (s->ppu_dot>=321 && s->ppu_dot<=336)) && (s->ppu_dot%8)==0)inc_x(s);
   if(s->ppu_dot>=1 && s->ppu_dot<=320)
    sprite_evaluate(s,s->ppu_dot,(s->ppu_scanline==261)?0u:s->ppu_scanline+1u);
   if(s->ppu_dot==256)inc_y(s);
   if(s->ppu_dot==257)copy_x(s);
   /* The PPU performs two additional nametable fetches on dots 337 and
      339, including on the pre-render line. They are visible to MMC3 A12. */
   if(s->ppu_dot==337 || s->ppu_dot==339)
    (void)ppu_read(s,(uint16_t)(0x2000u|(s->ppu_v&0x0fffu)));
   if(s->ppu_scanline==261 && s->ppu_dot>=280 && s->ppu_dot<=304)copy_y(s);
  }
  if(s->ppu_scanline==241 && s->ppu_dot==1){
   if(!s->ppu_vblank_suppressed){
    s->ppu_status|=0x80;
    if(s->ppu_ctrl&0x80)s->nmi_pending=1;
   }
  }
  if(s->ppu_scanline==261 && s->ppu_dot==1){
   s->ppu_status&=(uint8_t)~0xe0;
   s->ppu_vblank_suppressed=0;
  }
  if(++s->ppu_dot==341){s->ppu_dot=0;
   if(++s->ppu_scanline==262){s->ppu_scanline=0;s->ppu_odd_frame^=1;s->frames++;}
  }
 }
}

/* Pixel composition uses the actual background shift registers and the
   sprite patterns fetched for the current scanline. */
static void render_pixel(in_system *s,unsigned y,unsigned x){
 unsigned palette=s->ppu_palette[0]&63u,bg_opaque=0;
 /* MMC5 vertical split replaces background tile/attribute/pattern data,
    not sprites. The split scroll is independent of the PPU scroll latch.
    ExRAM supplies the split nametable and $5202 selects a 4 KiB CHR bank. */
 unsigned split=0;
 if(s->mapper==5 && (s->mmc5_split_control&0x80) &&
    (s->ppu_mask&0x08) && s->mmc5_exram_mode<=1){
  unsigned boundary=s->mmc5_split_control&31u;
  unsigned column=x>>3;
  split=(s->mmc5_split_control&0x40)?column>=boundary:column<boundary;
 }
 if(split){
  unsigned sx=x&7u;
  /* MMC5 split vertical scroll wraps within its 30-row nametable. */
  unsigned sy=(y+(unsigned)s->mmc5_split_scroll)%240u;
  /* Split tile addressing starts at column zero at the split boundary,
     rather than continuing the ordinary screen tile column. */
  unsigned boundary=s->mmc5_split_control&31u;
  unsigned screen_col=x>>3;
  unsigned col=(s->mmc5_split_control&0x40)
      ?(screen_col-boundary)&31u:(screen_col+(32u-boundary))&31u;
  unsigned row=sy>>3;
  unsigned tile=s->mmc5_exram[row*32u+col];
  unsigned attr=s->mmc5_exram[960u+(row>>2)*8u+(col>>2)];
  unsigned shift=((row&2u)<<1u)|(col&2u);
  unsigned pal=(attr>>shift)&3u;
  size_t size=s->chr_size?s->chr_size:8192u;
  size_t base=((size_t)s->mmc5_split_bank*4096u+(size_t)tile*16u+(sy&7u))%size;
  unsigned bit=7u-sx;
  unsigned pix=((s->chr[base]>>bit)&1u)|(((s->chr[(base+8u)%size]>>bit)&1u)<<1u);
  if(x<8 && !(s->ppu_mask&2))pix=0;
  if(pix){bg_opaque=1;palette=s->ppu_palette[pal*4u+pix]&63u;}
 }

 if((s->ppu_mask&0x08) && !split){
  unsigned bit=0x8000u>>s->ppu_fine_x;
  unsigned pix=((s->ppu_bg_pattern_low&bit)?1u:0u)|((s->ppu_bg_pattern_high&bit)?2u:0u);
  unsigned attr=((s->ppu_bg_attr_low&bit)?1u:0u)|((s->ppu_bg_attr_high&bit)?2u:0u);
  if(x<8 && !(s->ppu_mask&2))pix=0;
  if(pix){bg_opaque=1;palette=s->ppu_palette[attr*4u+pix]&63u;}
 }
 if((s->ppu_mask&0x10) && (x>=8 || (s->ppu_mask&4))){
  for(unsigned i=0;i<s->active_sprite_count;i++){
   unsigned left=s->sprite_x[i];
   if(x<left || x>=left+8u)continue;
   unsigned col=x-left;
   if(s->sprite_attr[i]&0x40)col=7u-col;
   unsigned bit=7u-col;
   unsigned pix=((s->sprite_low[i]>>bit)&1u)|(((s->sprite_high[i]>>bit)&1u)<<1);
   if(!pix)continue;
   if(s->sprite_indices[i]==0 && bg_opaque && x<255 &&
      (s->ppu_mask&0x18)==0x18 &&
      (x>=8 || ((s->ppu_mask&0x06)==0x06)))s->ppu_status|=0x40;
   if(!(s->sprite_attr[i]&0x20) || !bg_opaque)
    palette=s->ppu_palette[0x10u+(s->sprite_attr[i]&3u)*4u+pix]&63u;
   break;
  }
 }
 s->pixels[y*IN_WIDTH+x]=0xff000000u|nes_rgb[palette];
}
