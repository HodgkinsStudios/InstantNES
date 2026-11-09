#include "cpu_internal.h"
#include <string.h>
/* NTSC 2A03 APU. Clocked in CPU cycles; no platform audio dependency.
   DMC bus arbitration and frame IRQ edge timing remain approximate. */
static const uint8_t lengths[32]={10,254,20,2,40,4,80,6,160,8,60,10,14,12,26,14,12,16,24,18,48,20,96,22,192,24,72,26,16,28,32,30};
static const uint16_t noise_periods[16]={4,8,16,32,64,96,128,160,202,254,380,508,762,1016,2034,4068};
static const uint16_t dmc_periods[16]={428,380,340,320,286,254,226,214,190,160,142,128,106,85,72,54};
static const uint8_t duty[4]={0x01,0x03,0x0f,0xfc};
static const uint8_t tri[32]={15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
static void irq_update(in_system*s){s->apu_irq=(uint8_t)(s->apu_frame_irq||s->apu_dmc_irq);}
static unsigned env(in_system*s,unsigned c){unsigned r=c==2?12:c*4;return (s->apu_regs[r]&0x10)?(s->apu_regs[r]&15):s->apu_env_decay[c];}
static void quarter(in_system*s){unsigned c;for(c=0;c<3;c++){unsigned r=c==2?12:c*4;uint8_t period=s->apu_regs[r]&15;if(s->apu_env_start[c]){s->apu_env_start[c]=0;s->apu_env_decay[c]=15;s->apu_env_div[c]=period;}else if(s->apu_env_div[c])s->apu_env_div[c]--;else{s->apu_env_div[c]=period;if(s->apu_env_decay[c])s->apu_env_decay[c]--;else if(s->apu_regs[r]&0x20)s->apu_env_decay[c]=15;}}if(s->apu_triangle_reload_flag)s->apu_triangle_linear=s->apu_triangle_reload;else if(s->apu_triangle_linear)s->apu_triangle_linear--;if(!(s->apu_regs[8]&0x80))s->apu_triangle_reload_flag=0;}
static uint16_t target(in_system*s,unsigned c){unsigned shift=s->apu_regs[c*4+1]&7;uint16_t delta=s->apu_period[c]>>shift;return (uint16_t)((s->apu_regs[c*4+1]&8)?(s->apu_period[c]-delta-(c==0)):(s->apu_period[c]+delta));}
static void half(in_system*s){unsigned c;for(c=0;c<4;c++){unsigned r=c==3?12:c*4;if(s->apu_length[c]&&!(s->apu_regs[r]&0x20))s->apu_length[c]--;}for(c=0;c<2;c++){uint8_t v=s->apu_regs[c*4+1];if(!s->apu_sweep_div[c]&&(v&0x80)&&(v&7)&&s->apu_period[c]>=8&&target(s,c)<=0x7ff)s->apu_period[c]=target(s,c);if(!s->apu_sweep_div[c]||s->apu_sweep_reload[c])s->apu_sweep_div[c]=(v>>4)&7;else s->apu_sweep_div[c]--;s->apu_sweep_reload[c]=0;}}
void in_apu_reset(in_system*s){memset(s->apu_regs,0,sizeof(s->apu_regs));memset(s->apu_length,0,sizeof(s->apu_length));memset(s->apu_timer,0,sizeof(s->apu_timer));memset(s->apu_period,0,sizeof(s->apu_period));memset(s->apu_phase,0,sizeof(s->apu_phase));memset(s->apu_env_div,0,sizeof(s->apu_env_div));memset(s->apu_env_decay,0,sizeof(s->apu_env_decay));memset(s->apu_env_start,0,sizeof(s->apu_env_start));memset(s->apu_sweep_div,0,sizeof(s->apu_sweep_div));memset(s->apu_sweep_reload,0,sizeof(s->apu_sweep_reload));s->apu_enable=s->apu_frame_irq=s->apu_dmc_irq=s->apu_irq=s->apu_frame_mode=s->apu_irq_inhibit=s->apu_frame_reset_delay=0;s->apu_frame_pending_mode=0;s->apu_frame_cycle=0;s->apu_triangle_linear=s->apu_triangle_reload=s->apu_triangle_reload_flag=0;s->apu_dmc_bytes=s->apu_dmc_bits=s->apu_dmc_output=s->apu_dmc_timer=0;s->apu_dmc_address=s->apu_dmc_start=0xc000;s->apu_dmc_sample_full=0;s->apu_dmc_silence=1;s->apu_dmc_shift=0;s->apu_lfsr=1;s->apu_cycle_parity=0;s->apu_sample_accum=0;s->apu_audio_read=s->apu_audio_write=0;}
uint8_t in_apu_read(in_system*s,uint16_t a){uint8_t r=0;unsigned i;if(a!=0x4015)return 0;for(i=0;i<4;i++)if(s->apu_length[i])r|=(uint8_t)(1u<<i);if(s->apu_dmc_bytes)r|=0x10;if(s->apu_frame_irq)r|=0x40;if(s->apu_dmc_irq)r|=0x80;s->apu_frame_irq=0;irq_update(s);return r;}
static void dmc_restart(in_system*s){s->apu_dmc_address=s->apu_dmc_start;s->apu_dmc_bytes=(uint16_t)((uint16_t)s->apu_regs[0x13]*16+1);}
void in_apu_write(in_system*s,uint16_t a,uint8_t v){unsigned i;if(a>=0x4000&&a<=0x4013){unsigned r=a-0x4000;s->apu_regs[r]=v;if(r==8)s->apu_triangle_reload=v&0x7f;if(r==2||r==6||r==10)s->apu_period[r/4]=(uint16_t)((s->apu_period[r/4]&0x700)|v);if(r==3||r==7||r==11){unsigned c=r/4;s->apu_period[c]=(uint16_t)((s->apu_period[c]&255)|((v&7)<<8));if(s->apu_enable&(1u<<c))s->apu_length[c]=lengths[v>>3];if(c<2){s->apu_phase[c]=0;s->apu_env_start[c]=1;}else s->apu_triangle_reload_flag=1;}if(r==15){if(s->apu_enable&8)s->apu_length[3]=lengths[v>>3];s->apu_env_start[2]=1;}if(r==1||r==5)s->apu_sweep_reload[r/4]=1;if(r==0x11)s->apu_dmc_output=v&0x7f;if(r==0x12)s->apu_dmc_start=(uint16_t)(0xc000+((uint16_t)v<<6));if(r==0x10&&!(v&0x80)){s->apu_dmc_irq=0;irq_update(s);}return;}if(a==0x4015){s->apu_enable=v&31;for(i=0;i<4;i++)if(!(v&(1u<<i)))s->apu_length[i]=0;if(!(v&16)){s->apu_dmc_bytes=0;s->dmc_dma_pending=0;}else if(!s->apu_dmc_bytes)dmc_restart(s);s->apu_dmc_irq=0;irq_update(s);return;}if(a==0x4017){s->apu_frame_pending_mode=(v>>7)&1;s->apu_irq_inhibit=(v>>6)&1;if(s->apu_irq_inhibit)s->apu_frame_irq=0;irq_update(s);s->apu_frame_reset_delay=(uint8_t)((s->cpu.cycles&1)?3:4);}}
/* Request a bus transaction; never read the CPU bus recursively from APU tick. */
static void dmc_fetch(in_system*s){
 if(!s->apu_dmc_sample_full && s->apu_dmc_bytes && !s->dmc_dma_busy)
  s->dmc_dma_pending=1;
}
void in_dmc_dma_complete(in_system*s,uint8_t sample){
 s->apu_dmc_sample=sample;s->apu_dmc_sample_full=1;
 s->apu_dmc_address=(s->apu_dmc_address==0xffff)?0x8000:(uint16_t)(s->apu_dmc_address+1);
 if(s->apu_dmc_bytes)--s->apu_dmc_bytes;
 if(!s->apu_dmc_bytes){
  if(s->apu_regs[0x10]&0x40)dmc_restart(s);
  else if(s->apu_regs[0x10]&0x80){s->apu_dmc_irq=1;irq_update(s);}
 }
 s->dmc_dma_pending=0;
}
/* MMC5 pulse envelopes and length counters share the APU frame cadence.
   The MMC5 pulse channels do not implement hardware sweep. */
static void mmc5_quarter(in_system*s){
 if(s->mapper!=5)return;
 for(unsigned c=0;c<2;c++){
  unsigned r=c*4,period=s->mmc5_audio_regs[r]&15;
  if(s->mmc5_audio_env_start[c]){s->mmc5_audio_env_start[c]=0;s->mmc5_audio_env_decay[c]=15;s->mmc5_audio_env_div[c]=(uint8_t)period;}
  else if(s->mmc5_audio_env_div[c])s->mmc5_audio_env_div[c]--;
  else{s->mmc5_audio_env_div[c]=(uint8_t)period;if(s->mmc5_audio_env_decay[c])s->mmc5_audio_env_decay[c]--;else if(s->mmc5_audio_regs[r]&0x20)s->mmc5_audio_env_decay[c]=15;}
 }
}
static void mmc5_half(in_system*s){
 if(s->mapper!=5)return;
 for(unsigned c=0;c<2;c++)if(s->mmc5_audio_length[c]&&!(s->mmc5_audio_regs[c*4]&0x20))s->mmc5_audio_length[c]--;
}
void in_mmc5_audio_tick(in_system*s){
 if(s->mapper!=5)return;
 if(!s->apu_cycle_parity)for(unsigned c=0;c<2;c++){
  if(s->mmc5_audio_timer[c])s->mmc5_audio_timer[c]--;
  else{s->mmc5_audio_timer[c]=s->mmc5_audio_period[c];s->mmc5_audio_phase[c]=(uint8_t)((s->mmc5_audio_phase[c]+1)&7);}
 }
}
int in_mmc5_audio_mix(in_system*s){
 if(s->mapper!=5)return 0;
 unsigned sum=0;
 for(unsigned c=0;c<2;c++){
  unsigned r=c*4;
  if(!(s->mmc5_audio_enable&(1u<<c))||!s->mmc5_audio_length[c]||s->mmc5_audio_period[c]<8)continue;
  if(duty[s->mmc5_audio_regs[r]>>6]>>(7-s->mmc5_audio_phase[c])&1)
   sum+=(s->mmc5_audio_regs[r]&0x10)?(s->mmc5_audio_regs[r]&15):s->mmc5_audio_env_decay[c];
 }
 /* A modest linear expansion-channel contribution in the existing PCM scale. */
 return (int)(sum*90u+s->mmc5_pcm_output*18u);
}
static void output(in_system*s){if(s->apu_muted)return;unsigned p[2]={0,0},t=0,n=0,c;for(c=0;c<2;c++){uint8_t sw=s->apu_regs[c*4+1];if((s->apu_enable&(1u<<c))&&s->apu_length[c]&&s->apu_period[c]>=8&&(!(sw&0x80)||!(sw&7)||target(s,c)<=0x7ff)&&(duty[s->apu_regs[c*4]>>6]>>(7-s->apu_phase[c])&1))p[c]=env(s,c);}if((s->apu_enable&4)&&s->apu_length[2]&&s->apu_triangle_linear)t=tri[s->apu_phase[2]];if((s->apu_enable&8)&&s->apu_length[3]&&!(s->apu_lfsr&1))n=env(s,2);/* integer approximation of the NES nonlinear DAC */unsigned pulse=p[0]+p[1];unsigned mix=(pulse? (9588u*pulse*256)/(812800u+10000u*pulse):0);unsigned tnd=3*t+2*n+s->apu_dmc_output;mix+=(tnd?(15979u*tnd*256)/(2263800u+10000u*tnd):0);int32_t pcm=(int32_t)mix*10-16000+in_mmc5_audio_mix(s);if(pcm>32767)pcm=32767;if(pcm< -32768)pcm=-32768;uint16_t next=(uint16_t)((s->apu_audio_write+1)&8191);if(next!=s->apu_audio_read){s->apu_audio[s->apu_audio_write]=(int16_t)pcm;s->apu_audio_write=next;}}
void in_apu_tick(in_system*s,uint32_t cycles){while(cycles--){uint32_t fc;unsigned c;if(s->apu_frame_reset_delay&&!--s->apu_frame_reset_delay){s->apu_frame_cycle=0;s->apu_frame_mode=s->apu_frame_pending_mode;if(s->apu_frame_mode){quarter(s);mmc5_quarter(s);half(s);mmc5_half(s);}}fc=++s->apu_frame_cycle;if(s->apu_frame_mode){if(fc==7457||fc==14913||fc==22371||fc==37281){quarter(s);mmc5_quarter(s);}if(fc==14913||fc==37281){half(s);mmc5_half(s);}if(fc>=37282)s->apu_frame_cycle=0;}else{if(fc==7457||fc==14913||fc==22371||fc==29829){quarter(s);mmc5_quarter(s);}if(fc==14913||fc==29829){half(s);mmc5_half(s);}if(fc>=29829&&!s->apu_irq_inhibit){s->apu_frame_irq=1;irq_update(s);}if(fc>=29830)s->apu_frame_cycle=0;}if(s->apu_timer[2])s->apu_timer[2]--;else{s->apu_timer[2]=s->apu_period[2];if(s->apu_length[2]&&s->apu_triangle_linear&&s->apu_period[2]>=2)s->apu_phase[2]=(uint8_t)((s->apu_phase[2]+1)&31);}s->apu_cycle_parity^=1;if(!s->apu_cycle_parity){for(c=0;c<2;c++){if(s->apu_timer[c])s->apu_timer[c]--;else{s->apu_timer[c]=s->apu_period[c];s->apu_phase[c]=(uint8_t)((s->apu_phase[c]+1)&7);}}if(s->apu_timer[3])s->apu_timer[3]--;else{s->apu_timer[3]=noise_periods[s->apu_regs[14]&15];unsigned tap=(s->apu_regs[14]&0x80)?6:1;s->apu_lfsr=(uint16_t)((s->apu_lfsr>>1)|(((s->apu_lfsr^(s->apu_lfsr>>tap))&1)<<14));}if(s->apu_dmc_timer)s->apu_dmc_timer--;else{s->apu_dmc_timer=(uint16_t)(dmc_periods[s->apu_regs[0x10]&15]/2-1);if(!s->apu_dmc_bits){s->apu_dmc_bits=8;if(s->apu_dmc_sample_full){s->apu_dmc_shift=s->apu_dmc_sample;s->apu_dmc_sample_full=0;s->apu_dmc_silence=0;}else s->apu_dmc_silence=1;}if(!s->apu_dmc_silence){if(s->apu_dmc_shift&1){if(s->apu_dmc_output<=125)s->apu_dmc_output+=2;}else if(s->apu_dmc_output>=2)s->apu_dmc_output-=2;}s->apu_dmc_shift>>=1;s->apu_dmc_bits--;}}in_mmc5_audio_tick(s);dmc_fetch(s);s->apu_sample_accum+=44100;if(s->apu_sample_accum>=1789773){s->apu_sample_accum-=1789773;output(s);}}}
