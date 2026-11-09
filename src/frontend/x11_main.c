#include <X11/Xatom.h>
#include "instantnes_icon.h"
#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <linux/joystick.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <strings.h>
#include "instantnes.h"
#include "audio_output.h"
#include <dlfcn.h>
#include <sys/ipc.h>
#include <sys/shm.h>
/* Runtime-loaded MIT-SHM: builds on systems without libXext development headers.
   The layout matches XShmSegmentInfo from X11/extensions/XShm.h. */
typedef struct { XID shmseg; int shmid; char *shmaddr; Bool readOnly; } INShmInfo;
typedef Bool (*INShmQuery)(Display *);
typedef XImage *(*INShmCreate)(Display *, Visual *, unsigned int, int, char *, INShmInfo *, unsigned int, unsigned int);
typedef Status (*INShmAttach)(Display *, INShmInfo *);
typedef Status (*INShmDetach)(Display *, INShmInfo *);
typedef Status (*INShmPut)(Display *, Drawable, GC, XImage *, int, int, int, int, unsigned int, unsigned int, Bool);


/* Optional OpenGL 1.x/GLX presentation loaded at runtime. The emulator core
   remains independent of GL; software X11 is the automatic fallback. */
typedef void *INGLContext;
typedef struct {void *lib; INGLContext ctx; unsigned tex; int ready;
 int (*getconfig)(Display*,XVisualInfo*,int,int*);
 INGLContext (*create)(Display*,XVisualInfo*,INGLContext,int);
 int (*make)(Display*,unsigned long,INGLContext);
 void (*swap)(Display*,unsigned long);
 void *(*getproc)(const unsigned char*);
 const char *(*query_extensions)(Display*,int);
 void (*swap_interval_ext)(Display*,unsigned long,int);
 int (*swap_interval_mesa)(unsigned);
 int (*swap_interval_sgi)(int);
 void (*destroy)(Display*,INGLContext);
 void (*viewport)(int,int,int,int);void (*matrixmode)(unsigned);void (*loadidentity)(void);
 void (*ortho)(double,double,double,double,double,double);
 void (*clearcolor)(float,float,float,float);void (*clear)(unsigned);
 void (*gentex)(int,unsigned*);void (*bindtex)(unsigned,unsigned);
 void (*texparam)(unsigned,unsigned,int);void (*teximage)(unsigned,int,int,int,int,int,unsigned,unsigned,const void*);
 void (*texsub)(unsigned,int,int,int,int,int,unsigned,unsigned,const void*);
 void (*enable)(unsigned);void (*begin)(unsigned);void (*end)(void);
 void (*texcoord)(float,float);void (*vertex)(float,float);
 void (*deletetex)(int,const unsigned*);
 void (*blendfunc)(unsigned,unsigned);void (*disable)(unsigned); unsigned ui_tex;
} INGL;
#define IN_GL_TEXTURE_2D 0x0DE1
#define IN_GL_RGBA 0x1908
#define IN_GL_BGRA 0x80E1
#define IN_GL_UNSIGNED_BYTE 0x1401
#define IN_GL_COLOR_BUFFER_BIT 0x4000
#define IN_GL_PROJECTION 0x1701
#define IN_GL_MODELVIEW 0x1700
#define IN_GL_QUADS 7
#define IN_GL_TEXTURE_MIN_FILTER 0x2801
#define IN_GL_TEXTURE_MAG_FILTER 0x2800
#define IN_GL_NEAREST 0x2600
#define IN_GL_UNPACK_ALIGNMENT 0x0CF5
#define IN_GL_BLEND 0x0BE2
#define IN_GL_SRC_ALPHA 0x0302
#define IN_GL_ONE_MINUS_SRC_ALPHA 0x0303
#define IN_GLX_RGBA 4
#define IN_GLX_DOUBLEBUFFER 5
enum { A,B,SELECT,START,UP,DOWN,LEFT,RIGHT,NBTN };
static const char *names[NBTN]={"A","B","Select","Start","Up","Down","Left","Right"};
static const KeySym defaults[NBTN]={XK_x,XK_z,XK_Shift_L,XK_Return,XK_Up,XK_Down,XK_Left,XK_Right};
static const int pad_defaults[NBTN]={0,1,6,7,-2,-3,-4,-5};
typedef struct {Display*d;Window w,about,input_win;Pixmap back;GC gc;XFontStruct*ui_font;Atom wm_delete;in_system*nes;in_input input;KeySym keys[NBTN];int pads[NBTN];unsigned char key_state,key_down[256],pad_buttons[256];short axes[32];int menu_hover,pad_fd,pad_idx,pad_active,editor,capture,pcapture,selected,selected_pad,armed,menu,screen_mode,save_slot,slot_menu,win_w,win_h;char status[256];struct timespec status_expires;unsigned long black,white,gray;XImage*img;unsigned char*image_data;uint32_t*scaled_pixels;size_t scaled_capacity;uint16_t *x_lookup;int x_lookup_width;uint16_t *y_lookup;int y_lookup_height;void *xext;INShmCreate shm_create;INShmAttach shm_attach;INShmDetach shm_detach;INShmPut shm_put;XImage *shm_image;INShmInfo shm_info;int shm_ready,shm_pending;INGL gl;} App;
static void gl_shutdown(App *a){
 INGL*g=&a->gl;if(g->ready){g->deletetex(1,&g->tex);g->deletetex(1,&g->ui_tex);g->make(a->d,None,NULL);g->destroy(a->d,g->ctx);}
 if(g->lib)dlclose(g->lib);memset(g,0,sizeof(*g));
}
static void gl_init(App *a){
 INGL*g=&a->gl;const char*enable=getenv("INSTANTNES_EXPERIMENTAL_GL");
 /* GLX swaps the drawable before X11 draws menus. Mixed presentation is
    prone to flashing and regressions: opt in only until UI is unified. */
 if(!enable||strcmp(enable,"1"))return;
 const char*disable=getenv("INSTANTNES_SOFTWARE_RENDERER");
 if(disable&&strcmp(disable,"0"))return;
 g->lib=dlopen("libGL.so.1",RTLD_NOW|RTLD_LOCAL);if(!g->lib)return;
#define IN_GL_LOAD(field,symbol) do{*(void**)(&g->field)=dlsym(g->lib,symbol);if(!g->field)goto fail;}while(0)
 IN_GL_LOAD(getconfig,"glXGetConfig");IN_GL_LOAD(create,"glXCreateContext");
 IN_GL_LOAD(make,"glXMakeCurrent");IN_GL_LOAD(swap,"glXSwapBuffers");
 IN_GL_LOAD(destroy,"glXDestroyContext");
 *(void**)(&g->getproc)=dlsym(g->lib,"glXGetProcAddressARB");
 if(!g->getproc)*(void**)(&g->getproc)=dlsym(g->lib,"glXGetProcAddress");
 *(void**)(&g->query_extensions)=dlsym(g->lib,"glXQueryExtensionsString");IN_GL_LOAD(viewport,"glViewport");
 IN_GL_LOAD(matrixmode,"glMatrixMode");IN_GL_LOAD(loadidentity,"glLoadIdentity");
 IN_GL_LOAD(ortho,"glOrtho");IN_GL_LOAD(clearcolor,"glClearColor");IN_GL_LOAD(clear,"glClear");
 IN_GL_LOAD(gentex,"glGenTextures");IN_GL_LOAD(bindtex,"glBindTexture");
 IN_GL_LOAD(texparam,"glTexParameteri");IN_GL_LOAD(teximage,"glTexImage2D");
 IN_GL_LOAD(texsub,"glTexSubImage2D");IN_GL_LOAD(enable,"glEnable");
 IN_GL_LOAD(begin,"glBegin");IN_GL_LOAD(end,"glEnd");
 IN_GL_LOAD(texcoord,"glTexCoord2f");IN_GL_LOAD(vertex,"glVertex2f");
 IN_GL_LOAD(deletetex,"glDeleteTextures");
 IN_GL_LOAD(blendfunc,"glBlendFunc");IN_GL_LOAD(disable,"glDisable");
 XVisualInfo templ={0};templ.visualid=XVisualIDFromVisual(DefaultVisual(a->d,DefaultScreen(a->d)));
 int count=0;XVisualInfo*vi=XGetVisualInfo(a->d,VisualIDMask,&templ,&count);
 if(!vi||!count){if(vi)XFree(vi);goto fail;}
 int rgba=0,db=0;int ok=!g->getconfig(a->d,vi,IN_GLX_RGBA,&rgba)&&rgba&&!g->getconfig(a->d,vi,IN_GLX_DOUBLEBUFFER,&db)&&db;
 if(ok)g->ctx=g->create(a->d,vi,NULL,True);XFree(vi);
 if(!g->ctx||!g->make(a->d,a->w,g->ctx))goto fail;
 g->gentex(1,&g->tex);g->bindtex(IN_GL_TEXTURE_2D,g->tex);
 g->texparam(IN_GL_TEXTURE_2D,IN_GL_TEXTURE_MIN_FILTER,IN_GL_NEAREST);
 g->texparam(IN_GL_TEXTURE_2D,IN_GL_TEXTURE_MAG_FILTER,IN_GL_NEAREST);
 g->teximage(IN_GL_TEXTURE_2D,0,IN_GL_RGBA,IN_WIDTH,IN_HEIGHT,0,IN_GL_BGRA,IN_GL_UNSIGNED_BYTE,NULL);
 g->gentex(1,&g->ui_tex);g->bindtex(IN_GL_TEXTURE_2D,g->ui_tex);
 g->texparam(IN_GL_TEXTURE_2D,IN_GL_TEXTURE_MIN_FILTER,IN_GL_NEAREST);
 g->texparam(IN_GL_TEXTURE_2D,IN_GL_TEXTURE_MAG_FILTER,IN_GL_NEAREST);
 g->enable(IN_GL_TEXTURE_2D);g->clearcolor(0,0,0,1);g->ready=1;
 /* The emulator's monotonic clock is the frame-rate master. Explicitly
    disable GL swap blocking when supported, avoiding double throttling on
    60 Hz (and worse, 120/144 Hz) desktops. Optional override for testing. */
 if(g->getproc && g->query_extensions){
  const char *extensions=g->query_extensions(a->d,DefaultScreen(a->d));
  if(!extensions)extensions="";
  *(void**)(&g->swap_interval_ext)=g->getproc((const unsigned char*)"glXSwapIntervalEXT");
  *(void**)(&g->swap_interval_mesa)=g->getproc((const unsigned char*)"glXSwapIntervalMESA");
  *(void**)(&g->swap_interval_sgi)=g->getproc((const unsigned char*)"glXSwapIntervalSGI");
  if(!strstr(extensions,"GLX_EXT_swap_control"))g->swap_interval_ext=NULL;
  if(!strstr(extensions,"GLX_MESA_swap_control"))g->swap_interval_mesa=NULL;
  if(!strstr(extensions,"GLX_SGI_swap_control"))g->swap_interval_sgi=NULL;
  const char*vs=getenv("INSTANTNES_GL_VSYNC");int interval=(vs&&strcmp(vs,"0"))?1:0;
  if(g->swap_interval_ext)g->swap_interval_ext(a->d,a->w,interval);
  else if(g->swap_interval_mesa)g->swap_interval_mesa((unsigned)interval);
  else if(interval&&g->swap_interval_sgi)g->swap_interval_sgi(1);
  fprintf(stderr,"InstantNES: GL swap interval requested %d\n",interval);
 }
 fprintf(stderr,"InstantNES: GPU presentation enabled (GLX)\n");return;
fail: gl_shutdown(a);fprintf(stderr,"InstantNES: GLX unavailable; using X11 software presentation\n");
#undef IN_GL_LOAD
}
static void gl_present(App*a,const uint32_t*fb,int left,int top,int dw,int dh){
 INGL*g=&a->gl;g->viewport(0,0,a->win_w,a->win_h);
 g->matrixmode(IN_GL_PROJECTION);g->loadidentity();g->ortho(0,a->win_w,a->win_h,0,-1,1);
 g->matrixmode(IN_GL_MODELVIEW);g->loadidentity();g->clear(IN_GL_COLOR_BUFFER_BIT);
 g->bindtex(IN_GL_TEXTURE_2D,g->tex);
 g->texsub(IN_GL_TEXTURE_2D,0,0,0,IN_WIDTH,IN_HEIGHT,IN_GL_BGRA,IN_GL_UNSIGNED_BYTE,fb);
 g->begin(IN_GL_QUADS);
 g->texcoord(0,0);g->vertex((float)left,(float)top);
 g->texcoord(1,0);g->vertex((float)(left+dw),(float)top);
 g->texcoord(1,1);g->vertex((float)(left+dw),(float)(top+dh));
 g->texcoord(0,1);g->vertex((float)left,(float)(top+dh));g->end();
 /* The buffer swap happens after the UI overlay is drawn. */
}
static void shm_release(App *a){
 if(a->shm_ready){XSync(a->d,False);a->shm_pending=0;a->shm_detach(a->d,&a->shm_info);XSync(a->d,False);a->shm_ready=0;}
 if(a->shm_image){a->shm_image->data=NULL;XDestroyImage(a->shm_image);a->shm_image=NULL;}
 if(a->shm_info.shmaddr && a->shm_info.shmaddr!=(char*)-1)shmdt(a->shm_info.shmaddr);
 a->shm_info.shmaddr=NULL;
}
static void shm_init(App *a){
 a->xext=dlopen("libXext.so.6",RTLD_LAZY|RTLD_LOCAL);
 if(!a->xext)return;
 INShmQuery query=(INShmQuery)dlsym(a->xext,"XShmQueryExtension");
 a->shm_create=(INShmCreate)dlsym(a->xext,"XShmCreateImage");
 a->shm_attach=(INShmAttach)dlsym(a->xext,"XShmAttach");
 a->shm_detach=(INShmDetach)dlsym(a->xext,"XShmDetach");
 a->shm_put=(INShmPut)dlsym(a->xext,"XShmPutImage");
 if(!query||!a->shm_create||!a->shm_attach||!a->shm_detach||!a->shm_put||!query(a->d)){
  dlclose(a->xext);a->xext=NULL;
 }
}
static int shm_ensure(App *a,int width,int height){
 if(!a->xext)return 0;
 if(a->shm_ready && a->shm_image->width==width && a->shm_image->height==height)return 1;
 shm_release(a);
 a->shm_info.shmid=-1;
 a->shm_image=a->shm_create(a->d,DefaultVisual(a->d,DefaultScreen(a->d)),
   (unsigned)DefaultDepth(a->d,DefaultScreen(a->d)),ZPixmap,NULL,&a->shm_info,(unsigned)width,(unsigned)height);
 if(!a->shm_image)return 0;
 size_t bytes=(size_t)a->shm_image->bytes_per_line*(size_t)a->shm_image->height;
 a->shm_info.shmid=shmget(IPC_PRIVATE,bytes,IPC_CREAT|0600);
 if(a->shm_info.shmid<0){shm_release(a);return 0;}
 a->shm_info.shmaddr=shmat(a->shm_info.shmid,NULL,0);
 if(a->shm_info.shmaddr==(char*)-1){a->shm_info.shmaddr=NULL;shmctl(a->shm_info.shmid,IPC_RMID,NULL);shm_release(a);return 0;}
 a->shm_image->data=a->shm_info.shmaddr;
 a->shm_info.readOnly=False;
 /* XShmAttach can raise an X error on unusual remote servers. QueryExtension
    excludes ordinary remote X servers; the non-SHM path remains available. */
 if(!a->shm_attach(a->d,&a->shm_info)){
  shmctl(a->shm_info.shmid,IPC_RMID,NULL);shm_release(a);return 0;
 }
 shmctl(a->shm_info.shmid,IPC_RMID,NULL);
 a->shm_ready=1;
 return 1;
}
static char config[1024];
static void config_init(void){const char*home=getenv("XDG_DATA_HOME");char tmp[768];if(home&&*home)snprintf(tmp,sizeof(tmp),"%s",home);else snprintf(tmp,sizeof(tmp),"%s/.local/share",getenv("HOME")?getenv("HOME"):".");snprintf(config,sizeof(config),"%s/InstantNES/input.ini",tmp);}

