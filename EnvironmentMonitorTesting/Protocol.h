#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

namespace Modbus {
inline uint16_t crc(const uint8_t *p, size_t n) {
  uint16_t c = 0xffff;
  while (n--) { c ^= *p++; for (uint8_t j=0;j<8;j++) c = (c & 1) ? (c >> 1) ^ 0xa001 : c >> 1; }
  return c;
}
inline bool validRequest(uint8_t id,uint8_t fn,uint16_t addr,uint16_t count) {
  return id>=1 && id<=247 && fn>=1 && fn<=4 && count>=1 && count<=8 &&
    ((fn<=2 && count==1) || fn>=3) && uint32_t(addr)+count<=65536;
}
inline void request(uint8_t *p,uint8_t id,uint8_t fn,uint16_t addr,uint16_t count) {
  p[0]=id;p[1]=fn;p[2]=addr>>8;p[3]=addr;p[4]=count>>8;p[5]=count;
  uint16_t c=crc(p,6);p[6]=c;p[7]=c>>8;
}
enum Error { OK, SHORT_FRAME, BAD_CRC, WRONG_ID, WRONG_FUNCTION, BAD_LENGTH, EXCEPTION, WRONG_ECHO };
inline const char* message(Error e) {
  const char* m[]={"OK","Incomplete reply","CRC error / collision / wiring","Wrong device ID","Wrong function","Unexpected reply length","Modbus exception","Write echo did not match request"};
  return m[e];
}
inline Error parseWrite(const uint8_t *p,size_t n,const uint8_t *sent,uint8_t &exception) {
  exception=0;
  if(n<5)return SHORT_FRAME;
  if(crc(p,n-2)!=(uint16_t(p[n-2])|(uint16_t(p[n-1])<<8)))return BAD_CRC;
  if(p[0]!=sent[0])return WRONG_ID;
  if(p[1]==0x86){if(n!=5)return BAD_LENGTH;exception=p[2];return EXCEPTION;}
  if(p[1]!=6)return WRONG_FUNCTION;
  if(n!=8)return BAD_LENGTH;
  return memcmp(p,sent,6)==0?OK:WRONG_ECHO;
}
inline Error parse(const uint8_t *p,size_t n,uint8_t id,uint8_t fn,uint16_t count,uint16_t *regs,uint8_t &exception) {
  exception=0;
  if(n<5)return SHORT_FRAME;
  if(crc(p,n-2)!=(uint16_t(p[n-2])|(uint16_t(p[n-1])<<8)))return BAD_CRC;
  if(p[0]!=id)return WRONG_ID;
  if(p[1]==uint8_t(fn|0x80)) { if(n!=5)return BAD_LENGTH;exception=p[2];return EXCEPTION; }
  if(p[1]!=fn)return WRONG_FUNCTION;
  size_t bytes=fn<=2?1:count*2;
  if(p[2]!=bytes || n!=bytes+5)return BAD_LENGTH;
  if(fn<=2)regs[0]=p[3]&1;
  else for(uint16_t i=0;i<count;i++)regs[i]=(uint16_t(p[3+i*2])<<8)|p[4+i*2];
  return OK;
}
// 0 u16; 1 s16; 2/3 u32 high/low word first; 4/5 s32; 6/7 IEEE float32.
inline uint16_t registerCount(uint8_t format) {return format<2?1:2;}
inline double decode(const uint16_t *r,uint8_t format,bool swapBytes,uint32_t &bits) {
  uint16_t a=r[0],b=format>=2?r[1]:0;
  if(swapBytes){a=(a>>8)|(a<<8);b=(b>>8)|(b<<8);}
  if(format<2){bits=a;return format==0?double(a):double(int16_t(a));}
  bits=(format&1)?(uint32_t(b)<<16)|a:(uint32_t(a)<<16)|b;
  if(format<4)return double(bits);
  if(format<6)return double(int32_t(bits));
  float f;memcpy(&f,&bits,4);return double(f);
}
}
