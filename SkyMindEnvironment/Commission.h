#pragma once
#include "Protocol.h"

// Model-specific profiles. Rain compatibility is checked using reads only.
namespace Commission {
struct Profile { uint8_t bus,id,format; uint16_t reg,idRegister; double scale; uint32_t settleMs; };
inline Profile profile(uint8_t sensor) {
  const Profile p[]={{0,2,0,0,0x30,0.1,1000},
                     {0,1,0,0,0xffff,1,1000},
                     {0,3,2,0,0x64,1,25000},
                     {1,2,0,1,0x30,0.1,1000},
                     {1,1,0,0,0x02,1,1000}};
  return sensor<5?p[sensor]:Profile{};
}
inline bool supported(uint8_t sensor) {return sensor<5;}
inline bool commonBaud(uint32_t baud) {return baud==4800||baud==9600;}
inline uint16_t baudRegister(uint8_t sensor) {return sensor==0?0x31:sensor==2?0x65:0xffff;}
inline uint16_t baudCode(uint8_t sensor,uint32_t baud) {
  return sensor==0?(baud==4800?0x4800:0x9600):(baud==4800?2:3);
}
inline bool allowedWrite(uint8_t sensor,uint8_t id,uint16_t reg,uint16_t value,uint32_t targetBaud=9600) {
  if(!supported(sensor)||sensor==1||id<1||id>247)return false;
  if(reg==profile(sensor).idRegister)return value==profile(sensor).id;
  return (sensor==0||sensor==2)&&commonBaud(targetBaud)&&reg==baudRegister(sensor)&&value==baudCode(sensor,targetBaud);
}
inline bool validValue(uint8_t sensor,const uint16_t *r,double &value) {
  auto p=profile(sensor);uint32_t raw=0;
  value=Modbus::decode(r,p.format,false,raw)*p.scale;
  return isfinite(value)&&value>=0&&(sensor!=3||value<=100)&&((sensor!=1&&sensor!=4)||raw<=1);
}
enum class Stage { Idle, Identify, Sample, Write, Wait, VerifyAddress, CheckBaud, WriteBaud, WaitBaud, VerifyBaud, VerifySample, RainRead, RainConfirm, Done, Failed };
struct Request { uint8_t id,fn; uint16_t reg,value; uint32_t baud; };
class Job {
public:
  Stage stage=Stage::Idle;
  uint8_t sensor=0,current=1,initialId=1;
  bool inFlight=false,writeSent=false,triedTarget=false,triedOtherBaud=false;
  uint8_t attempts=0;
  uint32_t waitStart=0,waitMs=0,currentBaud=9600,targetBaud=9600,lastReply=0;
  bool paceRead=false;
  double value=0;
  const char* error="";
  bool active() const {return stage!=Stage::Idle&&stage!=Stage::Done&&stage!=Stage::Failed;}
  uint8_t finalId() const {return sensor==1?current:profile(sensor).id;}
  bool start(uint8_t s,uint8_t id,uint32_t busBaud=9600) {
    if(active()||!supported(s)||id<1||id>247||!commonBaud(busBaud)||(profile(s).bus==1&&busBaud!=9600))return false;
    *this=Job();sensor=s;initialId=current=id;targetBaud=busBaud;
    if(s==1){stage=Stage::RainRead;currentBaud=4800;}else stage=Stage::Identify;
    return true;
  }
  bool next(uint32_t now,Request &r) {
    if(!active()||inFlight)return false;
    // This also exceeds the rain manual's minimum 200 ms polling interval.
    if(paceRead&&uint32_t(now-lastReply)<250)return false;
    if(stage==Stage::Wait||stage==Stage::WaitBaud){
      if(uint32_t(now-waitStart)<waitMs)return false;
      stage=stage==Stage::Wait?Stage::VerifyAddress:Stage::VerifyBaud;
    }
    auto p=profile(sensor);r={current,3,p.idRegister,1,currentBaud};
    if(stage==Stage::Sample||stage==Stage::VerifySample||stage==Stage::RainRead||stage==Stage::RainConfirm){r.reg=p.reg;r.value=Modbus::registerCount(p.format);}
    if(stage==Stage::VerifyAddress||stage==Stage::VerifySample||stage==Stage::CheckBaud||stage==Stage::WriteBaud||stage==Stage::VerifyBaud)r.id=p.id;
    if(stage==Stage::Write){r.fn=6;r.reg=p.idRegister;r.value=p.id;}
    if(stage==Stage::CheckBaud||stage==Stage::VerifyBaud)r.reg=baudRegister(sensor);
    if(stage==Stage::WriteBaud){r.fn=6;r.reg=baudRegister(sensor);r.value=baudCode(sensor,targetBaud);}
    return true;
  }
  void sent(){inFlight=true;if(stage==Stage::Write||stage==Stage::WriteBaud)writeSent=true;}
  void fail(const char* why){error=why;stage=Stage::Failed;inFlight=false;}
  void afterAddress(){attempts=0;current=profile(sensor).id;stage=(sensor==0||sensor==2)?Stage::CheckBaud:Stage::VerifySample;}
  void reply(bool ok,const uint16_t *regs,uint32_t now) {
    if(!active()||!inFlight)return;
    inFlight=false;lastReply=now;paceRead=true;auto p=profile(sensor);
    if(stage==Stage::RainRead||stage==Stage::RainConfirm){
      if(!ok||!validValue(sensor,regs,value)){
        if(!triedOtherBaud){triedOtherBaud=true;currentBaud=9600;stage=Stage::RainRead;return;}
        fail("Rain did not return valid 0/1 status twice at 4800 or 9600 / 8N1. No settings were written. Check power, A/B, address or the exact model.");return;
      }
      if(stage==Stage::RainRead){stage=Stage::RainConfirm;return;}
      targetBaud=currentBaud;stage=Stage::Done;return;
    }
    if(stage==Stage::Write){
      // Lost echo is ambiguous: do not repeat a write; verify at the new ID.
      stage=Stage::Wait;waitStart=now;waitMs=p.settleMs;attempts=0;return;
    }
    if(stage==Stage::WriteBaud){
      currentBaud=targetBaud;stage=Stage::WaitBaud;waitStart=now;waitMs=1000;attempts=0;return;
    }
    if(stage==Stage::Identify){
      if(!ok){
        if(!triedTarget&&current!=p.id){current=p.id;triedTarget=true;return;}
        if(p.bus==0&&!triedOtherBaud){triedOtherBaud=true;currentBaud=4800;current=initialId;triedTarget=false;return;}
        fail("No valid address-register reply. Check the selected sensor, current ID, power, A/B and RSE wiring.");return;
      }
      if(regs[0]!=current){fail("Address register did not match. No write was sent; check the selected model.");return;}
      stage=Stage::Sample;return;
    }
    if(stage==Stage::Sample){
      if(!ok||!validValue(sensor,regs,value)){fail("Measurement check failed. No settings write was sent.");return;}
      if(current!=p.id)stage=Stage::Write;else afterAddress();return;
    }
    if(stage==Stage::VerifyAddress){
      if(!ok||regs[0]!=p.id){
        if(++attempts<5){stage=Stage::Wait;waitStart=now;waitMs=1000;return;}
        fail("New address not verified. It may have changed; keep the sensor isolated and retry setup.");return;
      }
      afterAddress();return;
    }
    if(stage==Stage::CheckBaud){
      if(!ok||regs[0]!=baudCode(sensor,currentBaud)){fail("Baud register did not match the active speed. No baud write was sent; retry with the correct model.");return;}
      stage=currentBaud==targetBaud?Stage::VerifySample:Stage::WriteBaud;return;
    }
    if(stage==Stage::VerifyBaud){
      if(!ok||regs[0]!=baudCode(sensor,targetBaud)){
        if(++attempts<5){stage=Stage::WaitBaud;waitStart=now;waitMs=1000;return;}
        fail("New baud rate not verified. It may have changed; keep the sensor isolated and retry setup.");return;
      }
      stage=Stage::VerifySample;return;
    }
    if(stage==Stage::VerifySample){
      if(!ok||!validValue(sensor,regs,value)){fail("Final measurement failed. Keep the sensor isolated and retry setup.");return;}
      current=p.id;stage=Stage::Done;
    }
  }
  const char* message() const {
    switch(stage){
      case Stage::Identify:return "Finding the sensor at its current or assigned address";
      case Stage::Sample:return "Checking a measurement before changing settings";
      case Stage::Write:return "Assigning the sensor address";
      case Stage::Wait:return sensor==2?"Waiting for the light sensor to apply its address (allow about 30 seconds)":"Waiting for the sensor to apply its address";
      case Stage::VerifyAddress:return "Verifying the new address";
      case Stage::CheckBaud:return "Checking the sensor baud setting";
      case Stage::WriteBaud:return "Setting the sensor to the shared outdoor bus speed";
      case Stage::WaitBaud:return "Waiting before checking the new baud rate";
      case Stage::VerifyBaud:return "Verifying the baud setting at the new speed";
      case Stage::VerifySample:return "Reading a measurement at the final address and speed";
      case Stage::RainRead:return currentBaud==4800?"Reading rain status at 4800 baud; no rain settings are written":"Trying rain status at 9600 baud; no rain settings are written";
      case Stage::RainConfirm:return "Confirming a second CRC-valid rain status reply";
      case Stage::Done:return "Communication verified";
      case Stage::Failed:return error;
      default:return "Set up rain first, then wind and light; indoor profiles can stay as configured";
    }
  }
};
}
