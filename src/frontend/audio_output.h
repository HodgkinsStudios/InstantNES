#ifndef INSTANTNES_AUDIO_OUTPUT_H
#define INSTANTNES_AUDIO_OUTPUT_H
/* Optional ALSA playback loaded at runtime; never blocks the emulation thread. */
#include <dlfcn.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef struct {
 void *lib,*pcm;
 int (*open)(void**,const char*,int,int);
 int (*params)(void*,int,int,unsigned,unsigned,unsigned,int);
 long (*write)(void*,const void*,unsigned long);
 int (*recover)(void*,int,int);
 int (*close)(void*);
 int16_t pending[8192]; size_t used;
 int active; double dc_prev_in,dc_prev_out;
} INAudio;
static void in_audio_close_device(INAudio*a){if(a->pcm&&a->close)a->close(a->pcm);if(a->lib)dlclose(a->lib);memset(a,0,sizeof(*a));}
static int in_audio_open_device(INAudio*a){
 memset(a,0,sizeof(*a));a->lib=dlopen("libasound.so.2",RTLD_NOW|RTLD_LOCAL);if(!a->lib)return 0;
 *(void**)(&a->open)=dlsym(a->lib,"snd_pcm_open");
 *(void**)(&a->params)=dlsym(a->lib,"snd_pcm_set_params");
 *(void**)(&a->write)=dlsym(a->lib,"snd_pcm_writei");
 *(void**)(&a->recover)=dlsym(a->lib,"snd_pcm_recover");
 *(void**)(&a->close)=dlsym(a->lib,"snd_pcm_close");
 if(!a->open||!a->params||!a->write||!a->recover||!a->close){in_audio_close_device(a);return 0;}
 /* ALSA: playback=0, nonblocking=1; S16_LE=2, interleaved=3, mono=1. */
 if(a->open(&a->pcm,"default",0,1)<0||!a->pcm||a->params(a->pcm,2,3,1,44100,1,80000)<0){in_audio_close_device(a);return 0;}
 a->active=1;return 1;
}
static void in_audio_submit(INAudio*a,in_system*nes){
 int16_t fresh[1024];size_t n;
 while((n=in_audio_read(nes,fresh,1024))!=0){
  if(!a->active)continue;
  if(a->used+n>4096){/* Latency guard: discard oldest audio rather than stall video. */
   size_t drop=a->used+n-4096;memmove(a->pending,a->pending+drop,(a->used-drop)*sizeof(int16_t));a->used-=drop;
  }
  /* Remove the APU mixer's large DC offset and apply bounded gain.
     Keep processing continuous across frames to avoid clicks. */
  for(size_t i=0;i<n;i++){
   double x=(double)fresh[i];
   double y=x-a->dc_prev_in+0.995*a->dc_prev_out;
   a->dc_prev_in=x;a->dc_prev_out=y;
   /* Higher output level with smooth saturation instead of hard clipping. */
   double amplified=y*18.0;
   double v=32700.0*amplified/(12000.0+(amplified<0?-amplified:amplified));
   if(v>30000.0)v=30000.0;if(v< -30000.0)v=-30000.0;
   a->pending[a->used++]=(int16_t)v;
  }
 }
 if(!a->active||!a->used)return;
 long w=a->write(a->pcm,a->pending,(unsigned long)a->used);
 if(w<0){int r=a->recover(a->pcm,(int)w,1);if(r<0){a->used=0;return;}w=0;}
 if(w>0){size_t done=(size_t)w;if(done>a->used)done=a->used;a->used-=done;memmove(a->pending,a->pending+done,a->used*sizeof(int16_t));}
}
#endif
