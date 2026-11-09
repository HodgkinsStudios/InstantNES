/* Portable 2A03 CPU: official instructions and selected NMOS bus-cycle behavior.
   Unsupported unofficial opcodes remain explicit rather than silently misexecuted. */
#include "cpu_internal.h"
#if defined(IN_AC_TRACE) || defined(IN_DMA_TRACE)
#include <stdio.h>
#endif
#define C 0x01
#define Z 0x02
#define I 0x04
#define D 0x08
#define V 0x40
#define N 0x80
static void flag(in_system *s,unsigned f,int on){if(on)s->cpu.p|=(uint8_t)f;else s->cpu.p&=(uint8_t)~f;}
static void nz(in_system *s,uint8_t v){flag(s,Z,v==0);flag(s,N,v&128);}
static uint8_t fetch(in_system *s){return in_bus_read(s,s->cpu.pc++);}
static uint16_t word(in_system *s){uint16_t lo=fetch(s);return (uint16_t)(lo|((uint16_t)fetch(s)<<8));}
static void push(in_system *s,uint8_t v){in_bus_write(s,(uint16_t)(0x100|s->cpu.sp--),v);}
static uint8_t pull(in_system *s){return in_bus_read(s,(uint16_t)(0x100|++s->cpu.sp));}
static void adc(in_system *s,uint8_t v){unsigned a=s->cpu.a,sum=a+v+(!!(s->cpu.p&C));flag(s,C,sum>255);flag(s,V,(~(a^v)&(a^sum)&128)!=0);s->cpu.a=(uint8_t)sum;nz(s,s->cpu.a);}
static void cmp(in_system *s,uint8_t a,uint8_t b){flag(s,C,a>=b);nz(s,(uint8_t)(a-b));}
static void branch(in_system *s,int take,uint32_t *cy){int8_t rel=(int8_t)fetch(s);if(take){uint16_t old=s->cpu.pc;s->cpu.pc=(uint16_t)(s->cpu.pc+rel);++*cy;in_bus_read(s,old);if((old^s->cpu.pc)&0xff00){++*cy;in_bus_read(s,(uint16_t)((old&0xff00)|(s->cpu.pc&0xff)));}}}
in_result in_cpu_step(in_system *s,uint32_t *out){uint8_t op;uint16_t addr=0;uint8_t v;uint32_t cy=0;unsigned cross=0;
 if(!s)return IN_ERR_ARGUMENT;if(out)*out=0;if(!s->has_rom)return IN_ERR_FORMAT;
 if(s->nmi_pending || ((s->irq_line || s->apu_irq) && !(s->cpu.p&I))){
#if defined(IN_AC_TRACE) || defined(IN_DMA_TRACE)
  static unsigned ac_irq_count=0;
  if(ac_irq_count++<60)fprintf(stderr,"AC_INTERRUPT cycle=%llu pc=%04x type=%s scan=%u dot=%u\n",(unsigned long long)s->master_cpu_cycles,s->cpu.pc,s->nmi_pending?"NMI":"IRQ",s->ppu_scanline,s->ppu_dot);
#endif
  uint16_t vector=s->nmi_pending?0xfffa:0xfffe;
  s->nmi_pending=0;
  in_bus_read(s,s->cpu.pc);in_bus_read(s,s->cpu.pc);
  push(s,(uint8_t)(s->cpu.pc>>8));push(s,(uint8_t)s->cpu.pc);
  push(s,(uint8_t)((s->cpu.p|0x20)&~0x10));flag(s,I,1);
  s->cpu.pc=(uint16_t)(in_bus_read(s,vector)|((uint16_t)in_bus_read(s,(uint16_t)(vector+1))<<8));
  s->cpu.cycles+=7;if(out)*out=7;return IN_OK;
 }
 /* Peek before fetch so unsupported opcodes do not change CPU state. */
 s->cpu_bus_clock_active=0;
 op=in_bus_read(s,s->cpu.pc);
 s->cpu_bus_clock_active=1;
 switch(op){
 case 0xea:case 0xa9:case 0xa5:case 0xb5:case 0xad:case 0xbd:case 0xb9:case 0xa1:case 0xb1:
 case 0xa2:case 0xa6:case 0xb6:case 0xae:case 0xbe:case 0xa0:case 0xa4:case 0xb4:case 0xac:case 0xbc:
 case 0x85:case 0x95:case 0x8d:case 0x9d:case 0x99:case 0x81:case 0x91:
 case 0x86:case 0x96:case 0x8e:case 0x84:case 0x94:case 0x8c:
 case 0x69:case 0x65:case 0x75:case 0x6d:case 0x7d:case 0x79:case 0x61:case 0x71:
 case 0xe9:case 0xe5:case 0xf5:case 0xed:case 0xfd:case 0xf9:case 0xe1:case 0xf1:
 case 0xc9:case 0xc5:case 0xd5:case 0xcd:case 0xdd:case 0xd9:case 0xc1:case 0xd1:
 case 0xe0:case 0xe4:case 0xec:case 0xc0:case 0xc4:case 0xcc:
 case 0x29:case 0x25:case 0x35:case 0x2d:case 0x3d:case 0x39:case 0x21:case 0x31:
 case 0x09:case 0x05:case 0x15:case 0x0d:case 0x1d:case 0x19:case 0x01:case 0x11:
 case 0x49:case 0x45:case 0x55:case 0x4d:case 0x5d:case 0x59:case 0x41:case 0x51:
 case 0xe8:case 0xc8:case 0xca:case 0x88:case 0xaa:case 0xa8:case 0x8a:case 0x98:case 0xba:case 0x9a:
 case 0x18:case 0x38:case 0x58:case 0x78:case 0xb8:case 0xd8:case 0xf8:
 case 0x48:case 0x68:case 0x08:case 0x28:case 0x4c:case 0x6c:case 0x20:case 0x60:case 0x40:case 0x00:
 case 0x10:case 0x30:case 0x50:case 0x70:case 0x90:case 0xb0:case 0xd0:case 0xf0:
 case 0x0a:case 0x06:case 0x16:case 0x0e:case 0x1e:
 case 0x2a:case 0x26:case 0x36:case 0x2e:case 0x3e:
 case 0x4a:case 0x46:case 0x56:case 0x4e:case 0x5e:
 case 0x6a:case 0x66:case 0x76:case 0x6e:case 0x7e:
 case 0xe6:case 0xf6:case 0xee:case 0xfe:
 case 0xc6:case 0xd6:case 0xce:case 0xde:
 case 0x24:case 0x2c:break;
 /* NMOS undocumented read-modify-write families. */
 case 0x03:case 0x07:case 0x0f:case 0x13:case 0x17:case 0x1b:case 0x1f:case 0x23:case 0x27:case 0x2f:case 0x33:case 0x37:case 0x3b:case 0x3f:case 0x43:case 0x47:case 0x4f:case 0x53:case 0x57:case 0x5b:case 0x5f:case 0x63:case 0x67:case 0x6f:case 0x73:case 0x77:case 0x7b:case 0x7f:case 0xc3:case 0xc7:case 0xcf:case 0xd3:case 0xd7:case 0xdb:case 0xdf:case 0xe3:case 0xe7:case 0xef:case 0xf3:case 0xf7:case 0xfb:case 0xff:break;
 case 0x83:case 0x87:case 0x8f:case 0x97:case 0xa3:case 0xa7:case 0xaf:case 0xb3:case 0xb7:case 0xbf:break;
 case 0x93:case 0x9f:case 0x9b:case 0x9c:case 0x9e:case 0xbb:break;
 case 0x0b:case 0x2b:case 0x4b:case 0x6b:case 0x8b:case 0xab:case 0xcb:case 0xeb:break;
 /* NMOS 6502 undocumented NOPs: consume operands and perform reads. */
 case 0x1a:case 0x3a:case 0x5a:case 0x7a:case 0xda:case 0xfa:
 case 0x80:case 0x82:case 0x89:case 0xc2:case 0xe2:
 case 0x04:case 0x44:case 0x64:
 case 0x14:case 0x34:case 0x54:case 0x74:case 0xd4:case 0xf4:
 case 0x0c:
 case 0x1c:case 0x3c:case 0x5c:case 0x7c:case 0xdc:case 0xfc:break;
 default:return IN_ERR_UNSUPPORTED;
 }
 fetch(s);cy=2;
 switch(op){
 /* Unstable NMOS high-byte masked stores and LAS/LAE. The store
    address on a page crossing uses the masked value as its high byte. */
 case 0x93:case 0x9f:case 0x9b:case 0x9c:case 0x9e:case 0xbb:{
  uint16_t base,intermediate;uint8_t mask,stored;
  if(op==0x93){uint8_t zp=fetch(s);base=(uint16_t)(in_bus_read(s,zp)|((uint16_t)in_bus_read(s,(uint8_t)(zp+1))<<8));addr=(uint16_t)(base+s->cpu.y);cy=6;}
  else {base=word(s);addr=(uint16_t)(base+((op==0x9c)?s->cpu.x:s->cpu.y));cy=5;}
  intermediate=(uint16_t)((base&0xff00)|(addr&0xff));
  in_bus_read(s,intermediate);
  if(op==0xbb){v=in_bus_read(s,addr);v=(uint8_t)(v&s->cpu.sp);s->cpu.a=s->cpu.x=s->cpu.sp=v;nz(s,v);cy=4+(((base^addr)&0xff00)!=0);break;}
  mask=(uint8_t)((base>>8)+1);
  if(op==0x9c)stored=(uint8_t)(s->cpu.y&mask);
  else if(op==0x9e)stored=(uint8_t)(s->cpu.x&mask);
  else if(op==0x9b){s->cpu.sp=(uint8_t)(s->cpu.a&s->cpu.x);stored=(uint8_t)(s->cpu.sp&mask);}
  else stored=(uint8_t)(s->cpu.a&(s->cpu.x|0xf5)&mask);
  if((base^addr)&0xff00){
   uint8_t index=(op==0x9c)?s->cpu.y:s->cpu.x;
   addr=(uint16_t)(((uint16_t)((addr>>8)&index)<<8)|(addr&0xff));
  }
#ifdef IN_DMA_TRACE
  {static unsigned count=0;if(count++<160)fprintf(stderr,"UNSTABLE_STORE cycle=%llu pc=%04x op=%02x base=%04x effective=%04x write=%04x value=%02x index_x=%02x index_y=%02x dmc_pending=%u\n",(unsigned long long)s->master_cpu_cycles,s->cpu.pc,op,base,(uint16_t)(base+((op==0x9c)?s->cpu.x:s->cpu.y)),addr,stored,s->cpu.x,s->cpu.y,s->dmc_dma_pending);}
#endif
  in_bus_write(s,addr,stored);break;
 }
 /* NMOS SAX stores A & X; LAX loads the same memory byte into both. */
 case 0x83:case 0x87:case 0x8f:case 0x97:case 0xa3:case 0xa7:case 0xaf:case 0xb3:case 0xb7:case 0xbf:{
  unsigned mode=op&0x1f,is_store=(op&0xe0)==0x80;uint16_t base;
  if(mode==3){uint8_t zp=fetch(s);in_bus_read(s,zp);zp=(uint8_t)(zp+s->cpu.x);addr=(uint16_t)(in_bus_read(s,zp)|((uint16_t)in_bus_read(s,(uint8_t)(zp+1))<<8));cy=6;}
  else if(mode==7){addr=fetch(s);cy=3;}
  else if(mode==0xf){addr=word(s);cy=4;}
  else if(mode==0x17){uint8_t zp=fetch(s);in_bus_read(s,zp);addr=(uint8_t)(zp+s->cpu.y);cy=4;}
  else if(mode==0x13){uint8_t zp=fetch(s);base=(uint16_t)(in_bus_read(s,zp)|((uint16_t)in_bus_read(s,(uint8_t)(zp+1))<<8));addr=(uint16_t)(base+s->cpu.y);cy=5;if((base^addr)&0xff00){in_bus_read(s,(uint16_t)((base&0xff00)|(addr&0xff)));cy++;}}
  else {base=word(s);addr=(uint16_t)(base+s->cpu.y);cy=4;if((base^addr)&0xff00){in_bus_read(s,(uint16_t)((base&0xff00)|(addr&0xff)));cy++;}}
  if(is_store){in_bus_write(s,addr,(uint8_t)(s->cpu.a&s->cpu.x));if(mode==3)cy=6;else if(mode==7)cy=3;else if(mode==0xf)cy=4;else cy=4;}
  else {v=in_bus_read(s,addr);s->cpu.a=s->cpu.x=v;nz(s,v);}break;
 }
 /* Undocumented RMW: SLO/RLA/SRE/RRA/DCP/ISC. */
 case 0x03:case 0x07:case 0x0f:case 0x13:case 0x17:case 0x1b:case 0x1f:case 0x23:case 0x27:case 0x2f:case 0x33:case 0x37:case 0x3b:case 0x3f:case 0x43:case 0x47:case 0x4f:case 0x53:case 0x57:case 0x5b:case 0x5f:case 0x63:case 0x67:case 0x6f:case 0x73:case 0x77:case 0x7b:case 0x7f:case 0xc3:case 0xc7:case 0xcf:case 0xd3:case 0xd7:case 0xdb:case 0xdf:case 0xe3:case 0xe7:case 0xef:case 0xf3:case 0xf7:case 0xfb:case 0xff:{
  unsigned mode=op&0x1f,group=op&0xe0;uint16_t base=0;unsigned carry=(s->cpu.p&C)!=0;
  if(mode==0x03){uint8_t zp=fetch(s);in_bus_read(s,zp);zp=(uint8_t)(zp+s->cpu.x);addr=(uint16_t)(in_bus_read(s,zp)|((uint16_t)in_bus_read(s,(uint8_t)(zp+1))<<8));cy=8;}
  else if(mode==0x07){addr=fetch(s);cy=5;}
  else if(mode==0x0f){addr=word(s);cy=6;}
  else if(mode==0x13){uint8_t zp=fetch(s);base=(uint16_t)(in_bus_read(s,zp)|((uint16_t)in_bus_read(s,(uint8_t)(zp+1))<<8));addr=(uint16_t)(base+s->cpu.y);in_bus_read(s,(uint16_t)((base&0xff00)|(addr&0xff)));cy=8;}
  else if(mode==0x17){base=fetch(s);in_bus_read(s,(uint8_t)base);addr=(uint8_t)(base+s->cpu.x);cy=6;}
  else {base=word(s);addr=(uint16_t)(base+((mode==0x1b)?s->cpu.y:s->cpu.x));in_bus_read(s,(uint16_t)((base&0xff00)|(addr&0xff)));cy=7;}
  v=in_bus_read(s,addr);in_bus_write(s,addr,v);
  if(group==0xc0)v--;
  else if(group==0xe0)v++;
  else {flag(s,C,(group==0x40||group==0x60)?(v&1):(v&0x80));
   if(group==0x00)v=(uint8_t)(v<<1);
   else if(group==0x20)v=(uint8_t)((v<<1)|carry);
   else if(group==0x40)v=(uint8_t)(v>>1);
   else v=(uint8_t)((v>>1)|(carry<<7));}
  in_bus_write(s,addr,v);
  if(group==0x00){s->cpu.a|=v;nz(s,s->cpu.a);}
  else if(group==0x20){s->cpu.a&=v;nz(s,s->cpu.a);}
  else if(group==0x40){s->cpu.a^=v;nz(s,s->cpu.a);}
  else if(group==0x60)adc(s,v);
  else if(group==0xc0)cmp(s,s->cpu.a,v);
  else adc(s,(uint8_t)~v);
  break;
 }
 case 0xea:case 0x1a:case 0x3a:case 0x5a:case 0x7a:case 0xda:case 0xfa:break;
 case 0x80:case 0x82:case 0x89:case 0xc2:case 0xe2:fetch(s);cy=2;break;
 case 0x04:case 0x44:case 0x64:{uint8_t a=fetch(s);in_bus_read(s,a);cy=3;break;}
 case 0x14:case 0x34:case 0x54:case 0x74:case 0xd4:case 0xf4:{uint8_t a=fetch(s);in_bus_read(s,a);in_bus_read(s,(uint8_t)(a+s->cpu.x));cy=4;break;}
 case 0x0c:{uint16_t a=word(s);in_bus_read(s,a);cy=4;break;}
 case 0x1c:case 0x3c:case 0x5c:case 0x7c:case 0xdc:case 0xfc:{uint16_t a=word(s),b=(uint16_t)(a+s->cpu.x);in_bus_read(s,(uint16_t)((a&0xff00)|(b&0xff)));cy=4;if((a^b)&0xff00){in_bus_read(s,b);cy=5;}break;}
 case 0x0a:case 0x2a:case 0x4a:case 0x6a:{
  unsigned carry=(s->cpu.p&C)!=0;v=s->cpu.a;
  flag(s,C,(op&0x40)?(v&1):(v&128));
  if(op==0x0a)v=(uint8_t)(v<<1);
  else if(op==0x2a)v=(uint8_t)((v<<1)|carry);
  else if(op==0x4a)v=(uint8_t)(v>>1);
  else v=(uint8_t)((v>>1)|(carry<<7));
  s->cpu.a=v;nz(s,v);break;
 }
 case 0x06:case 0x16:case 0x0e:case 0x1e:
 case 0x26:case 0x36:case 0x2e:case 0x3e:
 case 0x46:case 0x56:case 0x4e:case 0x5e:
 case 0x66:case 0x76:case 0x6e:case 0x7e:
 case 0xe6:case 0xf6:case 0xee:case 0xfe:
 case 0xc6:case 0xd6:case 0xce:case 0xde:{
  unsigned mode=op&0x1f;unsigned carry=(s->cpu.p&C)!=0;
  if(mode==6){addr=fetch(s);cy=5;}
  else if(mode==0x16){addr=(uint8_t)(fetch(s)+s->cpu.x);cy=6;}
  else if(mode==0x0e){addr=word(s);cy=6;}
  else {addr=(uint16_t)(word(s)+s->cpu.x);cy=7;}
  if(mode==0x16)in_bus_read(s,(uint8_t)(addr-s->cpu.x));
  if(mode==0x1e)in_bus_read(s,(uint16_t)((addr-s->cpu.x)&0xff00 | (addr&0xff)));
  v=in_bus_read(s,addr);
  /* NMOS read-modify-write instructions write the original value first. */
  in_bus_write(s,addr,v);
  if((op&0xe0)==0xe0)v++;
  else if((op&0xe0)==0xc0)v--;
  else {
   flag(s,C,(op&0x40)?(v&1):(v&128));
   if((op&0xe0)==0x00)v=(uint8_t)(v<<1);
   else if((op&0xe0)==0x20)v=(uint8_t)((v<<1)|carry);
   else if((op&0xe0)==0x40)v=(uint8_t)(v>>1);
   else v=(uint8_t)((v>>1)|(carry<<7));
  }
  in_bus_write(s,addr,v);nz(s,v);break;
 }
 case 0x24:case 0x2c:{addr=(op==0x24)?fetch(s):word(s);v=in_bus_read(s,addr);
  flag(s,Z,(s->cpu.a&v)==0);flag(s,N,v&0x80);flag(s,V,v&0x40);cy=(op==0x24)?3:4;break;}

 case 0xe8:s->cpu.x++;nz(s,s->cpu.x);break;case 0xc8:s->cpu.y++;nz(s,s->cpu.y);break;
 case 0xca:s->cpu.x--;nz(s,s->cpu.x);break;case 0x88:s->cpu.y--;nz(s,s->cpu.y);break;
 case 0xaa:s->cpu.x=s->cpu.a;nz(s,s->cpu.x);break;case 0xa8:s->cpu.y=s->cpu.a;nz(s,s->cpu.y);break;
 case 0x8a:s->cpu.a=s->cpu.x;nz(s,s->cpu.a);break;case 0x98:s->cpu.a=s->cpu.y;nz(s,s->cpu.a);break;
 case 0xba:s->cpu.x=s->cpu.sp;nz(s,s->cpu.x);break;case 0x9a:s->cpu.sp=s->cpu.x;break;
 case 0x18:flag(s,C,0);break;case 0x38:flag(s,C,1);break;case 0x58:flag(s,I,0);break;case 0x78:flag(s,I,1);break;
 case 0xb8:flag(s,V,0);break;case 0xd8:flag(s,D,0);break;case 0xf8:flag(s,D,1);break;
 case 0x48:in_bus_read(s,s->cpu.pc);push(s,s->cpu.a);cy=3;break;case 0x08:in_bus_read(s,s->cpu.pc);push(s,s->cpu.p|0x30);cy=3;break;
 case 0x68:in_bus_read(s,s->cpu.pc);in_bus_read(s,(uint16_t)(0x100|s->cpu.sp));s->cpu.a=pull(s);nz(s,s->cpu.a);cy=4;break;case 0x28:in_bus_read(s,s->cpu.pc);in_bus_read(s,(uint16_t)(0x100|s->cpu.sp));s->cpu.p=(pull(s)&0xef)|0x20;cy=4;break;
 case 0x4c:s->cpu.pc=word(s);cy=3;break;
 case 0x6c:{uint16_t p=word(s);s->cpu.pc=(uint16_t)(in_bus_read(s,p)|((uint16_t)in_bus_read(s,(uint16_t)((p&0xff00)|((p+1)&255)))<<8));cy=5;break;}
 case 0x20:{uint8_t lo=fetch(s);uint16_t ret=s->cpu.pc;in_bus_read(s,(uint16_t)(0x100|s->cpu.sp));push(s,(uint8_t)(ret>>8));push(s,(uint8_t)ret);s->cpu.pc=(uint16_t)(lo|((uint16_t)fetch(s)<<8));cy=6;break;}
 case 0x60:{uint16_t lo;in_bus_read(s,s->cpu.pc);in_bus_read(s,(uint16_t)(0x100|s->cpu.sp));lo=pull(s);s->cpu.pc=(uint16_t)(lo|((uint16_t)pull(s)<<8));in_bus_read(s,s->cpu.pc);s->cpu.pc=(uint16_t)(s->cpu.pc+1);cy=6;break;}
 case 0x40:{uint16_t lo;in_bus_read(s,s->cpu.pc);in_bus_read(s,(uint16_t)(0x100|s->cpu.sp));s->cpu.p=(pull(s)&0xef)|0x20;lo=pull(s);s->cpu.pc=(uint16_t)(lo|((uint16_t)pull(s)<<8));cy=6;break;}
 case 0x00:{in_bus_read(s,s->cpu.pc);uint16_t ret=(uint16_t)(s->cpu.pc+1);push(s,(uint8_t)(ret>>8));push(s,(uint8_t)ret);push(s,s->cpu.p|0x30);flag(s,I,1);s->cpu.pc=(uint16_t)(in_bus_read(s,0xfffe)|((uint16_t)in_bus_read(s,0xffff)<<8));cy=7;break;}
 case 0x10:branch(s,!(s->cpu.p&N),&cy);break;case 0x30:branch(s,s->cpu.p&N,&cy);break;
 case 0x50:branch(s,!(s->cpu.p&V),&cy);break;case 0x70:branch(s,s->cpu.p&V,&cy);break;
 case 0x90:branch(s,!(s->cpu.p&C),&cy);break;case 0xb0:branch(s,s->cpu.p&C,&cy);break;
 case 0xd0:branch(s,!(s->cpu.p&Z),&cy);break;case 0xf0:branch(s,s->cpu.p&Z,&cy);break;
 /* Undocumented NMOS 6502 immediate instructions. */
 case 0x0b:case 0x2b:{v=fetch(s);s->cpu.a&=v;nz(s,s->cpu.a);flag(s,C,s->cpu.a&0x80);cy=2;break;}
 case 0x4b:{v=fetch(s);s->cpu.a&=v;flag(s,C,s->cpu.a&1);s->cpu.a>>=1;nz(s,s->cpu.a);cy=2;break;}
 case 0x6b:{uint8_t t;v=fetch(s);t=(uint8_t)((s->cpu.a&v)>>1);if(s->cpu.p&C)t|=0x80;s->cpu.a=t;nz(s,t);flag(s,C,t&0x40);flag(s,V,((t>>6)^(t>>5))&1);cy=2;break;}
 case 0xcb:{v=fetch(s);{uint8_t t=(uint8_t)(s->cpu.a&s->cpu.x);flag(s,C,t>=v);s->cpu.x=(uint8_t)(t-v);nz(s,s->cpu.x);}cy=2;break;}
 case 0xeb:{v=fetch(s);adc(s,(uint8_t)~v);cy=2;break;}
 case 0x8b:{v=fetch(s);s->cpu.a=(uint8_t)(s->cpu.x&v);nz(s,s->cpu.a);cy=2;break;}
 case 0xab:{v=fetch(s);s->cpu.a=(uint8_t)(s->cpu.a&v);s->cpu.x=s->cpu.a;nz(s,s->cpu.a);cy=2;break;}
 default:{
  unsigned low=op&31;unsigned grp=op&0xe0;
  /* Addressing modes are shared across the regular ALU/load/store families. */
  if(low==9 || op==0xa2 || op==0xa0 || op==0xe0 || op==0xc0){v=fetch(s);cy=2;goto execute;}
  if(low==5 || low==4 || op==0xa6 || op==0xa4 || op==0x86 || op==0x84 || op==0xe4 || op==0xc4){addr=fetch(s);cy=3;}
  else if(low==0x15 || low==0x14 || op==0xb5 || op==0xb4 || op==0x95 || op==0x94){addr=(uint8_t)(fetch(s)+s->cpu.x);cy=4;in_bus_read(s,(uint8_t)(addr-s->cpu.x));}
  else if(op==0xb6 || op==0x96){addr=(uint8_t)(fetch(s)+s->cpu.y);cy=4;in_bus_read(s,(uint8_t)(addr-s->cpu.y));}
  else if(low==0x0d || low==0x0c || op==0xae || op==0xac || op==0x8e || op==0x8c || op==0xec || op==0xcc){addr=word(s);cy=4;}
  else if(low==0x1d || low==0x1c || op==0xbc){uint16_t base=word(s);addr=(uint16_t)(base+s->cpu.x);cross=((base^addr)&0xff00)!=0;cy=4+cross;if(cross || op==0x9d || op==0x99)in_bus_read(s,(uint16_t)((base&0xff00)|(addr&0xff)));}
  else if(low==0x19 || op==0xbe){uint16_t base=word(s);addr=(uint16_t)(base+s->cpu.y);cross=((base^addr)&0xff00)!=0;cy=4+cross;if(cross || op==0x9d || op==0x99)in_bus_read(s,(uint16_t)((base&0xff00)|(addr&0xff)));}
  else if(low==1){uint8_t zp=fetch(s);in_bus_read(s,zp);zp=(uint8_t)(zp+s->cpu.x);addr=(uint16_t)(in_bus_read(s,zp)|((uint16_t)in_bus_read(s,(uint8_t)(zp+1))<<8));cy=6;}
  else if(low==0x11){uint8_t zp=fetch(s);uint16_t base=(uint16_t)(in_bus_read(s,zp)|((uint16_t)in_bus_read(s,(uint8_t)(zp+1))<<8));addr=(uint16_t)(base+s->cpu.y);cross=((base^addr)&0xff00)!=0;cy=5+cross;if(cross || op==0x91)in_bus_read(s,(uint16_t)((base&0xff00)|(addr&0xff)));}
  else if(op==0xa2 || op==0xa0){v=fetch(s);goto execute;}
  else { /* remaining indexed load/store addressing */
    if(op==0xb6 || op==0x96)addr=(uint8_t)(fetch(s)+s->cpu.y);
    else {uint16_t base=word(s);addr=(uint16_t)(base+((op==0xbe)?s->cpu.y:s->cpu.x));cross=((base^addr)&0xff00)!=0;}cy=4+cross;
  }
  if(op==0xa6||op==0xa4||op==0x86||op==0x84||op==0xe4||op==0xc4)cy=3;
  if(op==0xb6||op==0x96||op==0xb4||op==0x94)cy=4;
  if(op==0xae||op==0xac||op==0x8e||op==0x8c||op==0xec||op==0xcc)cy=4;
  if(op==0xbe||op==0xbc)cy=4+cross;
  if(op==0x9d || op==0x99 || op==0x91)cy=(op==0x91)?6:5;
  if(op==0x81)cy=6;
  if(op==0x85||op==0x95||op==0x8d||op==0x9d||op==0x99||op==0x81||op==0x91){in_bus_write(s,addr,s->cpu.a);break;}
  if(op==0x86||op==0x96||op==0x8e){in_bus_write(s,addr,s->cpu.x);break;}
  if(op==0x84||op==0x94||op==0x8c){in_bus_write(s,addr,s->cpu.y);break;}
  v=in_bus_read(s,addr);
 execute:
  if(op==0xa1 || op==0xa9 || op==0xa5 || op==0xb5 || op==0xad || op==0xbd || op==0xb9 || op==0xb1){s->cpu.a=v;nz(s,v);}
  else if(op==0xa2||op==0xa6||op==0xb6||op==0xae||op==0xbe){s->cpu.x=v;nz(s,v);}
  else if(op==0xa0||op==0xa4||op==0xb4||op==0xac||op==0xbc){s->cpu.y=v;nz(s,v);}
  else if(grp==0x60)adc(s,v);
  else if(grp==0xe0 && !(op==0xe0||op==0xe4||op==0xec))adc(s,(uint8_t)~v);
  else if(grp==0xc0 || op==0xe0||op==0xe4||op==0xec){if(op==0xe0||op==0xe4||op==0xec)cmp(s,s->cpu.x,v);else if(op==0xc0||op==0xc4||op==0xcc)cmp(s,s->cpu.y,v);else cmp(s,s->cpu.a,v);}
  else if(grp==0x20){s->cpu.a&=v;nz(s,s->cpu.a);}
  else if(grp==0x00){s->cpu.a|=v;nz(s,s->cpu.a);}
  else if(grp==0x40){s->cpu.a^=v;nz(s,s->cpu.a);}
  break;
 }
 }
 /* NMOS two-cycle implied and accumulator instructions perform a
    second bus read at the following PC, rather than an idle cycle. */
 if(cy==2 && ((op==0xea) || (op==0x1a)||(op==0x3a)||(op==0x5a)||(op==0x7a)||(op==0xda)||(op==0xfa) ||
    (op==0x0a)||(op==0x2a)||(op==0x4a)||(op==0x6a) ||
    (op==0xe8)||(op==0xc8)||(op==0xca)||(op==0x88) ||
    (op==0xaa)||(op==0xa8)||(op==0x8a)||(op==0x98)||(op==0xba)||(op==0x9a) ||
    (op==0x18)||(op==0x38)||(op==0x58)||(op==0x78)||(op==0xb8)||(op==0xd8)||(op==0xf8)))
     in_bus_read(s,s->cpu.pc);
 s->cpu.cycles+=cy;if(out)*out=cy;return IN_OK;
}