static char rom_path[4096];
static int state_path(char *out,size_t cap,int slot){
 const char *home=getenv("HOME"),*xdg=getenv("XDG_DATA_HOME");
 if(!rom_path[0]||slot<0||slot>4)return -1;
 char dir[4096];int n;
 if(xdg&&*xdg)n=snprintf(dir,sizeof(dir),"%s/HodgkinsStudios/InstantNES/SaveStates",xdg);
 else if(home&&*home)n=snprintf(dir,sizeof(dir),"%s/.local/share/HodgkinsStudios/InstantNES/SaveStates",home);
 else return -1;
 if(n<0||(size_t)n>=sizeof(dir))return -1;
 char partial[4096];snprintf(partial,sizeof(partial),"%s",dir);
 for(char *q=partial+1;*q;q++)if(*q=='/'){*q=0;if(mkdir(partial,0700)&&errno!=EEXIST)return -1;*q='/';}
 if(mkdir(partial,0700)&&errno!=EEXIST)return -1;
 const char *name=strrchr(rom_path,'/');name=name?name+1:rom_path;
 char base[256];size_t j=0;while(name[j]&&j<sizeof(base)-1){base[j]=name[j];j++;}base[j]=0;
 char *dot=strrchr(base,'.');if(dot&&(!strcasecmp(dot,".nes"))) *dot=0;
 for(size_t i=0;base[i];i++)if(base[i]=='/'||base[i]=='\\'||base[i]<32)base[i]='_';
 n=snprintf(out,cap,"%s/%s_%d.SAV",dir,base,slot+1);
 return n>=0&&(size_t)n<cap?0:-1;
}
static void defaults_set(App*a){memcpy(a->keys,defaults,sizeof(defaults));memcpy(a->pads,pad_defaults,sizeof(pad_defaults));}
static void config_load(App*a){FILE*f=fopen(config,"r");char line[128],name[40],section[40]="";long val;int i;if(!f)return;while(fgets(line,sizeof(line),f)){if(sscanf(line,"[%39[^]]]",section)==1)continue;if(sscanf(line,"%39[^=]=%ld",name,&val)==2){for(i=0;i<NBTN;i++)if(!strcmp(name,names[i])){if(!strcmp(section,"Player1")&&val>=0)a->keys[i]=(KeySym)val;else if(!strcmp(section,"Gamepad1")&&val>=-163&&val<256)a->pads[i]=(int)val;}}}fclose(f);}
static int config_save(App*a){char dir[1024];char*p;FILE*f;snprintf(dir,sizeof(dir),"%s",config);p=strrchr(dir,'/');if(!p)return -1;*p=0;for(p=dir+1;*p;p++)if(*p=='/'){*p=0;mkdir(dir,0700);*p='/';}mkdir(dir,0700);f=fopen(config,"w");if(!f)return -1;fprintf(f,"[Player1]\n");for(int i=0;i<NBTN;i++)fprintf(f,"%s=%lu\n",names[i],(unsigned long)a->keys[i]);fprintf(f,"\n[Gamepad1]\n");for(int i=0;i<NBTN;i++)fprintf(f,"%s=%d\n",names[i],a->pads[i]);return fclose(f);}
static unsigned char pad_mask(App*a){unsigned char m=0;if(a->pad_fd<0)return 0;for(int i=0;i<NBTN;i++){int v=a->pads[i],on=0;if(v>=0&&v<256)on=a->pad_buttons[v];else if(v<=-100&&v>=-163){int x=-v-100,ax=x/2;if(ax<32)on=(x%2)?a->axes[ax]>16000:a->axes[ax]<-16000;}else if(v<=-2&&v>=-5){int ax=(v==-2||v==-3)?1:0;on=(v==-2||v==-4)?a->axes[ax]<-16000:a->axes[ax]>16000;}if(on)m|=1u<<i;}return m;}
static void key_recompute(App*a){a->key_state=0;for(int i=0;i<NBTN;i++){KeyCode code=XKeysymToKeycode(a->d,a->keys[i]);if(code&&a->key_down[code])a->key_state|=1u<<i;}}
static void input_update(App*a){a->input.buttons[0]=a->editor?0:(a->pad_active&&a->pad_fd>=0?pad_mask(a):a->key_state);in_set_input(a->nes,&a->input);}
static void pad_close(App*a){if(a->pad_fd>=0)close(a->pad_fd);a->pad_fd=-1;a->pad_idx=-1;a->pad_active=0;memset(a->pad_buttons,0,sizeof(a->pad_buttons));memset(a->axes,0,sizeof(a->axes));input_update(a);}
static void pad_scan(App*a){if(a->pad_fd>=0)return;for(int i=0;i<16;i++){char path[64];snprintf(path,sizeof(path),"/dev/input/js%d",i);int fd=open(path,O_RDONLY|O_NONBLOCK);if(fd>=0){a->pad_fd=fd;a->pad_idx=i;memset(a->pad_buttons,0,sizeof(a->pad_buttons));memset(a->axes,0,sizeof(a->axes));break;}}}
static int pad_neutral(App*a){for(int i=0;i<256;i++)if(a->pad_buttons[i])return 0;for(int i=0;i<32;i++)if(a->axes[i]>12000||a->axes[i]<-12000)return 0;return 1;}
static void pad_poll(App*a){pad_scan(a);if(a->pad_fd<0)return;struct js_event ev;ssize_t n;while((n=read(a->pad_fd,&ev,sizeof(ev)))==(ssize_t)sizeof(ev)){int v=-999;unsigned t=ev.type&~JS_EVENT_INIT;int init=ev.type&JS_EVENT_INIT;if(t==JS_EVENT_BUTTON&&ev.number<256){int rising=ev.value&&!a->pad_buttons[ev.number];a->pad_buttons[ev.number]=ev.value!=0;if(rising&&!init){v=ev.number;}}else if(t==JS_EVENT_AXIS&&ev.number<32){short old=a->axes[ev.number];a->axes[ev.number]=ev.value;if(!init&&((ev.value>20000&&old<=12000)||(ev.value< -20000&&old>=-12000))){v=-100-ev.number*2-(ev.value>0);}}if(!a->editor&&v!=-999){for(int i=0;i<NBTN;i++)if(a->pads[i]==v)a->pad_active=1;}if(a->editor&&a->pcapture>=0&&a->armed&&v!=-999){int idx=a->pcapture,old=a->pads[idx];for(int j=0;j<NBTN;j++)if(j!=idx&&a->pads[j]==v)a->pads[j]=old;a->pads[idx]=v;a->pcapture=-1;a->armed=0;}}if(a->editor&&a->pcapture>=0&&!a->armed&&pad_neutral(a))a->armed=1;if(n==0||(n<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK))pad_close(a);input_update(a);}
static void about_paint(App*a){
 if(!a->about)return;
 XSetForeground(a->d,a->gc,a->black);XFillRectangle(a->d,a->about,a->gc,0,0,510,355);
 for(int y=0;y<64;y++)for(int x=0;x<64;x++){
  uint32_t c=in_icon_argb[y*64+x];if((c>>24)<128)continue;
  XSetForeground(a->d,a->gc,((c>>16)&255)<<16|((c>>8)&255)<<8|(c&255));
  XFillRectangle(a->d,a->about,a->gc,159+x*3,12+y*3,3,3);
 }
 XSetForeground(a->d,a->gc,a->white);
 const char*lines[]={"InstantNES","HodgkinsStudios","100% written with ChatGPT 6 Instant"};
 for(int i=0;i<3;i++)XDrawString(a->d,a->about,a->gc,45,237+i*35,lines[i],(int)strlen(lines[i]));
}
static void about_show(App*a){
 if(a->about){XRaiseWindow(a->d,a->about);return;}
 int scr=DefaultScreen(a->d);
 a->about=XCreateSimpleWindow(a->d,RootWindow(a->d,scr),120,120,510,355,1,a->white,a->black);
 XStoreName(a->d,a->about,"About InstantNES");
 XSetWMProtocols(a->d,a->about,&a->wm_delete,1);
 XSelectInput(a->d,a->about,ExposureMask|KeyPressMask|StructureNotifyMask);
 XMapRaised(a->d,a->about);about_paint(a);
}
static Drawable paint_target(App*a){return a->back;}
static void text(App*a,int x,int y,const char*s){XSetForeground(a->d,a->gc,a->white);XDrawString(a->d,paint_target(a),a->gc,x,y,s,(int)strlen(s));}
static void rect(App*a,int x,int y,int w,int h,unsigned long color){XSetForeground(a->d,a->gc,color);XFillRectangle(a->d,paint_target(a),a->gc,x,y,w,h);}
static void input_text(App*a,int x,int y,const char*s){XSetForeground(a->d,a->gc,a->white);XDrawString(a->d,a->input_win,a->gc,x,y,s,(int)strlen(s));}
static void input_rect(App*a,int x,int y,int w,int h,unsigned long color){XSetForeground(a->d,a->gc,color);XFillRectangle(a->d,a->input_win,a->gc,x,y,(unsigned)w,(unsigned)h);}
static void input_paint(App*a){
 if(!a->input_win)return;
 input_rect(a,0,0,820,610,a->black);
 input_text(a,20,35,"Player 1 Input (emulation paused)");
 input_text(a,20,68,"NES Button");input_text(a,220,68,"Keyboard");input_text(a,505,68,"Gamepad");
 for(int i=0;i<NBTN;i++){
  char pad[64];int v=a->pads[i];
  if(v==-1)snprintf(pad,sizeof(pad),"Unassigned");
  else if(v>=0)snprintf(pad,sizeof(pad),"Button %d",v);
  else if(v<=-100)snprintf(pad,sizeof(pad),"Axis %d %c",(-v-100)/2,(-v-100)%2?'+':'-');
  else snprintf(pad,sizeof(pad),"Axis %d %c",(v==-2||v==-3)?1:0,(v==-3||v==-5)?'+':'-');
  const char*k=XKeysymToString(a->keys[i]);
  input_text(a,20,100+i*36,names[i]);
  input_text(a,220,100+i*36,a->capture==i?"Press key...":(k?k:"Unassigned"));
  input_text(a,505,100+i*36,a->pcapture==i?"Move gamepad...":pad);
 }
 const int bx[]={15,200,335,480},bw[]={175,120,135,120};
 const char*labels[]={"Reset to Default","Save","Unassign","Return"};
 for(int i=0;i<4;i++){input_rect(a,bx[i],425,bw[i],40,a->gray);input_text(a,bx[i]+10,452,labels[i]);}
 input_text(a,20,495,"Select a keyboard or gamepad binding, then choose Unassign.");
 input_text(a,20,522,"Save persists changes. Return closes without writing to disk.");
 input_text(a,20,549,"Save State keybind: Not assigned (coming soon)");
 input_text(a,20,576,"Load State keybind: Not assigned (coming soon)");
 XFlush(a->d);
}
static void input_show(App*a){
 if(a->input_win){XRaiseWindow(a->d,a->input_win);return;}
 int scr=DefaultScreen(a->d);
 a->input_win=XCreateSimpleWindow(a->d,RootWindow(a->d,scr),140,140,820,610,1,a->white,a->black);
 XStoreName(a->d,a->input_win,"InstantNES - Input Configuration");
 XSetWMProtocols(a->d,a->input_win,&a->wm_delete,1);
 XSelectInput(a->d,a->input_win,ExposureMask|ButtonPressMask|KeyPressMask|StructureNotifyMask);
 a->editor=1;a->capture=a->pcapture=a->selected=-1;a->armed=0;
 memset(a->key_down,0,sizeof(a->key_down));a->key_state=0;input_update(a);
 XMapRaised(a->d,a->input_win);input_paint(a);
}
static int menu_visible(const App*a){return a->screen_mode==0||a->menu_hover||a->menu!=0;}
/* Draw UI into an X11 pixmap, then composite it as a GL texture BEFORE
   swapping. X11 never paints directly over a GLX front buffer. Only menu and
   status strips are read back; normal gameplay performs no UI readback. */
