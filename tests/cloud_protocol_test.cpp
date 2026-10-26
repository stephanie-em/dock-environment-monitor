#include "../SkyMindEnvironment/CloudProtocol.h"
#include <assert.h>
#include <iostream>
#include <string>
#include <stdlib.h>
using namespace CloudProtocol;
int main(int argc,char** argv) {
  uint8_t random[16]={};char boot[37];uuid(boot,random);assert(uuidValid(boot));
  assert(!uuidValid("A1B2C3D4E5F60708"));assert(deviceIdValid("owner-device:1"));assert(!deviceIdValid("bad\"id"));
  assert(credentialValid("xenv_0000000000000000000000000000000000000000000000000000000000000000"));
  assert(!credentialValid("xdv_0000000000000000000000000000000000000000000000000000000000000000"));
  assert(clockUsable(true,10000,9000,1800000000000LL));assert(!clockUsable(false,10000,9000,1800000000000LL));
  assert(!clockUsable(true,9000,10000,1800000000000LL));assert(!clockUsable(true,2000000,0,1800000000000LL));
  assert(!clockUsable(true,10000,9000,100LL));assert(pendingFresh(1000,1500));assert(!pendingFresh(1000,1501));
  assert(!pendingFresh(1000,999));assert(retryMs(1)==2000&&retryMs(2)==4000&&retryMs(4)==16000&&retryMs(5)==30000&&retryMs(100)==30000);
  Meta meta{"contract-test-device",boot,1,0,21000,1800000000000ULL,false};Sensor sensors[6];
  for(auto& sensor:sensors)sensor={0,true,true,0};
  std::string mode=argc>1?argv[1]:"zero";
  if(mode=="fixture") {double values[]={0,0,50,64,0,25.562};for(size_t i=0;i<6;i++)sensors[i]={values[i],true,true,i==0?1000U:0U};}
  if(mode=="binary") {sensors[1].value=sensors[4].value=1;}
  if(mode=="paused")meta.paused=true;
  if(mode=="invalid") {
    sensors[0]={0,false,false,0};sensors[1].value=2;sensors[2].ageMs=10000;sensors[3].value=NAN;
    sensors[4].value=-1;sensors[5].value=126;
    for(size_t i=0;i<6;i++)assert(!sensorValid(i,sensors[i],meta.uptimeMs,meta.paused));
  }
  if(mode=="rollover") {meta.uptimeMs=0x100000000ULL+2500;sensors[0].ageMs=750;}
  if(argc>2)meta.capturedAt=strtoull(argv[2],nullptr,10);
  char output[2048];assert(encode(output,sizeof output,meta,sensors));
  char small[40];assert(!encode(small,sizeof small,meta,sensors));
  Meta bad=meta;bad.bootNumber=0;assert(!encode(output,sizeof output,bad,sensors));
  bad=meta;bad.deviceId="invalid id";assert(!encode(output,sizeof output,bad,sensors));
  assert(encode(output,sizeof output,meta,sensors));std::cout<<output<<"\n";
}
