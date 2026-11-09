#include "cpu_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Portable, platform-free core. Mapper 0 is the initial cartridge baseline.
   CPU execution, PPU, APU, DMA, controllers and timing are explicit extension points.
   This scaffold intentionally does NOT claim to emulate games yet. */
static uint16_t read16(in_system *s,uint16_t a);
/* The CPU, APU and PPU share one CPU-cycle clock domain. Bus accesses
   advance the peripherals at the access, not after the entire instruction. */
void in_clock_owned_cycle(in_system *s,in_bus_owner owner){
    s->last_bus_owner=owner;
    ++s->bus_owner_cycles[owner];
    in_ppu_tick(s,1);
    in_apu_tick(s,1);
    ++s->master_cpu_cycles;
}
void in_clock_cpu_cycle(in_system *s){in_clock_owned_cycle(s,IN_BUS_CPU_IDLE);}
void in_cpu_clock_finish(in_system *s,uint32_t cycles){
    while(s->cpu_bus_clock_count<cycles){
        in_clock_owned_cycle(s,IN_BUS_CPU_IDLE);
        ++s->cpu_bus_clock_count;
    }
    s->cpu_bus_clock_active=0;
}
/* A DMC transfer is scheduled outside the APU tick to avoid recursive bus
   clocks. The four-cycle read stall is an approximation until CPU micro-ops
   expose read/write eligibility and exact DMA arbitration phases. */