static void gl_overlay_strip(App*a,int y,int h){
 if(h<=0||a->win_w<=0)return;
 INGL*g=&a->gl;
 XSync(a->d,False);
 XImage*im=XGetImage(a->d,a->back,0,y,(unsigned)a->win_w,(unsigned)h,AllPlanes,ZPixmap);
 if(!im)return;
 size_t count=(size_t)a->win_w*h;
 uint32_t*rgba=malloc(count*4);
 if(!rgba){XDestroyImage(im);return;}
 for(int yy=0;yy<h;yy++)for(int xx=0;xx<a->win_w;xx++){
  unsigned long p=XGetPixel(im,xx,yy);
  /* The pixmap is cleared to black; black is the transparent key. */
  rgba[(size_t)yy*a->win_w+xx]=(uint32_t)(p&0x00ffffffu)|((p&0xffffffu)?0xff000000u:0);
 }
 XDestroyImage(im);
 g->bindtex(IN_GL_TEXTURE_2D,g->ui_tex);
 g->teximage(IN_GL_TEXTURE_2D,0,IN_GL_RGBA,a->win_w,h,0,IN_GL_BGRA,IN_GL_UNSIGNED_BYTE,rgba);
 free(rgba);
 g->enable(IN_GL_BLEND);g->blendfunc(IN_GL_SRC_ALPHA,IN_GL_ONE_MINUS_SRC_ALPHA);
 g->begin(IN_GL_QUADS);
 g->texcoord(0,0);g->vertex(0,(float)y);
 g->texcoord(1,0);g->vertex((float)a->win_w,(float)y);
 g->texcoord(1,1);g->vertex((float)a->win_w,(float)(y+h));
 g->texcoord(0,1);g->vertex(0,(float)(y+h));g->end();
 g->disable(IN_GL_BLEND);
}
static void paint(App*a){
 /* Compose software video and UI into one offscreen pixmap, then publish once.
    This avoids exposing partially drawn menus and letterbox regions. */
 if(!a->gl.ready){rect(a,0,0,a->win_w,a->win_h,a->black);}
 /* Fullscreen menu overlays the picture instead of resizing it every hover. */int bar=a->screen_mode==0?30:0;if(!in_has_rom(a->nes)||a->editor)rect(a,0,0,a->win_w,a->win_h,a->black);if(a->editor){ /* Input configuration is painted in its own window. */ }else if(in_has_rom(a->nes)){const uint32_t*fb=in_framebuffer(a->nes);/* The scaled renderer reads the native 32-bit framebuffer directly. */int aw=a->win_w,ah=a->win_h-bar;if(aw>0&&ah>0){
 int dw=aw,dh=ah;
 {double scale=fmin((double)aw/IN_WIDTH,(double)ah/IN_HEIGHT);dw=(int)(IN_WIDTH*scale);dh=(int)(IN_HEIGHT*scale);}
 if(dw>0&&dh>0 && a->gl.ready){gl_present(a,fb,(aw-dw)/2,bar+(ah-dh)/2,dw,dh);}
 else if(dw>0&&dh>0){
 /* Clear only letterbox regions, not the entire fullscreen drawable. */
 int left=(aw-dw)/2,top=bar+(ah-dh)/2;
 if(left>0){rect(a,0,bar,left,ah,a->black);rect(a,left+dw,bar,aw-left-dw,ah,a->black);}
 if(top>bar)rect(a,left,bar,dw,top-bar,a->black);
 if(top+dh<a->win_h)rect(a,left,top+dh,dw,a->win_h-top-dh,a->black);
 size_t count=(size_t)dw*dh;
 int use_shm=shm_ensure(a,dw,dh) && a->shm_image->bits_per_pixel==32 && a->shm_image->bytes_per_line==(int)((size_t)dw*4);
 /* Scale directly into shared memory: avoid an extra fullscreen-sized copy. */
 uint32_t *scaled=NULL;
 if(use_shm){
  /* The previous frame's XShmPutImage and final pixmap copy are ordered
     on the X connection. Wait only when reusing its shared buffer, not
     between video transfer and UI composition. */
  if(a->shm_pending){XSync(a->d,False);a->shm_pending=0;}
  scaled=(uint32_t*)a->shm_image->data;
 }
 else {
  if(count>a->scaled_capacity){uint32_t *p=realloc(a->scaled_pixels,count*sizeof(uint32_t));if(p){a->scaled_pixels=p;a->scaled_capacity=count;}}
  if(a->scaled_capacity>=count)scaled=a->scaled_pixels;
 }
 if(scaled){
  if(a->x_lookup_width!=dw){uint16_t *lookup=malloc((size_t)dw*sizeof(*lookup));if(lookup){for(int x=0;x<dw;x++)lookup[x]=(uint16_t)((int64_t)x*IN_WIDTH/dw);free(a->x_lookup);a->x_lookup=lookup;a->x_lookup_width=dw;}}
  /* Cache vertical source indices as well as horizontal indices. The display
     dimensions remain fixed across most frames, so avoid repeated divisions. */
  if(a->y_lookup_height!=dh){
   uint16_t *lookup=malloc((size_t)dh*sizeof(*lookup));
   if(lookup){for(int y=0;y<dh;y++)lookup[y]=(uint16_t)((int64_t)y*IN_HEIGHT/dh);free(a->y_lookup);a->y_lookup=lookup;a->y_lookup_height=dh;}
  }
  int previous_sy=-1;
  for(int yy=0;yy<dh;yy++){
   int sy=(a->y_lookup && a->y_lookup_height==dh)?a->y_lookup[yy]:(int)((int64_t)yy*IN_HEIGHT/dh);
   uint32_t *dst=scaled+(size_t)yy*dw;
   if(sy==previous_sy){memcpy(dst,dst-dw,(size_t)dw*sizeof(uint32_t));continue;}
   previous_sy=sy;
   const uint32_t *src=fb+(size_t)sy*IN_WIDTH;
   if(a->x_lookup && a->x_lookup_width==dw){for(int xx=0;xx<dw;xx++)dst[xx]=src[a->x_lookup[xx]];}
   else{for(int xx=0;xx<dw;xx++)dst[xx]=src[(int)((int64_t)xx*IN_WIDTH/dw)];}
  }
  if(use_shm){
   if(a->shm_put(a->d,a->back,a->gc,a->shm_image,0,0,(aw-dw)/2,bar+(ah-dh)/2,(unsigned)dw,(unsigned)dh,False))
    a->shm_pending=1;
  }else{
   XImage *view=XCreateImage(a->d,DefaultVisual(a->d,DefaultScreen(a->d)),DefaultDepth(a->d,DefaultScreen(a->d)),ZPixmap,0,(char*)scaled,dw,dh,32,0);
   if(view){XPutImage(a->d,a->back,a->gc,view,0,0,(aw-dw)/2,bar+(ah-dh)/2,dw,dh);view->data=NULL;XDestroyImage(view);}
  }
 }
 }
 }
 }if(a->gl.ready){
 /* Clear just the UI strips to the transparency key, never the GL drawable. */
 int mh=a->menu==3?255:(a->menu?165:30);
 if(mh>a->win_h)mh=a->win_h;
 rect(a,0,0,a->win_w,mh,a->black);
 if(a->status[0]&&a->win_h>50)rect(a,0,a->win_h-50,a->win_w,50,a->black);
 }if(menu_visible(a)){rect(a,0,0,a->win_w,30,a->gray);text(a,12,20,"File");text(a,76,20,"Config");text(a,155,20,"System");text(a,245,20,"Info");}if(a->menu){
 rect(a,a->menu==1?0:(a->menu==2?65:(a->menu==4?235:145)),30,a->menu==3?305:(a->menu==1?160:125),a->menu==3?130:(a->menu==1?85:35),a->gray);
 if(a->menu==1){text(a,10,50,"Open NES ROM");text(a,10,75,"Reset");text(a,10,100,"Quit");}
 else if(a->menu==2)text(a,75,50,"Input");
 else if(a->menu==4)text(a,255,55,"About");
 else {
 text(a,155,50,a->screen_mode==1?"[x] Fullscreen":"[ ] Fullscreen");
 text(a,155,75,in_get_mute(a->nes)?"[x] Mute":"[ ] Mute");
 text(a,155,100,"Save State");text(a,155,125,"Load State");
 text(a,155,150,"Save State Slot  >");
 if(a->slot_menu){
  rect(a,450,130,105,125,a->gray);
  for(int i=0;i<5;i++){
   char label[24];snprintf(label,sizeof(label),"[%c] %d",a->save_slot==i?'x':' ',i+1);
   text(a,460,150+i*25,label);
  }
 }
 }
}if(a->status[0])text(a,15,a->win_h-30,a->status);
 if(a->gl.ready){
  if(menu_visible(a)||a->menu){int mh=a->menu==3?255:(a->menu?165:30);if(mh>a->win_h)mh=a->win_h;gl_overlay_strip(a,0,mh);}
  if(a->status[0]&&a->win_h>50)gl_overlay_strip(a,a->win_h-50,50);
  a->gl.swap(a->d,a->w);
 }else {
  /* The shared-memory transfer must finish before the completed pixmap is
     copied to the visible window. XCopyArea is ordered after XShmPutImage. */
  XCopyArea(a->d,a->back,a->w,a->gc,0,0,(unsigned)a->win_w,(unsigned)a->win_h,0,0);
  XFlush(a->d);
 }
}
/* Transient notifications are cleared using monotonic time, independent of
   frame rate and of fullscreen transitions. Other status messages retain
   their existing behavior. */
