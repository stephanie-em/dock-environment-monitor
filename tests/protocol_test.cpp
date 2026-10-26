#include "../EnvironmentMonitorTesting/Protocol.h"
#include <assert.h>
#include <iostream>
#include <vector>
using namespace Modbus;
static std::vector<uint8_t> frame(std::initializer_list<uint8_t> x) {
  std::vector<uint8_t> b(x);auto c=crc(b.data(),b.size());b.push_back(c&255);b.push_back(c>>8);return b;
}
int main(){
  uint8_t requestBytes[8];request(requestBytes,1,3,0,1);
  const uint8_t golden[]={1,3,0,0,0,1,0x84,0x0a};assert(!memcmp(requestBytes,golden,8));
  assert(validRequest(1,3,65535,1));assert(!validRequest(1,3,65535,2));assert(!validRequest(0,3,0,1));
  assert(!validRequest(248,3,0,1));assert(!validRequest(1,6,0,1));assert(!validRequest(1,1,0,2));
  uint16_t r[8]={};uint8_t ex=0;
  // 65.3% only if the sensor's documented scale is 0.1.
  auto good=frame({1,3,2,0x02,0x8d});assert(parse(good.data(),good.size(),1,3,1,r,ex)==OK&&r[0]==653);
  auto corrupt=good;corrupt[3]^=1;assert(parse(corrupt.data(),corrupt.size(),1,3,1,r,ex)==BAD_CRC);
  assert(parse(good.data(),good.size(),2,3,1,r,ex)==WRONG_ID);
  assert(parse(good.data(),good.size(),1,4,1,r,ex)==WRONG_FUNCTION);
  assert(parse(good.data(),good.size(),1,3,2,r,ex)==BAD_LENGTH);
  assert(parse(good.data(),4,1,3,1,r,ex)==SHORT_FRAME);
  auto exception=frame({1,0x83,2});assert(parse(exception.data(),exception.size(),1,3,1,r,ex)==EXCEPTION&&ex==2);
  auto bit=frame({7,2,1,0xff});assert(parse(bit.data(),bit.size(),7,2,1,r,ex)==OK&&r[0]==1);
  auto dry=frame({7,2,1,0});assert(parse(dry.data(),dry.size(),7,2,1,r,ex)==OK&&r[0]==0);
  auto wrongBytes=frame({1,3,4,0,1});assert(parse(wrongBytes.data(),wrongBytes.size(),1,3,1,r,ex)==BAD_LENGTH);
  auto extra=frame({1,3,2,0,1,0});assert(parse(extra.data(),extra.size(),1,3,1,r,ex)==BAD_LENGTH);
  uint32_t bits;
  uint16_t signed16[]={0xff9c,0};assert(decode(signed16,1,false,bits)==-100);
  uint16_t unsigned16[]={0xffff,0};assert(decode(unsigned16,0,false,bits)==65535);
  uint16_t f[]={0x41a0,0};assert(decode(f,6,false,bits)==20.0);
  uint16_t fl[]={0,0x41a0};assert(decode(fl,7,false,bits)==20.0);
  uint16_t fb[]={0xa041,0};assert(decode(fb,6,true,bits)==20.0);
  uint16_t neg[]={0xffff,0xff9c};assert(decode(neg,4,false,bits)==-100);
  uint16_t large[]={0xffff,0xffff};assert(decode(large,2,false,bits)==4294967295.0);
  uint16_t nan[]={0x7fc0,0};assert(!isfinite(decode(nan,6,false,bits)));
  uint16_t inf[]={0x7f80,0};assert(!isfinite(decode(inf,6,false,bits)));
  // Every single-bit corruption of a valid frame must be rejected.
  for(size_t i=0;i<good.size();i++)for(int k=0;k<8;k++){auto b=good;b[i]^=1<<k;assert(parse(b.data(),b.size(),1,3,1,r,ex)!=OK);}
  std::cout<<"Protocol tests passed: known CRC, frame integrity, exceptions, bit reads, length and address limits, signed values, word/byte order and invalid floats.\n";
}