static void service_dmc_dma(in_system *s){
    unsigned i;uint8_t sample;
    if(!s->dmc_dma_pending || s->dmc_dma_busy || !s->apu_dmc_bytes)return;
#ifdef IN_DMA_TRACE
    { static unsigned count=0;
      if(count++<120)fprintf(stderr,"DMC_DMA start cycle=%llu pc=%04x addr=%04x cpu_bus=%u oam_pending=%u\n",(unsigned long long)s->master_cpu_cycles,s->cpu.pc,s->apu_dmc_address,s->cpu_bus_clock_active,s->dma_pending);
    }
#endif
    s->dmc_dma_busy=1;
    for(i=0;i<3;i++){in_clock_owned_cycle(s,IN_BUS_DMC_HALT);s->dma_extra_cycles++;}
    /* DMA owns this bus read: do not recursively charge a CPU access. */
    { uint8_t was_active=s->cpu_bus_clock_active;
      s->cpu_bus_clock_active=0;
      sample=in_bus_read(s,s->apu_dmc_address);
      s->cpu_bus_clock_active=was_active; }
    in_clock_owned_cycle(s,IN_BUS_DMC_FETCH);s->dma_extra_cycles++;
    in_dmc_dma_complete(s,sample);
    s->dmc_dma_busy=0;
}
static void clock_bus_access(in_system *s,int is_read){
    if(s->cpu_bus_clock_active){
        /* A DMC halt is permitted at CPU read boundaries, not writes. */
        if(is_read)service_dmc_dma(s);
        in_clock_owned_cycle(s,is_read?IN_BUS_CPU_READ:IN_BUS_CPU_WRITE);
        ++s->cpu_bus_clock_count;
    }
}
static uint8_t in_bus_read_impl(in_system *s,uint16_t a) {
    uint8_t value;
    clock_bus_access(s,1);
    if (a<0x2000) return s->cpu_open_bus=s->ram[a&0x7ff];
    if(a>=0x2000 && a<0x4000) return s->cpu_open_bus=in_ppu_register_read(s,a);
    /* $4015 is sourced inside the 2A03: its read value does not
       overwrite the external CPU data-bus latch. */
    if(a==0x4015)return (uint8_t)((in_apu_read(s,a)&0xdf)|(s->cpu_open_bus&0x20));
    if(a==0x4016 || a==0x4017) {
        unsigned port=a&1;
        /* While strobe is high, read the live A button. Otherwise shift
           eight latched bits LSB-first, followed by ones. */
        if(s->controller_strobe)return s->cpu_open_bus=(uint8_t)((s->cpu_open_bus&0xe0)|(s->input.buttons[port]&1));
        uint8_t bit=(uint8_t)(s->controller_shift[port]&1);
        s->controller_shift[port]=(uint16_t)((s->controller_shift[port]>>1)|0x80);
        return s->cpu_open_bus=(uint8_t)((s->cpu_open_bus&0xe0)|bit);
    }
    if(s->mapper==5 && a>=0x5000 && a<=0x5fff)return s->cpu_open_bus=in_mmc5_read(s,a);
    if (s->mapper==5 && a>=0x6000 && a<0x8000) return s->cpu_open_bus=s->mmc5_prg_ram[((size_t)(s->mmc5_prg_regs[0]&15)*8192+(a&8191))%sizeof(s->mmc5_prg_ram)];
    if (a>=0x6000 && a<0x8000) return s->cpu_open_bus=((s->mapper!=4 || s->prg_ram_enable) && (s->mapper!=1 || !(s->mmc1_prg&0x10)))?s->prg_ram[a-0x6000]:0;
    if (a>=0x8000 && s->prg_size) {
        uint8_t value=in_mapper_prg_read(s,a);
        if(s->mapper==5 && a<0xc000 && (s->mmc5_pcm_control&1)){
            unsigned slot=(a-0x8000)/8192, mode=s->mmc5_prg_mode;
            unsigned reg=(mode==3)?slot+1:(mode==2)?(slot<2?2:3):(mode==1)?(slot<2?2:4):4;
            if(reg==4 || (s->mmc5_prg_regs[reg]&0x80)){
                if(value)s->mmc5_pcm_output=value;
                else if(s->mmc5_pcm_control&0x80){s->mmc5_pcm_irq=1;s->irq_line=1;}
            }
        }
        return s->cpu_open_bus=value;
    }
    return s->cpu_open_bus; /* undriven address retains CPU data bus */
}
static uint16_t read16(in_system *s,uint16_t a) {
    return (uint16_t)(in_bus_read(s,a)|((uint16_t)in_bus_read(s,(uint16_t)(a+1))<<8));
}
static void in_bus_write_impl(in_system *s,uint16_t a,uint8_t value) {
#ifdef IN_AC_TRACE
 static unsigned ac_writes=0;
 if(a>=0x2000 && a<0x4000 && ((a&7)==0 || (a&7)==1) && ac_writes++<300) fprintf(stderr,"AC_PPU_WRITE cycle=%llu addr=%04x val=%02x pc=%04x scan=%u dot=%u\n",(unsigned long long)s->master_cpu_cycles,a,value,s->cpu.pc,s->ppu_scanline,s->ppu_dot);
#endif
    clock_bus_access(s,0);
    s->cpu_open_bus=value;
    if(a<0x2000)s->ram[a&0x7ff]=value;
    if(s->mapper==5 && a>=0x5000 && a<=0x5fff)in_mmc5_write(s,a,value);
    if(a>=0x6000 && a<0x8000){
        if(s->mapper==34 && s->mapper34_nina && a>=0x7ffd){
            if(a==0x7ffd)s->simple_prg_bank=value&1;
            if(a==0x7ffe)s->mmc1_chr0=value;
            if(a==0x7fff)s->mmc1_chr1=value;
        }else if(s->mapper==87){
            s->simple_chr_bank=((value&1u)<<1)|((value&2u)>>1);
        }else if(s->mapper==140){
            in_mapper_write(s,a,value);
        }else if(s->mapper==5){
            in_mmc5_prg_write(s,a,value);
        }else if(
                 (s->mapper!=4 || (s->prg_ram_enable && !s->prg_ram_protect)) &&
                 (s->mapper!=1 || !(s->mmc1_prg&0x10)))s->prg_ram[a-0x6000]=value;
    }
    if(a>=0x8000){if(s->mapper==5)in_mmc5_prg_write(s,a,value);else in_mapper_write(s,a,value);}
    if(a>=0x2000 && a<0x4000)in_ppu_register_write(s,a,value);
    if((a>=0x4000&&a<=0x4013)||a==0x4015||a==0x4017)in_apu_write(s,a,value);
    if(a==0x4014){s->dma_pending=1;s->dma_page=value;}
    if(a==0x4016){
        uint8_t previous=s->controller_strobe;
        s->controller_strobe=value&1;
        if(previous && !s->controller_strobe){
            s->controller_shift[0]=s->input.buttons[0];
            s->controller_shift[1]=s->input.buttons[1];
        }
    }
    /* PPU/APU/IO and mapper writes are reserved for later passes. */
}
in_system *in_create(void) { return (in_system*)calloc(1,sizeof(in_system)); }
void in_destroy(in_system *s) { free(s); }
void in_unload(in_system *s) { if(s) { memset(s,0,sizeof(*s)); } }
in_result in_load_ines(in_system *s,const uint8_t *data,size_t n) {
    size_t off,ps,cs; unsigned mapper;
    if(!s||!data) return IN_ERR_ARGUMENT;
    if(n<16||memcmp(data,"NES\032",4)!=0) return IN_ERR_FORMAT;
    unsigned nes2=((data[7]&0x0c)==0x08),sub=0;
    unsigned prg_units=data[4],chr_units=data[5];
    mapper=(data[6]>>4)|(data[7]&0xf0);
    if(nes2){
        mapper|=(unsigned)(data[8]&15)<<8;
        sub=data[8]>>4;
        /* NES 2.0 linear and exponent/multiplier ROM sizes. */
        if((data[9]&15)==15){
            unsigned exponent=data[4]>>2,mult=(data[4]&3)*2+1;
            if(exponent>=sizeof(size_t)*8-2)return IN_ERR_UNSUPPORTED;
            ps=((size_t)1<<exponent)*mult;
        }else ps=((size_t)((data[9]&15)<<8)|prg_units)*16384;
        if((data[9]>>4)==15){
            unsigned exponent=data[5]>>2,mult=(data[5]&3)*2+1;
            if(exponent>=sizeof(size_t)*8-2)return IN_ERR_UNSUPPORTED;
            cs=((size_t)1<<exponent)*mult;
        }else cs=((size_t)((data[9]>>4)<<8)|chr_units)*8192;
        /* This core currently emulates NTSC timing only. */
        if((data[12]&3)==1)return IN_ERR_UNSUPPORTED;
    }else{ps=(size_t)prg_units*16384;cs=(size_t)chr_units*8192;}
    if(mapper!=0 && mapper!=1 && mapper!=2 && mapper!=3 && mapper!=4 && mapper!=5 && mapper!=7 && mapper!=9 && mapper!=11 && mapper!=34 && mapper!=66 && mapper!=71 && mapper!=10 && mapper!=87 && mapper!=140 && mapper!=180)return IN_ERR_UNSUPPORTED;
    if(ps==0 || ps>sizeof(s->prg) || cs>sizeof(s->chr))return IN_ERR_UNSUPPORTED;
    /* Discrete mapper windows require complete physical PRG/CHR banks.
       Reject malformed NES 2.0 sizes rather than indexing padded ROM data. */
    if((mapper==2 || mapper==3 || mapper==11 || mapper==66) &&
       ((ps % (mapper==11 || mapper==66 ? 32768u : 16384u)) != 0 ||
        (cs && cs % 8192u != 0)))return IN_ERR_UNSUPPORTED;
    /* NROM is fixed 16/32 KiB PRG with a single 8 KiB CHR region.
       AxROM selects complete 32 KiB PRG banks and uses 8 KiB CHR RAM.
       Never accept partial banks that would read uninitialized padding. */
    if(mapper==0 && !((ps==16384u || ps==32768u) && (cs==0 || cs==8192u)))
        return IN_ERR_UNSUPPORTED;
    if(mapper==7 && (ps%32768u != 0 || cs != 0))
        return IN_ERR_UNSUPPORTED;
    /* BNROM/NINA-001 use complete 32 KiB PRG windows; NINA-001
       additionally selects independent 4 KiB CHR-ROM windows.
       Camerica mapper 71 uses complete 16 KiB PRG windows. */
    if(mapper==34 && (ps%32768u || (cs && cs%4096u)))
        return IN_ERR_UNSUPPORTED;
    if(mapper==71 && (ps%16384u || (cs && cs%8192u)))
        return IN_ERR_UNSUPPORTED;
    if((mapper==10 || mapper==180) && ps%16384u)return IN_ERR_UNSUPPORTED;
    if(mapper==140 && (ps%32768u || (cs && cs%8192u)))return IN_ERR_UNSUPPORTED;
    if(mapper==87 && (ps!=16384u && ps!=32768u))return IN_ERR_UNSUPPORTED;
    if(mapper==87 && cs%8192u)return IN_ERR_UNSUPPORTED;
    if(mapper==5 && ps<32768)return IN_ERR_UNSUPPORTED;
    /* MMC4 uses complete 16 KiB PRG and 4 KiB CHR-ROM banks. */
    if(mapper==10 && (cs%4096u != 0))return IN_ERR_UNSUPPORTED;
    if((mapper==9 || mapper==10) && (ps<32768 || cs==0))return IN_ERR_UNSUPPORTED;
    /* Explicitly reject unsupported NES 2.0 expansion devices and unsupported
       board variants instead of silently misidentifying them. */
    if(nes2 && (data[7]&3))return IN_ERR_UNSUPPORTED;
    if(nes2 && sub && (mapper==9 || mapper==10 || mapper==87 || mapper==140 || mapper==180))return IN_ERR_UNSUPPORTED;
    off=16+((data[6]&0x04)?512u:0u);
    if(off>n||ps>n-off||cs>n-off-ps) return IN_ERR_FORMAT;
    in_unload(s);
    memcpy(s->prg,data+off,ps);
    if(cs) memcpy(s->chr,data+off+ps,cs);
    s->prg_size=ps;s->chr_size=cs;s->chr_ram=(cs==0);
    s->mirroring=(data[6]&8)?6:(data[6]&1);
    /* iNES mapper 34: CHR-ROM boards use NINA-001; CHR-RAM boards use BNROM. */
    s->mapper34_nina=(mapper==34 && cs!=0);
    s->mapper71_mirror=(mapper==71);
    s->mapper=(uint16_t)mapper;s->submapper=(uint8_t)sub;s->nes2=(uint8_t)nes2;s->has_rom=1;
    return in_reset(s);
}
in_result in_load_file(in_system *s,const char *path) {
    FILE *f;long len;uint8_t *buf;size_t got;in_result r;
    if(!s||!path) return IN_ERR_ARGUMENT;
    f=fopen(path,"rb");if(!f) return IN_ERR_IO;
    if(fseek(f,0,SEEK_END)!=0||(len=ftell(f))<0||len>8*1024*1024||fseek(f,0,SEEK_SET)!=0) {fclose(f);return IN_ERR_IO;}
    buf=(uint8_t*)malloc((size_t)len? (size_t)len:1);
    if(!buf){fclose(f);return IN_ERR_MEMORY;}
    got=fread(buf,1,(size_t)len,f);fclose(f);
    if(got!=(size_t)len){free(buf);return IN_ERR_IO;}
    r=in_load_ines(s,buf,got);free(buf);return r;
}
in_result in_reset(in_system *s) {
    if(!s) return IN_ERR_ARGUMENT;
    if(!s->has_rom) return IN_ERR_FORMAT;
    memset(s->ram,0,sizeof(s->ram));memset(s->prg_ram,0,sizeof(s->prg_ram));memset(&s->cpu,0,sizeof(s->cpu));
    s->cpu.sp=0xfd;s->cpu.p=0x24;s->master_cpu_cycles=0;s->cpu_open_bus=0;in_apu_reset(s);
    /* Initialize mapper banking before reading the CPU reset vector. */
    s->nmi_pending=0;s->irq_line=0;s->dma_pending=0;s->dma_page=0;s->dmc_dma_pending=s->dmc_dma_busy=0;s->dma_extra_cycles=0;s->controller_strobe=0;
    s->controller_shift[0]=s->controller_shift[1]=0xff;
    s->bank_select=0;memset(s->bank_regs,0,sizeof(s->bank_regs));
    s->mmc3_irq_latch=s->mmc3_irq_counter=s->mmc3_irq_reload=s->mmc3_irq_enable=s->mmc3_irq_pending=0;
    s->ppu_a12_low_ticks=8;s->ppu_a12_last=0;s->prg_ram_enable=1;s->prg_ram_protect=0;
    s->simple_prg_bank=s->simple_chr_bank=0;
    s->mmc5_prg_mode=3;s->mmc5_chr_mode=3;memset(s->mmc5_prg_regs,0,sizeof(s->mmc5_prg_regs));s->mmc5_prg_regs[4]=0xff;
    memset(s->mmc5_chr_regs,0,sizeof(s->mmc5_chr_regs));memset(s->mmc5_exram,0,sizeof(s->mmc5_exram));
    s->mmc5_nt_map=s->mmc5_fill_tile=s->mmc5_fill_attr=s->mmc5_exram_mode=0;
    s->mmc5_ram_protect1=s->mmc5_ram_protect2=s->mmc5_irq_target=s->mmc5_irq_enabled=s->mmc5_irq_pending=s->mmc5_in_frame=0;
    s->mmc5_mul_a=s->mmc5_mul_b=s->mmc5_chr_upper=0;
    s->mmc5_split_control=s->mmc5_split_scroll=s->mmc5_split_bank=0;
    memset(s->mmc5_audio_regs,0,sizeof(s->mmc5_audio_regs));
    memset(s->mmc5_audio_length,0,sizeof(s->mmc5_audio_length));
    memset(s->mmc5_audio_phase,0,sizeof(s->mmc5_audio_phase));
    memset(s->mmc5_audio_timer,0,sizeof(s->mmc5_audio_timer));
    memset(s->mmc5_audio_period,0,sizeof(s->mmc5_audio_period));
    memset(s->mmc5_audio_env_div,0,sizeof(s->mmc5_audio_env_div));
    memset(s->mmc5_audio_env_decay,0,sizeof(s->mmc5_audio_env_decay));
    memset(s->mmc5_audio_env_start,0,sizeof(s->mmc5_audio_env_start));
    s->mmc5_audio_enable=s->mmc5_pcm_control=s->mmc5_pcm_output=s->mmc5_pcm_irq=0;
    memset(s->mmc5_prg_ram,0,sizeof(s->mmc5_prg_ram));
    s->mmc2_prg=s->mmc2_chr_fd0=s->mmc2_chr_fe0=s->mmc2_chr_fd1=s->mmc2_chr_fe1=0;
    s->mmc2_latch0=s->mmc2_latch1=1;
    s->mmc1_last_write_valid=0;s->mmc1_last_write_cycle=0;
    s->mmc1_shift=0x10;s->mmc1_control=0x0c;s->mmc1_chr0=s->mmc1_chr1=s->mmc1_prg=0;s->cnrom_bank=0;
    s->cpu.pc=read16(s,0xfffc);
    s->frames=0;s->ppu_ctrl=s->ppu_mask=s->ppu_status=0;s->ppu_v=s->ppu_t=0;
    s->ppu_dot=s->ppu_scanline=0;s->ppu_odd_frame=0;s->ppu_latch=0;
    s->ppu_oam_addr=0;s->ppu_read_buffer=0;
    s->ppu_open_bus=s->ppu_next_nt=s->ppu_next_at=s->ppu_next_low=s->ppu_next_high=0;
    s->ppu_bg_pattern_low=s->ppu_bg_pattern_high=s->ppu_bg_attr_low=s->ppu_bg_attr_high=0;s->ppu_fine_x=0;s->ppu_scroll_x=s->ppu_scroll_y=0;
    memset(s->ppu_vram,0,sizeof(s->ppu_vram));memset(s->ppu_palette,0,sizeof(s->ppu_palette));
    s->ppu_vblank_suppressed=0;
    memset(s->ppu_oam,0,sizeof(s->ppu_oam));memset(s->pixels,0,sizeof(s->pixels));return IN_OK;
}
void in_request_nmi(in_system *s){if(s)s->nmi_pending=1;}
void in_set_irq_line(in_system *s,int asserted){if(s)s->irq_line=asserted!=0;}
in_result in_step_cpu(in_system *s,uint32_t *cycles) {
    uint32_t used=0;
    in_result r;
    if(!s)return IN_ERR_ARGUMENT;
    s->cpu_bus_clock_count=0;
    s->dma_extra_cycles=0;
    s->cpu_bus_clock_active=1;
    r=in_cpu_step(s,&used);
    if(r==IN_OK){
        in_cpu_clock_finish(s,used);
        if(s->dma_pending){
#ifdef IN_DMA_TRACE
            {static unsigned count=0;if(count++<120)fprintf(stderr,"OAM_DMA start cycle=%llu pc=%04x page=%02x dmc_pending=%u\n",(unsigned long long)s->master_cpu_cycles,s->cpu.pc,s->dma_page,s->dmc_dma_pending);}
#endif
            uint16_t base=(uint16_t)s->dma_page<<8;
            unsigned i;
            uint32_t stall=513u+(uint32_t)(s->master_cpu_cycles&1u);
            s->dma_pending=0;
            /* Halt/alignment phase followed by alternating read/write cycles.
               Keep peripherals running on every stolen CPU cycle. */
            in_clock_owned_cycle(s,IN_BUS_OAM_HALT);
            if(stall==514)in_clock_owned_cycle(s,IN_BUS_OAM_HALT);
            for(i=0;i<256;i++){
                uint8_t value;
                service_dmc_dma(s);
                /* OAM DMA reads use the CPU bus without an extra implicit tick. */
                { uint8_t was_active=s->cpu_bus_clock_active;
                  s->cpu_bus_clock_active=0;
                  value=in_bus_read(s,(uint16_t)(base+i));
                  s->cpu_bus_clock_active=was_active; }
                in_clock_owned_cycle(s,IN_BUS_OAM_READ);
                s->ppu_oam[s->ppu_oam_addr++]=value;
                in_clock_owned_cycle(s,IN_BUS_OAM_WRITE);
            }
            s->cpu.cycles+=stall;
            used+=stall;
        }
    }else s->cpu_bus_clock_active=0;
    if(r==IN_OK){s->cpu.cycles+=s->dma_extra_cycles;used+=s->dma_extra_cycles;}
    if(cycles)*cycles=used;
    return r;
}
in_result in_run_frame(in_system *s) {
    if(!s) return IN_ERR_ARGUMENT;
    if(!s->has_rom)return IN_ERR_FORMAT;
    /* Bounded execution avoids a hung UI on malformed or unsupported code. */
    uint64_t target=s->frames+1;
    unsigned steps=0;
    while(s->frames<target && steps++<40000){
        in_result r=in_step_cpu(s,NULL);
        if(r!=IN_OK)return r;
    }
    return s->frames>=target?IN_OK:IN_ERR_UNSUPPORTED;
}
const uint32_t *in_framebuffer(const in_system *s) {return s?s->pixels:NULL;}
void in_set_input(in_system *s,const in_input *in){if(s&&in)s->input=*in;}
int in_has_rom(const in_system *s){return s&&s->has_rom;}
in_cpu_state in_get_cpu_state(const in_system *s){in_cpu_state z={0};return s?s->cpu:z;}
const char *in_result_string(in_result r){switch(r){case IN_OK:return "OK";case IN_ERR_ARGUMENT:return "Invalid argument";case IN_ERR_MEMORY:return "Out of memory";case IN_ERR_FORMAT:return "Invalid or unloaded ROM";case IN_ERR_UNSUPPORTED:return "Not implemented or unsupported cartridge";case IN_ERR_IO:return "File I/O error";default:return "Unknown error";}}

