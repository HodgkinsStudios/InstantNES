#ifndef IN_CPU_INTERNAL_H
#define IN_CPU_INTERNAL_H
#include "instantnes.h"
/* Every elapsed CPU cycle has one bus owner. This is a bookkeeping
   foundation, not yet a resumable 6502 micro-op scheduler. */
typedef enum in_bus_owner {
 IN_BUS_CPU_READ, IN_BUS_CPU_WRITE, IN_BUS_CPU_IDLE,
 IN_BUS_DMC_HALT, IN_BUS_DMC_FETCH, IN_BUS_OAM_HALT,
 IN_BUS_OAM_READ, IN_BUS_OAM_WRITE, IN_BUS_OWNER_COUNT
} in_bus_owner;
struct in_system {
 uint8_t ram[2048],prg_ram[8192],prg[524288],chr[524288];
 size_t prg_size,chr_size;
 uint16_t mapper; uint8_t submapper,nes2,mirroring,has_rom,chr_ram;
 uint8_t mmc2_prg,mmc2_chr_fd0,mmc2_chr_fe0,mmc2_chr_fd1,mmc2_chr_fe1,mmc2_latch0,mmc2_latch1;
 uint8_t mmc5_prg_ram[131072],mmc5_chr_upper,mmc5_chr_fetch_sprite;
 uint16_t mmc5_chr_regs[12];
 uint8_t mmc5_prg_mode,mmc5_chr_mode,mmc5_prg_regs[5],mmc5_nt_map,mmc5_fill_tile,mmc5_fill_attr,mmc5_exram_mode,mmc5_exram[1024];
 uint8_t mmc5_extended_tile,mmc5_extended_active;
 uint8_t mmc5_split_control,mmc5_split_scroll,mmc5_split_bank;
 uint8_t mmc5_nt_fetch_count;
 uint8_t mmc5_ram_protect1,mmc5_ram_protect2,mmc5_irq_target,mmc5_irq_enabled,mmc5_irq_pending,mmc5_in_frame,mmc5_mul_a,mmc5_mul_b;
 uint8_t mmc5_audio_regs[8],mmc5_audio_enable,mmc5_pcm_control,mmc5_pcm_output,mmc5_pcm_irq;
 uint8_t mmc5_audio_length[2],mmc5_audio_phase[2],mmc5_audio_env_div[2],mmc5_audio_env_decay[2],mmc5_audio_env_start[2];
 uint16_t mmc5_audio_period[2],mmc5_audio_timer[2];
 uint8_t mmc3_revision;
 uint8_t bank_select,bank_regs[8],mmc3_irq_latch,mmc3_irq_counter,mmc3_irq_reload,mmc3_irq_enable,mmc3_irq_pending;
 uint8_t mmc1_shift,mmc1_control,mmc1_chr0,mmc1_chr1,mmc1_prg,cnrom_bank;
 uint8_t prg_ram_enable,prg_ram_protect;
 uint8_t simple_prg_bank,simple_chr_bank,mapper34_nina,mapper71_mirror;
 uint64_t mmc1_last_write_cycle;uint8_t mmc1_last_write_valid;
 uint64_t ppu_a12_low_ticks;uint8_t ppu_a12_last;
 uint8_t cpu_open_bus;
 uint8_t cpu_bus_clock_active; uint32_t cpu_bus_clock_count; uint64_t master_cpu_cycles;
 in_bus_owner last_bus_owner; uint64_t bus_owner_cycles[IN_BUS_OWNER_COUNT];
 in_cpu_state cpu;in_input input;uint8_t nmi_pending,irq_line;
 uint8_t apu_regs[20],apu_length[4],apu_enable,apu_frame_irq,apu_dmc_irq,apu_irq,apu_frame_mode,apu_irq_inhibit,apu_frame_reset_delay,apu_frame_pending_mode;
 uint8_t apu_triangle_linear,apu_triangle_reload,apu_triangle_reload_flag,apu_dmc_bits,apu_dmc_output;
 uint16_t apu_dmc_bytes,apu_dmc_timer;uint32_t apu_frame_cycle;
 uint16_t apu_timer[4],apu_period[4],apu_lfsr,apu_dmc_address,apu_dmc_start;
 uint8_t apu_phase[4],apu_env_div[3],apu_env_decay[3],apu_env_start[3],apu_sweep_div[2],apu_sweep_reload[2];
 uint8_t apu_dmc_sample,apu_dmc_sample_full,apu_dmc_shift,apu_dmc_silence;
 uint8_t apu_muted;uint8_t apu_cycle_parity;uint32_t apu_sample_accum;int16_t apu_audio[8192];uint16_t apu_audio_read,apu_audio_write;
 uint8_t dma_pending,dma_page,dmc_dma_pending,dmc_dma_busy;uint32_t dma_extra_cycles;uint8_t controller_strobe;uint16_t controller_shift[2];
 uint8_t ppu_ctrl,ppu_mask,ppu_status,ppu_oam_addr,ppu_oam[256];
 uint8_t ppu_vblank_suppressed;
 uint64_t ppu_bus_bit_last_driven[8];
 uint8_t ppu_open_bus,ppu_next_nt,ppu_next_at,ppu_next_low,ppu_next_high;
 uint16_t ppu_bg_pattern_low,ppu_bg_pattern_high,ppu_bg_attr_low,ppu_bg_attr_high;
 uint8_t secondary_oam[32],sprite_indices[8],sprite_low[8],sprite_high[8],sprite_attr[8],sprite_x[8];
 uint8_t eval_oam_index,eval_count,active_sprite_count;
 uint8_t next_sprite_indices[8],next_sprite_low[8],next_sprite_high[8],next_sprite_attr[8],next_sprite_x[8],next_sprite_count;
 uint8_t ppu_vram[4096],ppu_palette[32],ppu_read_buffer,ppu_latch,ppu_fine_x;
 uint16_t ppu_v,ppu_t;uint16_t ppu_dot,ppu_scanline;uint8_t ppu_odd_frame;
 uint32_t pixels[IN_WIDTH*IN_HEIGHT];uint8_t ppu_scroll_x,ppu_scroll_y;uint64_t frames;
};
void in_clock_cpu_cycle(in_system *s);
void in_clock_owned_cycle(in_system *s,in_bus_owner owner);
void in_cpu_clock_finish(in_system *s,uint32_t cycles);
void in_apu_reset(in_system *s);
void in_dmc_dma_complete(in_system *s,uint8_t sample);
void in_apu_tick(in_system *s,uint32_t cycles);
void in_mmc5_audio_tick(in_system *s);
int in_mmc5_audio_mix(in_system *s);
uint8_t in_apu_read(in_system *s,uint16_t a);
void in_apu_write(in_system *s,uint16_t a,uint8_t v);
uint8_t in_ppu_register_read(in_system *s,uint16_t a);
void in_ppu_register_write(in_system *s,uint16_t a,uint8_t v);
void in_ppu_tick(in_system *s,uint32_t cpu_cycles);
uint8_t in_bus_read(in_system *s,uint16_t address);
void in_bus_write(in_system *s,uint16_t address,uint8_t value);
in_result in_cpu_step(in_system *s,uint32_t *cycles);
uint8_t in_mapper_prg_read(in_system *s,uint16_t a);
void in_mapper_write(in_system *s,uint16_t a,uint8_t v);
uint8_t in_mapper_chr_read(in_system *s,uint16_t a);
void in_mapper_chr_write(in_system *s,uint16_t a,uint8_t v);
void in_mapper_ppu_address(in_system *s,uint16_t a);
uint8_t in_mmc5_read(in_system *s,uint16_t a);
void in_mmc5_write(in_system *s,uint16_t a,uint8_t v);
void in_mmc5_prg_write(in_system *s,uint16_t a,uint8_t v);
void in_mapper_ppu_address_source(in_system *s,uint16_t a,unsigned source);
#endif
