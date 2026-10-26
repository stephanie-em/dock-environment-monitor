#include "../SkyMindEnvironment/Commission.h"
#include <cassert>
#include <iostream>
#include <vector>
using namespace Commission;
struct Device {
  uint8_t sensor,id;uint32_t baud;bool present=true,dropEcho=false,ignoreWrites=false,badBaudRegister=false;
  uint16_t sample=0;int writes=0,reads=0;uint8_t pendingId=0;uint32_t applyAt=0;
  std::vector<Request> requests;
  Device(uint8_t s,uint8_t address,uint32_t speed):sensor(s),id(address),baud(speed){}
  bool exchange(const Request &q,uint32_t now,uint16_t *r){
    if(pendingId&&int32_t(now-applyAt)>=0){id=pendingId;pendingId=0;}
    requests.push_back(q);
    if(!present||q.id!=id||q.baud!=baud)return false;
    auto p=profile(sensor);
    if(q.fn==6){
      assert(sensor!=1);writes++;
      if(q.reg==p.idRegister){if(!ignoreWrites){pendingId=q.value;applyAt=now+p.settleMs;}}
      else if(q.reg==baudRegister(sensor)){
        assert(sensor==0||sensor==2);
        if(!ignoreWrites)baud=(q.value==baudCode(sensor,4800))?4800:9600;
      }else assert(false);
      return !dropEcho;
    }
    assert(q.fn==3);reads++;
    if(q.reg==p.idRegister&&sensor!=1){r[0]=id;return true;}
    if(q.reg==p.reg){r[0]=sample;if(sensor==2){assert(q.value==2);r[0]=1;r[1]=4464;}return true;}
    if(q.reg==baudRegister(sensor)&&(sensor==0||sensor==2)){r[0]=badBaudRegister?77:baudCode(sensor,baud);return true;}
    return false;
  }
};
static uint32_t run(Job &j,Device &d,uint32_t start=0,Stage stop=Stage::Idle){
  Request q;uint32_t now=start;
  for(int n=0;n<500&&j.active();n++,now+=250){
    if(j.next(now,q)){
      if(q.fn==6)assert(allowedWrite(j.sensor,q.id,q.reg,q.value,j.targetBaud));
      j.sent();uint16_t r[2]={};bool ok=d.exchange(q,now,r);j.reply(ok,r,now);
      if(stop!=Stage::Idle&&j.stage==stop)return now;
    }
  }
  assert(!j.active());return now;
}
int main(){
  assert(supported(1));assert(!supported(5));
  for(uint16_t reg: {uint16_t(0),uint16_t(0x30),uint16_t(0x31),uint16_t(0x32),uint16_t(0x33),uint16_t(0x34)})assert(!allowedWrite(1,1,reg,1,4800));
  assert(!allowedWrite(0,0,0x30,2));assert(!allowedWrite(0,1,0x30,3));
  assert(!allowedWrite(0,1,0x31,4800,4800)); // Code is 0x4800, NOT decimal 4800.
  assert(allowedWrite(0,2,0x31,0x4800,4800));assert(allowedWrite(2,3,0x65,2,4800));
  assert(!allowedWrite(2,3,0x65,0x4800,4800));assert(!allowedWrite(3,2,0x31,0x4800,4800));
  assert(!allowedWrite(0,2,0x31,0x4800,2400));
  Job j;assert(!j.start(5,1));assert(!j.start(0,0));assert(!j.start(0,1,2400));assert(!j.start(3,1,4800));
  Device rain{1,1,4800};assert(j.start(1,1));run(j,rain);assert(j.stage==Stage::Done&&j.targetBaud==4800&&!j.writeSent&&rain.writes==0&&rain.reads==2);
  Device rain9600{1,1,9600};rain9600.sample=1;assert(j.start(1,1));run(j,rain9600);assert(j.stage==Stage::Done&&j.value==1&&j.targetBaud==9600&&!j.writeSent);
  Device rain2400{1,1,2400};assert(j.start(1,1));run(j,rain2400);assert(j.stage==Stage::Failed&&rain2400.writes==0);
  Device rainInvalid{1,1,4800};rainInvalid.sample=2;assert(j.start(1,1));run(j,rainInvalid);assert(j.stage==Stage::Failed);
  Device rainCustom{1,7,4800};assert(j.start(1,7));run(j,rainCustom);assert(j.finalId()==7&&rainCustom.writes==0);
  Device wind{0,1,9600};wind.sample=38;assert(j.start(0,1,4800));run(j,wind);assert(j.stage==Stage::Done&&wind.id==2&&wind.baud==4800&&wind.writes==2&&fabs(j.value-3.8)<1e-9);
  // Re-running a configured v3 device is read-only and finds both its new ID and baud.
  wind.writes=0;assert(j.start(0,1,4800));run(j,wind);assert(j.stage==Stage::Done&&wind.writes==0);
  // v2 upgrade: preserve assigned address, change baud only.
  Device upgrade{0,2,9600};upgrade.sample=38;assert(j.start(0,1,4800));run(j,upgrade);assert(j.stage==Stage::Done&&upgrade.writes==1&&upgrade.baud==4800);
  // A rain device at 9600 needs the outdoor sensors restored to 9600 too.
  Device reverse{0,2,4800};reverse.sample=38;assert(j.start(0,1,9600));run(j,reverse);assert(j.stage==Stage::Done&&reverse.writes==1&&reverse.baud==9600);
  Device light{2,1,9600};assert(j.start(2,1,4800));auto elapsed=run(j,light);assert(j.stage==Stage::Done&&j.value==70000&&light.id==3&&light.baud==4800&&light.writes==2&&elapsed>=25000);
  Device humidity{3,1,9600};humidity.sample=645;assert(j.start(3,1));run(j,humidity);assert(j.stage==Stage::Done&&j.value==64.5&&humidity.writes==1&&humidity.baud==9600);
  Device leak{4,1,9600};assert(j.start(4,1));run(j,leak);assert(j.stage==Stage::Done&&leak.writes==0);
  // A lost acknowledgement still requires address, baud and measurement readback.
  Device lost{2,1,9600};lost.dropEcho=true;assert(j.start(2,1,4800));run(j,lost);assert(j.stage==Stage::Done&&lost.writes==2);
  Device absent{0,1,9600};absent.present=false;assert(j.start(0,1,4800));run(j,absent);assert(j.stage==Stage::Failed&&absent.writes==0);
  Device ignored{0,1,9600};ignored.ignoreWrites=true;assert(j.start(0,1,4800));run(j,ignored);assert(j.stage==Stage::Failed&&ignored.writes==1);
  Device wrongBaudMap{2,3,9600};wrongBaudMap.badBaudRegister=true;assert(j.start(2,3,4800));run(j,wrongBaudMap);assert(j.stage==Stage::Failed&&wrongBaudMap.writes==0);
  Device badHumidity{3,1,9600};badHumidity.sample=1001;assert(j.start(3,1));run(j,badHumidity);assert(j.stage==Stage::Failed&&badHumidity.writes==0);
  // Power cut after the baud write: a new job recovers at the assigned ID/speed.
  Device interrupted{0,2,9600};assert(j.start(0,1,4800));run(j,interrupted,0,Stage::WaitBaud);
  assert(interrupted.baud==4800&&interrupted.writes==1);j=Job();assert(j.start(0,1,4800));run(j,interrupted);assert(j.stage==Stage::Done&&interrupted.writes==1);
  // Read pacing and millis rollover.
  j=Job();assert(j.start(1,1));Request q;assert(j.next(0xfffffff0,q));j.sent();uint16_t r[]={0,0};j.reply(true,r,0xfffffff0);
  assert(!j.next(100,q));assert(j.next(300,q));
  uint8_t tx[8],ex=0;Modbus::request(tx,1,6,0x31,0x4800);
  const uint8_t baudGolden[]={1,6,0,0x31,0x48,0,0xee,5};assert(!memcmp(tx,baudGolden,8));
  Modbus::request(tx,1,6,0x30,2);const uint8_t idGolden[]={1,6,0,0x30,0,2,8,4};assert(!memcmp(tx,idGolden,8));
  assert(Modbus::parseWrite(tx,8,tx,ex)==Modbus::OK);
  uint8_t wrong[8];Modbus::request(wrong,1,6,0x31,2);assert(Modbus::parseWrite(wrong,8,tx,ex)==Modbus::WRONG_ECHO);
  for(int i=0;i<8;i++)for(int bit=0;bit<8;bit++){memcpy(wrong,tx,8);wrong[i]^=1<<bit;assert(Modbus::parseWrite(wrong,8,tx,ex)!=Modbus::OK);}
  std::cout<<"Commissioning simulations passed: rain detection/read-only constraints, shared baud, v2 upgrade, write/readback failures, retries, interrupted writes and 32-bit lux.\n";
}