uint8_t in_debug_peek_prg_ram(const in_system *s,uint16_t address){return s && address>=0x6000 && address<0x8000 ? s->prg_ram[address-0x6000] : 0;}

size_t in_audio_read(in_system *s,int16_t *out,size_t capacity){size_t n=0;if(!s||!out)return 0;while(n<capacity&&s->apu_audio_read!=s->apu_audio_write){out[n++]=s->apu_audio[s->apu_audio_read];s->apu_audio_read=(uint16_t)((s->apu_audio_read+1)&8191);}return n;}

in_result in_set_mmc3_revision(in_system *s,in_mmc3_revision revision){
 if(!s || revision<IN_MMC3_REV_AUTO || revision>IN_MMC3_REV_A)return IN_ERR_ARGUMENT;
 s->mmc3_revision=(uint8_t)revision;return IN_OK;
}
in_mmc3_revision in_get_mmc3_revision(const in_system *s){
 return s?(in_mmc3_revision)s->mmc3_revision:IN_MMC3_REV_AUTO;
}

/* Diagnostic bus-event recorder. Sequence numbers are access order, NOT
   certified 2A03 microcycle indices. Disabled in production builds. */
#ifdef IN_BUS_EVENT_TRACE
static unsigned long in_bus_event_seq;
static unsigned in_bus_event_count;
static void in_bus_event(in_system *s,char kind,uint16_t addr,uint8_t value){
    unsigned long seq=++in_bus_event_seq;
    if(s->cpu.pc>=0xaaf0 && s->cpu.pc<0xab20 && in_bus_event_count<600){
        ++in_bus_event_count;
        fprintf(stderr,"BUS_EVENT seq=%lu master=%llu pc=%04x kind=%c addr=%04x value=%02x active=%u dmc=%u oam=%u\n",
          seq,(unsigned long long)s->master_cpu_cycles,s->cpu.pc,kind,addr,value,
          s->cpu_bus_clock_active,s->dmc_dma_busy,s->dma_pending);
    }
}
#endif
uint8_t in_bus_read(in_system *s,uint16_t a){
    uint8_t value=in_bus_read_impl(s,a);
#ifdef IN_BUS_EVENT_TRACE
    in_bus_event(s,'R',a,value);
#endif
    return value;
}
void in_bus_write(in_system *s,uint16_t a,uint8_t value){
#ifdef IN_BUS_EVENT_TRACE
    in_bus_event(s,'W',a,value);
#endif
    in_bus_write_impl(s,a,value);
}