static void status_rom_loaded(App*a){
 clock_gettime(CLOCK_MONOTONIC,&a->status_expires);
 a->status_expires.tv_sec+=3;
}
static void status_expire(App*a){
 if(!a->status_expires.tv_sec)return;
 struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
 if(now.tv_sec>a->status_expires.tv_sec ||
   (now.tv_sec==a->status_expires.tv_sec && now.tv_nsec>=a->status_expires.tv_nsec)){
  a->status[0]=0;a->status_expires.tv_sec=0;
 }
}
static void file_open(App*a){/* Prefer a system file chooser; Tk is a portable fallback where Zenity is absent. */char path[4096]={0};FILE*f=popen("if command -v zenity >/dev/null 2>&1; then zenity --file-selection --title='Open NES ROM' --file-filter='NES ROMs | *.nes *.NES' 2>/dev/null; else python3 -c 'import tkinter as t; from tkinter import filedialog; r=t.Tk();r.withdraw();print(filedialog.askopenfilename(title=\"Open NES ROM\",filetypes=[(\"NES ROM\",\"*.nes\"),(\"All files\",\"*\")]))' 2>/dev/null; fi","r");if(!f){snprintf(a->status,sizeof(a->status),"Could not launch file chooser");return;}if(fgets(path,sizeof(path),f)){size_t n=strlen(path);while(n&&(path[n-1]=='\n'||path[n-1]=='\r'))path[--n]=0;}pclose(f);if(*path){in_result r=in_load_file(a->nes,path);if(r==IN_OK)snprintf(rom_path,sizeof(rom_path),"%s",path);snprintf(a->status,sizeof(a->status),"%s",r==IN_OK?"ROM loaded":in_result_string(r));if(r==IN_OK)status_rom_loaded(a);else a->status_expires.tv_sec=0;a->key_state=0;a->pad_active=0;input_update(a);}}
static void state_action(App*a,int load){a->status_expires.tv_sec=0;char path[4096];if(!in_has_rom(a->nes)){snprintf(a->status,sizeof(a->status),"Load a ROM first");return;}if(state_path(path,sizeof(path),a->save_slot)){snprintf(a->status,sizeof(a->status),"Invalid save-state path");return;}in_result r=load?in_load_state(a->nes,path):in_save_state(a->nes,path);snprintf(a->status,sizeof(a->status),"%s slot %d: %s",load?"Load":"Save",a->save_slot+1,in_result_string(r));}
static void editor_exit(App*a,int save){a->status_expires.tv_sec=0;if(save&&config_save(a)!=0)snprintf(a->status,sizeof(a->status),"Could not save input.ini: %s",strerror(errno));a->editor=0;if(a->input_win){Window closing=a->input_win;a->input_win=0;XDestroyWindow(a->d,closing);}a->capture=a->pcapture=-1;memset(a->key_down,0,sizeof(a->key_down));a->key_state=0;input_update(a);}
static void set_screen_mode(App*a,int mode){
 if(a->screen_mode==mode)return;
 a->menu_hover=0;a->menu=0;a->slot_menu=0;
 int old=a->screen_mode;a->screen_mode=mode;
 if((old==0)!=(mode==0)){
 Atom state=XInternAtom(a->d,"_NET_WM_STATE",False),full=XInternAtom(a->d,"_NET_WM_STATE_FULLSCREEN",False);
 XEvent ev={0};ev.xclient.type=ClientMessage;ev.xclient.window=a->w;ev.xclient.message_type=state;ev.xclient.format=32;
 ev.xclient.data.l[0]=mode?1:0;ev.xclient.data.l[1]=full;ev.xclient.data.l[3]=1;
 XSendEvent(a->d,DefaultRootWindow(a->d),False,SubstructureNotifyMask|SubstructureRedirectMask,&ev);
 }
}
static void input_click(App*a,int x,int y){
 if(y>=425&&y<465){
  if(x>=15&&x<190)defaults_set(a);
  else if(x>=200&&x<320)editor_exit(a,1);
  else if(x>=335&&x<470&&a->selected>=0){
   if(a->selected_pad)a->pads[a->selected]=-1;else a->keys[a->selected]=NoSymbol;
   a->capture=a->pcapture=-1;key_recompute(a);input_update(a);
  }else if(x>=480&&x<600)editor_exit(a,0);
 }else if(y>=78&&y<376){int i=(y-78)/36;
  if(i>=0&&i<NBTN){
   if(x>=195&&x<480){a->capture=i;a->pcapture=-1;a->selected=i;a->selected_pad=0;}
   else if(x>=480){a->pcapture=i;a->capture=-1;a->armed=0;a->selected=i;a->selected_pad=1;}
  }
 }
 if(a->input_win)input_paint(a);
}
static void click(App*a,int x,int y){
 if(a->screen_mode&& !menu_visible(a))return;
 if(y<30){if(x<65)a->menu=a->menu==1?0:1;else if(x<145)a->menu=a->menu==2?0:2;else if(x<235)a->menu=a->menu==3?0:3;else if(x<325)a->menu=a->menu==4?0:4;else a->menu=0;a->slot_menu=0;return;}
 if(a->menu==3&&a->slot_menu&&x>=450&&x<555&&y>=130&&y<255){int slot=(y-130)/25;if(slot>=0&&slot<5)a->save_slot=slot;a->menu=0;a->slot_menu=0;return;}
 if(a->menu==4&&y>=30&&y<75){about_show(a);}
 else if(a->menu==1){if(y<60)file_open(a);else if(y<85){in_result r=in_reset(a->nes);snprintf(a->status,sizeof(a->status),"%s",in_result_string(r));memset(a->key_down,0,sizeof(a->key_down));a->key_state=0;input_update(a);}else if(y<115){XEvent ev={0};ev.type=ClientMessage;ev.xclient.data.l[0]=a->wm_delete;XSendEvent(a->d,a->w,False,NoEventMask,&ev);}}
 else if(a->menu==2&&y<65){input_show(a);}
 else if(a->menu==3&&x>=145&&x<450&&y>=30&&y<160){
  if(y>=130){a->slot_menu=!a->slot_menu;return;}
  if(y>=85){state_action(a,y>=110);}
  else if(y>=60)in_set_mute(a->nes,!in_get_mute(a->nes));
  else set_screen_mode(a,a->screen_mode?0:1);
 }
 a->menu=0;a->slot_menu=0;
}
static void key_event(App*a,XKeyEvent*ev,int down){
 KeySym k=XLookupKeysym(ev,0);
 if(a->editor){
  if(down&&a->capture>=0){
   if(k==XK_Escape)a->capture=-1;
   else if(k!=NoSymbol){int idx=a->capture;KeySym old=a->keys[idx];for(int j=0;j<NBTN;j++)if(j!=idx&&a->keys[j]==k)a->keys[j]=old;a->keys[idx]=k;a->capture=-1;}
  }
  if(a->input_win)input_paint(a);
  return;
 }
 /* ESC toggles Windowed <-> Fullscreen.
    Trigger only on the initial key press while the main emulator is active;
    key-repeat and releases must not change display modes. */
 if(k==XK_Escape){
  if(ev->keycode<256){
   int was=a->key_down[ev->keycode];
   a->key_down[ev->keycode]=down!=0;
   if(down&&!was&&in_has_rom(a->nes))set_screen_mode(a,a->screen_mode?0:1);
  }
  return;
 }
 if(a->menu)return;
 if(ev->keycode>=256)return;
 int was=a->key_down[ev->keycode];a->key_down[ev->keycode]=down!=0;
 if(down&&!was){for(int i=0;i<NBTN;i++)if(k==a->keys[i]){a->pad_active=0;break;}}
 key_recompute(a);input_update(a);
}
int main(void){App a={0};config_init();a.pad_fd=-1;a.pad_idx=-1;a.capture=a.pcapture=a.selected=-1;defaults_set(&a);config_load(&a);a.nes=in_create();if(!a.nes){fprintf(stderr,"NES core initialization failed\n");return 1;}a.d=XOpenDisplay(NULL);if(!a.d){fprintf(stderr,"Cannot open X display (set DISPLAY)\n");in_destroy(a.nes);return 1;}int scr=DefaultScreen(a.d);a.black=BlackPixel(a.d,scr);a.white=WhitePixel(a.d,scr);a.gray=0x333333;a.win_w=768;a.win_h=720;a.w=XCreateSimpleWindow(a.d,RootWindow(a.d,scr),0,0,768,720,0,a.white,a.black);XStoreName(a.d,a.w,"InstantNES");/* Avoid automatic X11 background clears during fullscreen resize/expose.
   The application explicitly paints the complete visible frame. */
 XSetWindowBackgroundPixmap(a.d,a.w,None);Atom icon_atom=XInternAtom(a.d,"_NET_WM_ICON",False);unsigned long icon_data[2+4096];icon_data[0]=IN_ICON_SIZE;icon_data[1]=IN_ICON_SIZE;for(int i=0;i<4096;i++)icon_data[i+2]=in_icon_argb[i];XChangeProperty(a.d,a.w,icon_atom,XA_CARDINAL,32,PropModeReplace,(unsigned char*)icon_data,4098);XSelectInput(a.d,a.w,ExposureMask|KeyPressMask|KeyReleaseMask|ButtonPressMask|PointerMotionMask|LeaveWindowMask|FocusChangeMask|StructureNotifyMask);a.wm_delete=XInternAtom(a.d,"WM_DELETE_WINDOW",False);XSetWMProtocols(a.d,a.w,&a.wm_delete,1);a.back=XCreatePixmap(a.d,a.w,(unsigned)a.win_w,(unsigned)a.win_h,(unsigned)DefaultDepth(a.d,scr));a.gc=XCreateGC(a.d,a.w,0,NULL);XSetGraphicsExposures(a.d,a.gc,False);a.ui_font=XLoadQueryFont(a.d,"-misc-fixed-bold-r-normal--20-200-75-75-c-100-iso8859-1");if(!a.ui_font)a.ui_font=XLoadQueryFont(a.d,"10x20");if(a.ui_font)XSetFont(a.d,a.gc,a.ui_font->fid);a.image_data=calloc(IN_WIDTH*IN_HEIGHT,4);a.img=XCreateImage(a.d,DefaultVisual(a.d,scr),DefaultDepth(a.d,scr),ZPixmap,0,(char*)a.image_data,IN_WIDTH,IN_HEIGHT,32,0);if(!a.img){fprintf(stderr,"Could not create XImage\n");return 1;}shm_init(&a);XMapWindow(a.d,a.w);gl_init(&a);INAudio audio;in_audio_open_device(&audio);int running=1;struct timespec next;clock_gettime(CLOCK_MONOTONIC,&next);while(running){while(XPending(a.d)){XEvent e;XNextEvent(a.d,&e);if(e.xany.window==a.about&&a.about){
 if(e.type==ClientMessage){if((Atom)e.xclient.data.l[0]==a.wm_delete){Window closing=a.about;a.about=0;XDestroyWindow(a.d,closing);}}else if(e.type==DestroyNotify){a.about=0;}
 else if(e.type==Expose)about_paint(&a);
 else if(e.type==KeyPress){KeySym k=XLookupKeysym(&e.xkey,0);if(k==XK_Escape){XDestroyWindow(a.d,a.about);a.about=0;}}
 continue;}
 if(a.input_win&&e.xany.window==a.input_win){
  if(e.type==ClientMessage&&(Atom)e.xclient.data.l[0]==a.wm_delete)editor_exit(&a,0);
  else if(e.type==DestroyNotify){a.input_win=0;a.editor=0;a.capture=a.pcapture=-1;input_update(&a);}
  else if(e.type==Expose&&e.xexpose.count==0)input_paint(&a);
  else if(e.type==ButtonPress)input_click(&a,e.xbutton.x,e.xbutton.y);
  else if(e.type==KeyPress)key_event(&a,&e.xkey,1);
  continue;
 }
 if(e.xany.window!=a.w)continue; /* Never let secondary-window events terminate the emulator. */
 if(e.type==ClientMessage&&(Atom)e.xclient.data.l[0]==a.wm_delete)running=0;else if(e.type==DestroyNotify)running=0;else if(e.type==ConfigureNotify){if(e.xconfigure.width>0&&e.xconfigure.height>0&&(a.win_w!=e.xconfigure.width||a.win_h!=e.xconfigure.height)){a.win_w=e.xconfigure.width;a.win_h=e.xconfigure.height;XFreePixmap(a.d,a.back);a.back=XCreatePixmap(a.d,a.w,(unsigned)a.win_w,(unsigned)a.win_h,(unsigned)DefaultDepth(a.d,scr));}}else if(e.type==MotionNotify){if(a.screen_mode){/* Hysteresis prevents menu-bar flicker near the 30px hover boundary. */int hover=a.menu_hover?(e.xmotion.y<42):(e.xmotion.y<30);if(hover!=a.menu_hover){a.menu_hover=hover;if(!hover&&!a.menu){a.slot_menu=0;}}}}else if(e.type==LeaveNotify){if(a.screen_mode){a.menu_hover=0;if(!a.menu)a.slot_menu=0;}}else if(e.type==ButtonPress)click(&a,e.xbutton.x,e.xbutton.y);else if(e.type==KeyPress)key_event(&a,&e.xkey,1);else if(e.type==KeyRelease){if(XEventsQueued(a.d,QueuedAfterReading)){XEvent next;XPeekEvent(a.d,&next);if(next.type==KeyPress&&next.xkey.keycode==e.xkey.keycode&&next.xkey.time==e.xkey.time)continue;}key_event(&a,&e.xkey,0);}else if(e.type==FocusOut){a.key_state=0;input_update(&a);}}status_expire(&a);pad_poll(&a);if(!a.editor&&in_has_rom(a.nes)){in_run_frame(a.nes);in_audio_submit(&audio,a.nes);}paint(&a);next.tv_nsec+=16639267L;if(next.tv_nsec>=1000000000L){next.tv_nsec-=1000000000L;next.tv_sec++;}struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);long long wait=(long long)(next.tv_sec-now.tv_sec)*1000000000LL+next.tv_nsec-now.tv_nsec;if(wait>0&&wait<100000000LL)clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&next,NULL);
 /* A tiny overshoot must not reset phase every frame: that produces
    irregular frame durations. Rebase only after a full missed frame. */
 else if(wait<=-16639267LL || wait>=100000000LL)next=now;}in_audio_close_device(&audio);gl_shutdown(&a);shm_release(&a);if(a.xext)dlclose(a.xext);free(a.x_lookup);free(a.y_lookup);free(a.scaled_pixels);pad_close(&a);XDestroyImage(a.img);if(a.about)XDestroyWindow(a.d,a.about);if(a.input_win)XDestroyWindow(a.d,a.input_win);if(a.ui_font)XFreeFont(a.d,a.ui_font);XFreeGC(a.d,a.gc);XFreePixmap(a.d,a.back);XDestroyWindow(a.d,a.w);XCloseDisplay(a.d);in_destroy(a.nes);return 0;}
