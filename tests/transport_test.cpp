#include "../EnvironmentMonitorTesting/RS485Transport.h"
#include "../EnvironmentMonitorTesting/Protocol.h"
#include <assert.h>
#include <iostream>
#include <vector>

// Behavioural UART/transceiver model: noise arrives while RO is disabled;
// the earliest possible reply arrives as soon as reception is enabled.
struct Port {
  bool automatic=false,transmitting=false,txIdle=true;
  unsigned directionChanges=0;
  std::vector<uint8_t> noise={0x68,0x00,0x21};
  std::vector<uint8_t> response={0x02,0x03,0x02,0x48,0x00,0xca,0x44};
  std::vector<uint8_t> received,sent;
  void direction(bool tx) {
    assert(!automatic);
    ++directionChanges;
    if(!tx) {
      assert(txIdle); // The final transmitted stop bit must finish first.
      received.insert(received.end(),response.begin(),response.end());
    }
    transmitting=tx;
  }
  void write(const uint8_t *p,size_t n) {
    assert(automatic||transmitting);
    sent.assign(p,p+n);txIdle=false;
    if(!automatic)received.insert(received.end(),noise.begin(),noise.end());
  }
  void flush(bool txOnly) {
    txIdle=true;
    if(automatic)received.insert(received.end(),response.begin(),response.end());
    if(!txOnly)received.clear();
  }
};

int main() {
  uint8_t request[8],exception=0;uint16_t regs[8]={};
  Modbus::request(request,2,3,0x31,1);
  Port manual;
  // Reproduce the screenshot with the previous TX-only-flush sequence.
  manual.direction(true);manual.write(request,8);manual.flush(true);manual.direction(false);
  assert(manual.received==std::vector<uint8_t>({0x68,0,0x21,2,3,2,0x48,0,0xca,0x44}));
  assert(Modbus::parse(manual.received.data(),manual.received.size(),2,3,1,regs,exception)==Modbus::BAD_CRC);
  assert(Modbus::parse(manual.received.data()+3,7,2,3,1,regs,exception)==Modbus::OK&&regs[0]==0x4800);

  // Every frame used here is a real response, including an immediate reply.
  manual=Port{};
  RS485::transmit(manual,request,8,false,[&](bool tx){manual.direction(tx);});
  assert(manual.sent==std::vector<uint8_t>(request,request+8));
  assert(manual.received==manual.response&&manual.directionChanges==2&&!manual.transmitting);
  assert(Modbus::parse(manual.received.data(),manual.received.size(),2,3,1,regs,exception)==Modbus::OK&&regs[0]==0x4800);

  Port automatic;automatic.automatic=true;
  RS485::transmit(automatic,request,8,true,[&](bool tx){automatic.direction(tx);});
  assert(automatic.directionChanges==0&&automatic.received==automatic.response);
  assert(Modbus::parse(automatic.received.data(),automatic.received.size(),2,3,1,regs,exception)==Modbus::OK);

  // An FC06 response survives turnaround; a response is not evidence that
  // settings applied until Commission's independent readback succeeds.
  Modbus::request(request,2,6,0x31,0x4800);
  Port write;write.response.assign(request,request+8);
  RS485::transmit(write,request,8,false,[&](bool tx){write.direction(tx);});
  assert(Modbus::parseWrite(write.received.data(),write.received.size(),request,exception)==Modbus::OK);

  // Actual corrupt sensor bytes stay corrupt; this change never scans past
  // noise that arrives after the receiver has been enabled.
  Modbus::request(request,2,3,0x31,1);
  Port corrupt;corrupt.response[3]^=1;
  RS485::transmit(corrupt,request,8,false,[&](bool tx){corrupt.direction(tx);});
  assert(Modbus::parse(corrupt.received.data(),corrupt.received.size(),2,3,1,regs,exception)==Modbus::BAD_CRC);
  Port postSwitchNoise;postSwitchNoise.response.insert(postSwitchNoise.response.begin(),{0x68,0,0x21});
  RS485::transmit(postSwitchNoise,request,8,false,[&](bool tx){postSwitchNoise.direction(tx);});
  assert(Modbus::parse(postSwitchNoise.received.data(),postSwitchNoise.received.size(),2,3,1,regs,exception)==Modbus::BAD_CRC);

  std::cout<<"Transport tests passed: screenshot reproduced, manual TX noise cleared before RX, immediate read/write replies preserved, automatic-direction preserved, corrupt and post-switch-noise replies rejected.\n";
}