void in_set_mute(in_system *s,int muted){if(!s)return;s->apu_muted=muted!=0;if(s->apu_muted)s->apu_audio_read=s->apu_audio_write;}
int in_get_mute(const in_system *s){return s?s->apu_muted:0;}

/* Versioned snapshot: complete machine state, including PPU pipeline, APU,
   DMA, mapper, timing and frame buffer. Never restore a partial snapshot. */
typedef struct { char magic[8]; uint32_t version, size; uint64_t rom_hash, state_hash; } StateHeader;
static uint64_t state_hash(const void *ptr,size_t n){
 const unsigned char *b=ptr;uint64_t h=UINT64_C(14695981039346656037);
 while(n--){h^=*b++;h*=UINT64_C(1099511628211);}return h;
}
static uint64_t rom_identity(const in_system *s){
 uint64_t h=state_hash(s->prg,s->prg_size);
 h^=state_hash(s->chr,s->chr_size);h*=UINT64_C(1099511628211);
 h^=(uint64_t)s->mapper<<32 | (uint64_t)s->submapper<<24 | s->mirroring;
 h^=(uint64_t)s->prg_size<<1 ^ (uint64_t)s->chr_size<<3;return h;
}
in_result in_save_state(in_system *s,const char *path){
 if(!s||!path||!*path)return IN_ERR_ARGUMENT;
 if(!s->has_rom)return IN_ERR_FORMAT;
 size_t n=strlen(path);char *tmp=malloc(n+5);if(!tmp)return IN_ERR_MEMORY;
 memcpy(tmp,path,n);memcpy(tmp+n,".tmp",5);
 StateHeader header={{'I','N','S','T','S','A','V','E'},1,sizeof(*s),rom_identity(s),state_hash(s,sizeof(*s))};
 FILE *f=fopen(tmp,"wb");if(!f){free(tmp);return IN_ERR_IO;}
 int ok=fwrite(&header,1,sizeof(header),f)==sizeof(header)&&fwrite(s,1,sizeof(*s),f)==sizeof(*s);
 if(fflush(f)!=0)ok=0;
 if(fclose(f)!=0)ok=0;
 if(ok&&rename(tmp,path)!=0)ok=0;
 if(!ok)remove(tmp);free(tmp);return ok?IN_OK:IN_ERR_IO;
}
in_result in_load_state(in_system *s,const char *path){
 if(!s||!path||!*path)return IN_ERR_ARGUMENT;
 if(!s->has_rom)return IN_ERR_FORMAT;
 FILE *f=fopen(path,"rb");if(!f)return IN_ERR_IO;
 StateHeader header;in_system *snapshot=malloc(sizeof(*s));
 if(!snapshot){fclose(f);return IN_ERR_MEMORY;}
 int ok=fread(&header,1,sizeof(header),f)==sizeof(header);
 if(ok)ok=memcmp(header.magic,"INSTSAVE",8)==0&&header.version==1&&header.size==sizeof(*s)&&header.rom_hash==rom_identity(s);
 if(ok)ok=fread(snapshot,1,sizeof(*s),f)==sizeof(*s)&&fgetc(f)==EOF;
 if(fclose(f)!=0)ok=0;
 if(ok)ok=header.state_hash==state_hash(snapshot,sizeof(*snapshot))&&snapshot->has_rom&&snapshot->prg_size==s->prg_size&&snapshot->chr_size==s->chr_size&&rom_identity(snapshot)==header.rom_hash;
 if(ok){uint8_t mute=s->apu_muted;in_input input=s->input;memcpy(s,snapshot,sizeof(*s));s->apu_muted=mute;s->input=input;}
 free(snapshot);return ok?IN_OK:IN_ERR_FORMAT;
}
